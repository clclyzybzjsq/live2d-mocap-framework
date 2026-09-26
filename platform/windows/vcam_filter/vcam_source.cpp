// lb_vcam_filter —— push-source 实现（无 strmbase，手工 COM）
#include "vcam_source.h"
#include <uuids.h>

#include <cstdio>
#include <cstring>

namespace lb::vcam {
namespace {

constexpr const wchar_t* kShmName = L"lb_vcam_frames";
constexpr int kDefaultW = 640;
constexpr int kDefaultH = 480;

// IAMStreamConfig / IKsPropertySet 的 IID：显式常量。
// 注意不要用工具链 strmiids 里的 IID_IAMStreamConfig 符号（取值不可靠，
// 实测与消费方（ffmpeg）请求的不一致，导致 QI 永远匹配不上）。
static const GUID kIID_IAMStreamConfig = {
    0xC6E13340, 0x30AC, 0x11D0, {0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56}};
// IKsPropertySet —— ffmpeg 用它查询 pin 的 KSPROPERTY 属性
static const GUID kIID_IKsPropertySet = {
    0x31EFAC30, 0x515C, 0x11D0, {0xA9, 0xAA, 0x00, 0xAA, 0x00, 0x61, 0xBE, 0x93}};
// AMPROPSETID_Pin / PIN_CATEGORY_CAPTURE
static const GUID kAMPROPSETID_Pin = {
    0x9B00F101, 0x1567, 0x11D1, {0xB3, 0xF1, 0x00, 0xAA, 0x00, 0x37, 0x61, 0xC5}};
static const GUID kPIN_CATEGORY_CAPTURE = {
    0xFB6C4281, 0x0353, 0x11D1, {0x90, 0x5F, 0x00, 0x00, 0xC0, 0xCC, 0x16, 0xBA}};
constexpr DWORD kAmPropertyPinCategory = 0;
constexpr DWORD kAmPropertyPinName = 1;
constexpr DWORD kKsPropertySupportGet = 1;

// ---- 简易枚举器：单 pin（元素类型与接口类型分离） ----
template <typename EnumT, typename ItemT>
class SingleEnum final : public EnumT {
public:
    explicit SingleEnum(ItemT* item) : item_(item) {
        if (item_) item_->AddRef();
    }
    ~SingleEnum() {
        if (item_) item_->Release();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        // 枚举器必须支持自身接口（调用方常 QI 回来）
        if (riid == IID_IUnknown || riid == __uuidof(EnumT)) {
            *ppv = static_cast<EnumT*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    // IEnum 公共
    STDMETHODIMP Next(ULONG c, ItemT** pp, ULONG* pc) {
        std::fprintf(stderr, "[lb_vcam] enum Next(c=%lu pos=%lu)\n",
                     static_cast<unsigned long>(c), static_cast<unsigned long>(pos_));
        if (!pp) return E_POINTER;
        ULONG got = 0;
        while (got < c && pos_ < 1) {
            pp[got] = item_;
            item_->AddRef();
            ++pos_;
            ++got;
        }
        if (pc) *pc = got;
        return got == c ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG c) {
        pos_ += c;
        return pos_ <= 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() { pos_ = 0; return S_OK; }
    STDMETHODIMP Clone(EnumT** pp) {
        if (!pp) return E_POINTER;
        SingleEnum* e = new SingleEnum(item_);
        e->pos_ = pos_;
        *pp = static_cast<EnumT*>(e);
        return S_OK;
    }

private:
    ItemT* item_;
    ULONG pos_ = 0;
    ULONG refs_ = 1;
};

using PinEnum = SingleEnum<IEnumPins, IPin>;

// media type 枚举：一次返回 YUY2
class MediaTypeEnum final : public IEnumMediaTypes {
public:
    explicit MediaTypeEnum(int w, int h) : w_(w), h_(h) {}
    ~MediaTypeEnum() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
            *ppv = static_cast<IEnumMediaTypes*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Next(ULONG c, AM_MEDIA_TYPE** pp, ULONG* pc) {
        if (!pp) return E_POINTER;
        ULONG got = 0;
        while (got < c && pos_ < 1) {
            AM_MEDIA_TYPE* mt = reinterpret_cast<AM_MEDIA_TYPE*>(
                CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
            if (!mt) break;
            make_video_type(mt, w_, h_);
            pp[got] = mt;
            ++pos_;
            ++got;
        }
        if (pc) *pc = got;
        return got == c ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG c) { pos_ += c; return pos_ <= 1 ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() { pos_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumMediaTypes** pp) {
        if (!pp) return E_POINTER;
        MediaTypeEnum* e = new MediaTypeEnum(w_, h_);
        e->pos_ = pos_;
        *pp = static_cast<IEnumMediaTypes*>(e);
        return S_OK;
    }

private:
    int w_;
    int h_;
    ULONG pos_ = 0;
    ULONG refs_ = 1;
};

// 读取共享内存当前尺寸（消费进程视角）
bool read_shm_size(const lb::core::SharedHeaderVcam* hdr, int* w, int* h) {
    if (!hdr || hdr->magic != lb::core::kVcamMagic) return false;
    if (hdr->width == 0 || hdr->height == 0) return false;
    *w = static_cast<int>(hdr->width);
    *h = static_cast<int>(hdr->height);
    return true;
}

// ---- 内置最小内存分配器（避免依赖系统 CLSID_MemoryAllocator/quartz.dll） ----
class VcamAllocator final : public IMemAllocator {
public:
    VcamAllocator() = default;
    ~VcamAllocator() { Decommit(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMemAllocator) {
            *ppv = static_cast<IMemAllocator*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP SetProperties(ALLOCATOR_PROPERTIES* pRequested,
                               ALLOCATOR_PROPERTIES* pActual) override {
        if (!pRequested) return E_POINTER;
        if (committed_) return VFW_E_ALREADY_COMMITTED;
        props_ = *pRequested;
        if (props_.cbAlign < 1) props_.cbAlign = 1;
        if (props_.cBuffers < 1) props_.cBuffers = 1;
        if (props_.cbBuffer < 1) props_.cbBuffer = 1;
        if (pActual) *pActual = props_;
        return S_OK;
    }
    STDMETHODIMP GetProperties(ALLOCATOR_PROPERTIES* pProps) override {
        if (!pProps) return E_POINTER;
        *pProps = props_;
        return S_OK;
    }
    STDMETHODIMP Commit() override {
        if (committed_) return S_OK;
        const size_t unit = (static_cast<size_t>(props_.cbBuffer) + props_.cbAlign - 1) /
                            props_.cbAlign * props_.cbAlign;
        buffer_ = new uint8_t[unit * props_.cBuffers];
        unit_ = unit;
        free_ = new bool[props_.cBuffers];
        memset(free_, 1, sizeof(bool) * props_.cBuffers);
        committed_ = true;
        return S_OK;
    }
    STDMETHODIMP Decommit() override {
        committed_ = false;
        delete[] buffer_;
        buffer_ = nullptr;
        delete[] free_;
        free_ = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetBuffer(IMediaSample** ppBuffer, REFERENCE_TIME*,
                           REFERENCE_TIME*, DWORD) override {
        if (!ppBuffer) return E_POINTER;
        if (!committed_ || !buffer_) return VFW_E_NOT_COMMITTED;
        for (ULONG i = 0; i < props_.cBuffers; ++i) {
            if (free_[i]) {
                free_[i] = false;
                VcamSample* s = new VcamSample(this, buffer_ + static_cast<size_t>(i) * unit_,
                                               unit_);
                *ppBuffer = s;
                return S_OK;
            }
        }
        return VFW_E_TIMEOUT;
    }
    STDMETHODIMP ReleaseBuffer(IMediaSample* pBuffer) override {
        // 由 sample 的 Release 回调归还
        (void)pBuffer;
        return S_OK;
    }

    void release_slot(uint8_t* ptr) {
        if (!committed_ || !buffer_ || !free_) return;
        const size_t idx = (ptr - buffer_) / unit_;
        if (idx < props_.cBuffers) free_[idx] = true;
    }

private:
    // sample 包装：让 IMediaSample 持有 IMemAllocator 的全局计数，归还时回调 slot
    class VcamSample final : public IMediaSample {
    public:
        VcamSample(VcamAllocator* owner, uint8_t* data, size_t size)
            : owner_(owner), data_(data), size_(size) {
            owner_->AddRef();
        }
        ~VcamSample() {
            if (owner_) {
                owner_->release_slot(data_);
                owner_->Release();
            }
        }
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == IID_IUnknown || riid == IID_IMediaSample) {
                *ppv = static_cast<IMediaSample*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
        STDMETHODIMP_(ULONG) Release() override {
            ULONG r = --refs_;
            if (r == 0) delete this;
            return r;
        }
        STDMETHODIMP GetPointer(BYTE** pp) override {
            if (!pp) return E_POINTER;
            *pp = data_;
            return S_OK;
        }
        STDMETHODIMP_(long) GetSize() override { return static_cast<long>(size_); }
        STDMETHODIMP GetTime(REFERENCE_TIME*, REFERENCE_TIME*) override { return E_POINTER; }
        STDMETHODIMP SetTime(REFERENCE_TIME*, REFERENCE_TIME*) override { return E_NOTIMPL; }
        STDMETHODIMP IsSyncPoint() override { return S_FALSE; }
        STDMETHODIMP SetSyncPoint(BOOL) override { return S_OK; }
        STDMETHODIMP IsPreroll() override { return S_FALSE; }
        STDMETHODIMP SetPreroll(BOOL) override { return S_OK; }
        STDMETHODIMP_(long) GetActualDataLength() override { return static_cast<long>(size_); }
        STDMETHODIMP SetActualDataLength(long) override { return S_OK; }
        STDMETHODIMP GetMediaType(AM_MEDIA_TYPE**) override { return E_NOTIMPL; }
        STDMETHODIMP SetMediaType(AM_MEDIA_TYPE*) override { return S_OK; }
        STDMETHODIMP IsDiscontinuity() override { return S_FALSE; }
        STDMETHODIMP SetDiscontinuity(BOOL) override { return S_OK; }
        STDMETHODIMP GetMediaTime(LONGLONG*, LONGLONG*) override { return E_NOTIMPL; }
        STDMETHODIMP SetMediaTime(LONGLONG*, LONGLONG*) override { return S_OK; }

    private:
        VcamAllocator* owner_;
        uint8_t* data_;
        size_t size_;
        ULONG refs_ = 1;
    };

    ALLOCATOR_PROPERTIES props_{};
    uint8_t* buffer_ = nullptr;
    bool* free_ = nullptr;
    size_t unit_ = 0;
    bool committed_ = false;
    ULONG refs_ = 1;
};

} // namespace

// ---- 媒体类型 ----
void make_video_type(AM_MEDIA_TYPE* mt, int w, int h) {
    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    mt->subtype = MEDIASUBTYPE_YUY2;
    mt->formattype = FORMAT_VideoInfo;
    mt->bFixedSizeSamples = TRUE;
    mt->bTemporalCompression = FALSE;
    mt->lSampleSize = static_cast<ULONG>(w) * h * 2;

    VIDEOINFOHEADER* vih = reinterpret_cast<VIDEOINFOHEADER*>(
        CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    memset(vih, 0, sizeof(*vih));
    vih->rcSource.right = w;
    vih->rcSource.bottom = h;
    vih->rcTarget.right = w;
    vih->rcTarget.bottom = h;
    vih->AvgTimePerFrame = 333333; // 30fps
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = w;
    vih->bmiHeader.biHeight = h;
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = 16;
    vih->bmiHeader.biCompression = MAKEFOURCC('U', 'Y', 'V', 'Y');
    vih->bmiHeader.biSizeImage = static_cast<DWORD>(w) * h * 2;
    mt->cbFormat = sizeof(VIDEOINFOHEADER);
    mt->pbFormat = reinterpret_cast<BYTE*>(vih);
}

void free_media_type(AM_MEDIA_TYPE* mt) {
    if (mt->pbFormat) CoTaskMemFree(mt->pbFormat);
    mt->pbFormat = nullptr;
    mt->cbFormat = 0;
}

// ---- push source ----
LBVcamPushSource::LBVcamPushSource() : pin_(new LBVcamOutputPin(this)) {}
LBVcamPushSource::~LBVcamPushSource() { Stop(); }

STDMETHODIMP LBVcamPushSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown) {
        *ppv = static_cast<IBaseFilter*>(this);
    } else if (riid == IID_IPersist || riid == IID_IPersistStream) {
        *ppv = static_cast<IPersist*>(this);
    } else if (riid == IID_IMediaFilter) {
        *ppv = static_cast<IMediaFilter*>(this);
    } else if (riid == IID_IBaseFilter) {
        *ppv = static_cast<IBaseFilter*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) LBVcamPushSource::AddRef() { return ++refs_; }
STDMETHODIMP_(ULONG) LBVcamPushSource::Release() {
    ULONG r = --refs_;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP LBVcamPushSource::GetClassID(CLSID* pClsID) {
    if (!pClsID) return E_POINTER;
    *pClsID = {0x2a17e0b2, 0x2a8c, 0x4e4a,
               {0x9e, 0x5c, 0x8b, 0x4a, 0x6d, 0x2f, 0x1c, 0x55}};
    return S_OK;
}

// -- IMediaFilter --
STDMETHODIMP LBVcamPushSource::Stop() {
    std::fprintf(stderr, "[lb_vcam] filter Stop\n");
    std::lock_guard<std::mutex> lock(state_lock_);
    if (state_ == State_Stopped) return S_OK;
    state_ = State_Stopped;
    run_thread_ = false;
    if (thread_.joinable()) thread_.join();
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::Pause() {
    std::fprintf(stderr, "[lb_vcam] filter Pause\n");
    std::lock_guard<std::mutex> lock(state_lock_);
    state_ = State_Paused;
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::Run(REFERENCE_TIME tStart) {
    std::fprintf(stderr, "[lb_vcam] filter Run\n");
    std::lock_guard<std::mutex> lock(state_lock_);
    if (state_ == State_Running) return S_OK;
    state_ = State_Running;
    next_ts_ = tStart;
    run_thread_ = true;
    if (!thread_.joinable()) {
        thread_ = std::thread(&LBVcamPushSource::push_thread, this);
    }
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::GetState(DWORD, FILTER_STATE* pState) {
    if (!pState) return E_POINTER;
    std::lock_guard<std::mutex> lock(state_lock_);
    *pState = state_;
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::SetSyncSource(IReferenceClock* pClock) {
    std::lock_guard<std::mutex> lock(state_lock_);
    if (clock_) clock_->Release();
    clock_ = pClock;
    if (clock_) clock_->AddRef();
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::GetSyncSource(IReferenceClock** ppClock) {
    if (!ppClock) return E_POINTER;
    std::lock_guard<std::mutex> lock(state_lock_);
    *ppClock = clock_;
    if (clock_) clock_->AddRef();
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::SetStopTime(const REFERENCE_TIME*) { return S_OK; }
STDMETHODIMP LBVcamPushSource::GetStopTime(REFERENCE_TIME* pT) {
    if (!pT) return E_POINTER;
    *pT = MAXLONGLONG;
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::SetStartTime(const REFERENCE_TIME*) { return S_OK; }
STDMETHODIMP LBVcamPushSource::GetStartTime(REFERENCE_TIME* pT) {
    if (!pT) return E_POINTER;
    *pT = 0;
    return S_OK;
}

// -- IBaseFilter --
STDMETHODIMP LBVcamPushSource::EnumPins(IEnumPins** ppEnum) {
    std::fprintf(stderr, "[lb_vcam] filter EnumPins\n");
    if (!ppEnum) return E_POINTER;
    *ppEnum = new PinEnum(pin_);
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    if (Id && (wcscmp(Id, L"Output") == 0 || wcscmp(Id, L"0") == 0)) {
        *ppPin = pin_;
        pin_->AddRef();
        return S_OK;
    }
    *ppPin = nullptr;
    return VFW_E_NOT_FOUND;
}
STDMETHODIMP LBVcamPushSource::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    wcscpy(pInfo->achName, L"LiveBridge Virtual Camera");
    std::lock_guard<std::mutex> lock(state_lock_);
    pInfo->pGraph = graph_;
    if (graph_) graph_->AddRef();
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR) {
    std::lock_guard<std::mutex> lock(state_lock_);
    if (graph_) graph_->Release();
    graph_ = pGraph;
    if (graph_) graph_->AddRef();
    return S_OK;
}
STDMETHODIMP LBVcamPushSource::QueryVendorInfo(LPWSTR* pVendorInfo) {
    if (!pVendorInfo) return E_POINTER;
    *pVendorInfo = nullptr;
    return E_NOTIMPL;
}

// 推流线程
void LBVcamPushSource::push_thread() {
    while (run_thread_) {
        // 等连接建立；未连接时慢循环
        if (!pin_->is_connected() || !pin_->allocator()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        IMemAllocator* alloc = pin_->allocator();
        IMediaSample* sample = nullptr;
        HRESULT hr = alloc->GetBuffer(&sample, nullptr, nullptr, 0);
        if (FAILED(hr) || !sample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        BYTE* data = nullptr;
        if (SUCCEEDED(sample->GetPointer(&data)) && data) {
            // 当前尺寸
            int w = kDefaultW, h = kDefaultH;
            read_shm_size(pin_->shm_header(), &w, &h);
            REFERENCE_TIME start = next_ts_;
            REFERENCE_TIME end = start + frame_duration_;
            next_ts_ = end;
            fill_frame(data, static_cast<size_t>(w) * 2u, w, h, start, end);
            sample->SetTime(&start, &end);
            sample->SetMediaTime(nullptr, nullptr);
            sample->SetSyncPoint(TRUE);
            sample->SetPreroll(FALSE);
            IMemInputPin* mem_in = pin_->mem_input();
            if (mem_in) {
                mem_in->Receive(sample);
            }
        }
        sample->Release();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}

void LBVcamPushSource::fill_frame(uint8_t* dst, size_t dst_pitch, int w, int h,
                                  REFERENCE_TIME start, REFERENCE_TIME end) {
    (void)start;
    (void)end;
    if (pin_->has_shm()) {
        const lb::core::SharedHeaderVcam* hdr = pin_->shm_header();
        if (hdr && hdr->frame_index > 0 &&
            hdr->width == static_cast<uint32_t>(w) &&
            hdr->height == static_cast<uint32_t>(h) &&
            hdr->state == lb::core::kVcamRunning) {
            // YUY2 直拷（共享内存即 YUY2）
            const uint8_t* src = pin_->shm_frame();
            if (hdr->pitch == static_cast<uint32_t>(w) * 2u) {
                memcpy(dst, src, static_cast<size_t>(w) * h * 2);
                return;
            }
            for (int y = 0; y < h; ++y) {
                memcpy(dst + static_cast<size_t>(y) * dst_pitch,
                       src + static_cast<size_t>(y) * hdr->pitch, dst_pitch);
            }
            return;
        }
    }
    fill_bars(dst, dst_pitch, w, h);
}

void LBVcamPushSource::fill_bars(uint8_t* dst, size_t dst_pitch, int w, int h) {
    // 8 条标准彩条（YUY2 版本）—— 无应用喂帧时仍能出画面
    struct Bar { int y; int u; int v; };
    static const Bar bars[8] = {
        {180, 128, 128}, // 白
        {162, 44, 202},  // 黄
        {131, 156, 44},  // 青
        {112, 72, 118},  // 绿
        {84, 184, 198},  // 品红
        {65, 100, 154},  // 红
        {35, 212, 70},   // 蓝
        {16, 128, 128},  // 黑
    };
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + static_cast<size_t>(y) * dst_pitch;
        for (int x = 0; x < w; x += 2) {
            const Bar& b = bars[(static_cast<size_t>(x) * 8) / w % 8];
            row[x * 2 + 0] = static_cast<uint8_t>(b.y);
            row[x * 2 + 1] = static_cast<uint8_t>(b.u);
            row[x * 2 + 2] = static_cast<uint8_t>(b.y);
            row[x * 2 + 3] = static_cast<uint8_t>(b.v);
        }
    }
}

// ---- output pin ----
LBVcamOutputPin::LBVcamOutputPin(LBVcamPushSource* owner) : owner_(owner) {}

STDMETHODIMP LBVcamOutputPin::QueryInterface(REFIID riid, void** ppv) {
    std::fprintf(stderr,
                 "[lb_vcam] pin QI {%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X} streamcfg_match=%d\n",
                 static_cast<unsigned long>(riid.Data1), riid.Data2, riid.Data3,
                 riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
                 riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7],
                 (riid == kIID_IAMStreamConfig) ? 1 : 0);
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == kIID_IAMStreamConfig) {
        std::fprintf(stderr, "[lb_vcam] pin QI -> IAMStreamConfig OK\n");
        *ppv = static_cast<IAMStreamConfig*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == kIID_IKsPropertySet) {
        *ppv = static_cast<IKsPropertySet*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) LBVcamOutputPin::AddRef() { return ++refs_; }
STDMETHODIMP_(ULONG) LBVcamOutputPin::Release() {
    ULONG r = --refs_;
    if (r == 0) delete this;
    return r;
}

bool LBVcamOutputPin::is_connected() const { return connected_; }
IMemInputPin* LBVcamOutputPin::mem_input() const { return mem_in_; }
IMemAllocator* LBVcamOutputPin::allocator() const { return alloc_; }
void LBVcamOutputPin::set_allocator(IMemAllocator* a) { alloc_ = a; }

void LBVcamOutputPin::acquire_shm() {
    shm_.map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kShmName);
    if (!shm_.map) return;
    shm_.view = MapViewOfFile(shm_.map, FILE_MAP_ALL_ACCESS, 0, 0,
                              static_cast<size_t>(lb::core::vcam_mapping_bytes()));
    if (!shm_.view) {
        CloseHandle(shm_.map);
        shm_.map = nullptr;
    }
}
void LBVcamOutputPin::release_shm() {
    if (shm_.view) {
        UnmapViewOfFile(shm_.view);
        shm_.view = nullptr;
    }
    if (shm_.map) {
        CloseHandle(shm_.map);
        shm_.map = nullptr;
    }
}
bool LBVcamOutputPin::has_shm() const { return shm_.view != nullptr; }
lb::core::SharedHeaderVcam* LBVcamOutputPin::shm_header() const {
    return static_cast<lb::core::SharedHeaderVcam*>(shm_.view);
}
const uint8_t* LBVcamOutputPin::shm_frame() const {
    return lb::core::vcam_frame_data(shm_header());
}

STDMETHODIMP LBVcamOutputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) {
    std::fprintf(stderr, "[lb_vcam] Connect enter (pmt=%p)\n",
                 static_cast<const void*>(pmt));
    if (!pReceivePin) return E_POINTER;
    std::lock_guard<std::mutex> lock(lock_);
    if (connected_) return VFW_E_ALREADY_CONNECTED;

    // 1) 协商媒体类型（pmt 为空则枚举默认 YUY2 640x480 -> 尝试读 shm 实际尺寸）
    int w = kDefaultW, h = kDefaultH;
    AM_MEDIA_TYPE type;
    if (pmt) {
        // 对方指定：接受 YUY2 即可，格式照抄（简化按缺省尺寸）
        type = *pmt;
        type.pbFormat = nullptr;
        type.cbFormat = 0;
    } else {
        make_video_type(&type, w, h);
    }
    std::fprintf(stderr, "[lb_vcam] Connect: type majortype=%d subtype=%d\n",
                 type.majortype.Data1, type.subtype.Data1);
    // 2) 检查下游 QueryAccept
    HRESULT hr = pReceivePin->QueryAccept(&type);
    std::fprintf(stderr, "[lb_vcam] Connect: QueryAccept hr=0x%08lX\n",
                 static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        free_media_type(&type);
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    // 3) 通知连接
    hr = pReceivePin->ReceiveConnection(static_cast<IPin*>(this), &type);
    std::fprintf(stderr, "[lb_vcam] Connect: ReceiveConnection hr=0x%08lX\n",
                 static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        free_media_type(&type);
        return hr;
    }
    // 释放临时类型（pmt 情形不释放对方的内存）
    if (!pmt) free_media_type(&type);

    // 4) 分配器：优先下游提供；缺省时自建标准内存分配器
    IMemInputPin* mem_in = nullptr;
    hr = pReceivePin->QueryInterface(IID_IMemInputPin, reinterpret_cast<void**>(&mem_in));
    if (FAILED(hr) || !mem_in) {
        std::fprintf(stderr, "[lb_vcam] Connect: no IMemInputPin\n");
        return VFW_E_NO_ALLOCATOR;
    }
    IMemAllocator* alloc = nullptr;
    hr = mem_in->GetAllocator(&alloc);
    std::fprintf(stderr, "[lb_vcam] Connect: GetAllocator hr=0x%08lX\n",
                 static_cast<unsigned long>(hr));
    if (FAILED(hr) || !alloc) {
        // 自建内置内存分配器（不依赖系统 quartz.dll）
        alloc = new VcamAllocator();
        ALLOCATOR_PROPERTIES props;
        props.cbBuffer = static_cast<LONG>(type.lSampleSize);
        props.cBuffers = 4;
        props.cbAlign = 1;
        props.cbPrefix = 0;
        alloc->SetProperties(&props, &props);
        alloc->Commit();
        own_alloc_ = true;
    }
    // 通知分配器
    mem_in->NotifyAllocator(alloc, FALSE);

    // 5) 记录连接
    peer_ = pReceivePin;
    peer_->AddRef();
    mem_in_ = mem_in;
    alloc_ = alloc;
    connected_ = true;
    acquire_shm();
    on_connected();
    std::fprintf(stderr, "[lb_vcam] Connect: S_OK\n");
    return S_OK;
}

void LBVcamOutputPin::on_connected() {
    if (shm_.view) {
        auto* hdr = shm_header();
        if (hdr->magic == lb::core::kVcamMagic) {
            hdr->consumer_count++;
            MemoryBarrier();
        }
    }
}

STDMETHODIMP LBVcamOutputPin::ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) {
    // push source 不应被反向连接
    return VFW_E_TYPE_NOT_ACCEPTED;
}

STDMETHODIMP LBVcamOutputPin::Disconnect() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!connected_) return S_FALSE;
    if (shm_.view) {
        auto* hdr = shm_header();
        if (hdr->magic == lb::core::kVcamMagic && hdr->consumer_count > 0) {
            hdr->consumer_count--;
            MemoryBarrier();
        }
    }
    release_shm();
    if (mem_in_) { mem_in_->Release(); mem_in_ = nullptr; }
    if (alloc_) {
        if (own_alloc_) {
            alloc_->Decommit();
            own_alloc_ = false;
        }
        alloc_->Release();
        alloc_ = nullptr;
    }
    if (peer_) { peer_->Release(); peer_ = nullptr; }
    connected_ = false;
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::ConnectedTo(IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    std::lock_guard<std::mutex> lock(lock_);
    if (!connected_) return VFW_E_NOT_CONNECTED;
    *ppPin = peer_;
    peer_->AddRef();
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    std::lock_guard<std::mutex> lock(lock_);
    if (!connected_) return VFW_E_NOT_CONNECTED;
    make_video_type(pmt, kDefaultW, kDefaultH);
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = owner_;
    if (pInfo->pFilter) pInfo->pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy(pInfo->achName, L"Output");
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::Notify(IBaseFilter*, PIN_DIRECTION) { return S_OK; }
STDMETHODIMP LBVcamOutputPin::QueryDirection(PIN_DIRECTION* pDir) {
    std::fprintf(stderr, "[lb_vcam] pin QueryDirection\n");
    if (!pDir) return E_POINTER;
    *pDir = PINDIR_OUTPUT;
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::QueryId(LPWSTR* id) {
    if (!id) return E_POINTER;
    wchar_t* buf = reinterpret_cast<wchar_t*>(
        CoTaskMemAlloc((wcslen(L"Output") + 1) * sizeof(wchar_t)));
    wcscpy(buf, L"Output");
    *id = buf;
    return S_OK;
}

// ---- IAMStreamConfig ----
// 能力以共享内存当前尺寸为准（无写端时为彩条的 640x480）。消费方（ffmpeg 等）
// 打开设备时会先 GetNumberOfCapabilities/GetStreamCaps 或 GetFormat 拿尺寸。
void LBVcamOutputPin::current_size(int* w, int* h) const {
    *w = kDefaultW;
    *h = kDefaultH;
    read_shm_size(shm_header(), w, h);
}

STDMETHODIMP LBVcamOutputPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    std::fprintf(stderr, "[lb_vcam] IAMStreamConfig::SetFormat\n");
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video || pmt->subtype != MEDIASUBTYPE_YUY2)
        return VFW_E_INVALIDMEDIATYPE;
    // 只接受与当前共享内存尺寸一致的请求：输出尺寸由写端决定，
    // 接受别的尺寸会导致消费方按错误 stride 解析画面
    if (pmt->formattype == FORMAT_VideoInfo && pmt->pbFormat) {
        const VIDEOINFOHEADER* vih = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
        int w = 0, h = 0;
        current_size(&w, &h);
        if (vih->bmiHeader.biWidth != w || vih->bmiHeader.biHeight != h)
            return VFW_E_INVALIDMEDIATYPE;
    }
    return S_OK;
}

STDMETHODIMP LBVcamOutputPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    std::fprintf(stderr, "[lb_vcam] IAMStreamConfig::GetFormat\n");
    if (!ppmt) return E_POINTER;
    int w = 0, h = 0;
    current_size(&w, &h);
    AM_MEDIA_TYPE* mt =
        reinterpret_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!mt) return E_OUTOFMEMORY;
    make_video_type(mt, w, h);
    *ppmt = mt;
    return S_OK;
}

STDMETHODIMP LBVcamOutputPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    std::fprintf(stderr, "[lb_vcam] IAMStreamConfig::GetNumberOfCapabilities\n");
    if (!piCount || !piSize) return E_POINTER;
    *piCount = 1;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP LBVcamOutputPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    std::fprintf(stderr, "[lb_vcam] IAMStreamConfig::GetStreamCaps(i=%d)\n", iIndex);
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex != 0) return S_FALSE;
    int w = 0, h = 0;
    current_size(&w, &h);

    AM_MEDIA_TYPE* mt =
        reinterpret_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!mt) return E_OUTOFMEMORY;
    make_video_type(mt, w, h);
    *ppmt = mt;

    VIDEO_STREAM_CONFIG_CAPS* caps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    memset(caps, 0, sizeof(*caps));
    caps->guid = FORMAT_VideoInfo;
    caps->VideoStandard = 0; // 非模拟视频源
    caps->InputSize.cx = w;
    caps->InputSize.cy = h;
    caps->MinCroppingSize.cx = w;
    caps->MinCroppingSize.cy = h;
    caps->MaxCroppingSize.cx = w;
    caps->MaxCroppingSize.cy = h;
    caps->CropGranularityX = 1;
    caps->CropGranularityY = 1;
    caps->MinOutputSize.cx = w;
    caps->MinOutputSize.cy = h;
    caps->MaxOutputSize.cx = w;
    caps->MaxOutputSize.cy = h;
    caps->OutputGranularityX = 1;
    caps->OutputGranularityY = 1;
    caps->MinFrameInterval = 333333; // 30fps
    caps->MaxFrameInterval = 333333;
    const LONG bps = static_cast<LONG>(w) * h * 2 * 30;
    caps->MinBitsPerSecond = bps;
    caps->MaxBitsPerSecond = bps;
    return S_OK;
}

// ---- IKsPropertySet ----
// ffmpeg 打开采集设备时会 Get(AMPROPSETID_Pin, AM_PROPERTY_PIN_CATEGORY)：
// 只有返回 PIN_CATEGORY_CAPTURE 的 output pin 才被认作采集 pin。
STDMETHODIMP LBVcamOutputPin::Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD) {
    return E_NOTIMPL; // 本属性集只读
}

STDMETHODIMP LBVcamOutputPin::Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID,
                                  DWORD, LPVOID pPropData, DWORD cbPropData,
                                  DWORD* pcbReturned) {
    if (pcbReturned) *pcbReturned = 0;
    if (!IsEqualGUID(guidPropSet, kAMPROPSETID_Pin)) return E_PROP_SET_UNSUPPORTED;

    if (dwPropID == kAmPropertyPinCategory) {
        if (pcbReturned) *pcbReturned = sizeof(GUID);
        if (!pPropData || cbPropData < sizeof(GUID)) return S_OK; // 仅查询大小
        *reinterpret_cast<GUID*>(pPropData) = kPIN_CATEGORY_CAPTURE;
        std::fprintf(stderr, "[lb_vcam] KsProperty: PIN_CATEGORY_CAPTURE returned\n");
        return S_OK;
    }
    if (dwPropID == kAmPropertyPinName) {
        const wchar_t* name = L"Capture";
        const DWORD need = static_cast<DWORD>((wcslen(name) + 1) * sizeof(wchar_t));
        if (pcbReturned) *pcbReturned = need;
        if (!pPropData || cbPropData < need) return S_OK;
        memcpy(pPropData, name, need);
        return S_OK;
    }
    return E_PROP_ID_UNSUPPORTED;
}

STDMETHODIMP LBVcamOutputPin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
                                             DWORD* pTypeSupport) {
    if (!pTypeSupport) return E_POINTER;
    if (IsEqualGUID(guidPropSet, kAMPROPSETID_Pin) &&
        (dwPropID == kAmPropertyPinCategory || dwPropID == kAmPropertyPinName)) {
        *pTypeSupport = kKsPropertySupportGet;
        return S_OK;
    }
    return E_PROP_SET_UNSUPPORTED;
}
STDMETHODIMP LBVcamOutputPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {    std::fprintf(stderr, "[lb_vcam] QueryAccept (maj=%d sub=%d fmtt=%d)\n",
                 pmt ? pmt->majortype.Data1 : -1,
                 pmt ? pmt->subtype.Data1 : -1,
                 pmt ? pmt->formattype.Data1 : -1);
    if (!pmt) return E_POINTER;
    if (pmt->majortype == MEDIATYPE_Video && pmt->subtype == MEDIASUBTYPE_YUY2)
        return S_OK;
    return S_FALSE;
}
STDMETHODIMP LBVcamOutputPin::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    std::fprintf(stderr, "[lb_vcam] EnumMediaTypes\n");
    if (!ppEnum) return E_POINTER;
    int w = kDefaultW, h = kDefaultH;
    read_shm_size(shm_header(), &w, &h);
    *ppEnum = new MediaTypeEnum(w, h);
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::QueryInternalConnections(IPin**, ULONG* n) {
    if (!n) return E_POINTER;
    *n = 0;
    return S_OK;
}
STDMETHODIMP LBVcamOutputPin::EndOfStream() { return S_OK; }
STDMETHODIMP LBVcamOutputPin::BeginFlush() { return S_OK; }
STDMETHODIMP LBVcamOutputPin::EndFlush() { return S_OK; }
STDMETHODIMP LBVcamOutputPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) {
    return S_OK;
}

} // namespace lb::vcam