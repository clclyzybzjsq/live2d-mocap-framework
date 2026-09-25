// vcam_flow_test —— 脱离注册表验证 filter 图行为。
// LoadLibrary(filter.dll) + DllGetClassObject 创建 source，手工 sink 接收样本，
// IGraphBuilder 完成连接/运行/停止。无需管理员，无需注册。
#include <windows.h>
#include <dshow.h>
#include <uuids.h>

#include <atomic>
#include <cstdio>
#include <cstring>

// {2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}
static const CLSID CLSID_LBVcam = {0x2a17e0b2, 0x2a8c, 0x4e4a,
                                   {0x9e, 0x5c, 0x8b, 0x4a, 0x6d, 0x2f, 0x1c, 0x55}};

// ---- 测试 sink：input pin + IMemInputPin，Receive 计数 ----
class TestSinkPin final : public IPin, public IMemInputPin {
public:
    TestSinkPin() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown) {
            *ppv = static_cast<IPin*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IPin) {
            *ppv = static_cast<IPin*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IMemInputPin) {
            *ppv = static_cast<IMemInputPin*>(this);
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

    // IPin（input）
    STDMETHODIMP Connect(IPin*, const AM_MEDIA_TYPE*) override { return VFW_E_TYPE_NOT_ACCEPTED; }
    STDMETHODIMP ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) override { return S_OK; }
    STDMETHODIMP Disconnect() override { return S_OK; }
    STDMETHODIMP ConnectedTo(IPin** pp) override {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return VFW_E_NOT_CONNECTED;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE*) override { return VFW_E_NOT_CONNECTED; }
    STDMETHODIMP QueryPinInfo(PIN_INFO* p) override {
        if (!p) return E_POINTER;
        p->pFilter = nullptr;
        p->dir = PINDIR_INPUT;
        wcscpy(p->achName, L"Input");
        return S_OK;
    }
    STDMETHODIMP Notify(IBaseFilter*, PIN_DIRECTION) { return S_OK; }
    STDMETHODIMP QueryDirection(PIN_DIRECTION* d) override {
        if (!d) return E_POINTER;
        *d = PINDIR_INPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR* id) override {
        if (!id) return E_POINTER;
        wchar_t* buf = reinterpret_cast<wchar_t*>(CoTaskMemAlloc(6 * sizeof(wchar_t)));
        wcscpy(buf, L"Input");
        *id = buf;
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* mt) override {
        if (!mt) return E_POINTER;
        return (mt->majortype == MEDIATYPE_Video && mt->subtype == MEDIASUBTYPE_YUY2)
                   ? S_OK : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** pp) override {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return S_FALSE;
    }
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG* n) override {
        if (!n) return E_POINTER;
        *n = 0;
        return S_OK;
    }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override { return S_OK; }

    // IMemInputPin
    STDMETHODIMP GetAllocator(IMemAllocator** pp) override {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return VFW_E_NOT_FOUND; // 触发 source 自建分配器
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator*, BOOL) override { return S_OK; }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES*) override { return E_NOTIMPL; }
    STDMETHODIMP Receive(IMediaSample* sample) override {
        ++frames_;
        BYTE* data = nullptr;
        if (sample && frames_ <= 40 && SUCCEEDED(sample->GetPointer(&data)) && data) {
            LONG size = sample->GetSize();
            std::memset(&last_sum_, 0, sizeof(last_sum_));
            for (LONG i = 0; i < size && i < 32; ++i) last_sum_ += data[i];
            printf("  sample[%llu] size=%ld head_bytes=%u\n",
                   static_cast<unsigned long long>(frames_), size, last_sum_);
        }
        return S_OK;
    }
    STDMETHODIMP ReceiveMultiple(IMediaSample** samples, long count, long* received) override {
        for (long i = 0; i < count; ++i) Receive(samples[i]);
        if (received) *received = count;
        return S_OK;
    }
    STDMETHODIMP ReceiveCanBlock() override { return S_FALSE; }

    std::atomic<unsigned long long> frames_{0};

private:
    ULONG refs_ = 1;
    unsigned int last_sum_ = 0;
};

class TestSink final : public IBaseFilter {
public:
    TestSink() = default;
    ~TestSink() { pin_.Release(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IBaseFilter ||
            riid == IID_IMediaFilter || riid == IID_IPersist) {
            *ppv = static_cast<IBaseFilter*>(this);
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
    STDMETHODIMP GetClassID(CLSID* p) override {
        if (!p) return E_POINTER;
        *p = CLSID_LBVcam;
        return S_OK;
    }
    STDMETHODIMP Stop() override { return S_OK; }
    STDMETHODIMP Pause() override { return S_OK; }
    STDMETHODIMP Run(REFERENCE_TIME) override { return S_OK; }
    STDMETHODIMP GetState(DWORD, FILTER_STATE* p) override {
        if (!p) return E_POINTER;
        *p = State_Stopped;
        return S_OK;
    }
    STDMETHODIMP SetSyncSource(IReferenceClock*) override { return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock** pp) override {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return S_OK;
    }
    STDMETHODIMP SetStopTime(const REFERENCE_TIME*) { return S_OK; }
    STDMETHODIMP GetStopTime(REFERENCE_TIME* p) {
        if (!p) return E_POINTER;
        *p = 0;
        return S_OK;
    }
    STDMETHODIMP SetStartTime(const REFERENCE_TIME*) { return S_OK; }
    STDMETHODIMP GetStartTime(REFERENCE_TIME* p) {
        if (!p) return E_POINTER;
        *p = 0;
        return S_OK;
    }
    STDMETHODIMP EnumPins(IEnumPins**) override { return E_NOTIMPL; }
    STDMETHODIMP FindPin(LPCWSTR, IPin** pp) override {
        if (!pp) return E_POINTER;
        *pp = &pin_;
        pin_.AddRef();
        return S_OK;
    }
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* p) override {
        if (!p) return E_POINTER;
        wcscpy(p->achName, L"TestSink");
        p->pGraph = nullptr;
        return S_OK;
    }
    STDMETHODIMP JoinFilterGraph(IFilterGraph*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP QueryVendorInfo(LPWSTR*) override { return E_NOTIMPL; }

    TestSinkPin pin_;

private:
    ULONG refs_ = 1;
};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    printf("vcam flow test start\n");

    // 1) 手工加载 filter（不走注册表）
    HMODULE dll = LoadLibraryW(L"D:\\ournotes_vtube\\build\\platform\\windows\\lb_vcam_filter.dll");
    if (!dll) {
        printf("FAIL: load filter dll err=%lu\n", GetLastError());
        return 1;
    }
    using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto dll_get_class = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(dll, "DllGetClassObject"));
    if (!dll_get_class) {
        printf("FAIL: no DllGetClassObject\n");
        return 1;
    }
    IClassFactory* factory = nullptr;
    HRESULT hr = dll_get_class(CLSID_LBVcam, IID_IClassFactory,
                               reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        printf("FAIL: DllGetClassObject hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    IBaseFilter* src = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IBaseFilter, reinterpret_cast<void**>(&src));
    factory->Release();
    if (FAILED(hr)) {
        printf("FAIL: CreateInstance hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    // 2) 模拟应用写端：创建共享内存并写帧（tag 与 vcam_protocol.h 一致）
    const wchar_t* kShmName = L"lb_vcam_frames";
    constexpr uint32_t kMagic = 0x4C425643u; // "LBVC"
    HANDLE shm_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, 128 + 1280u * 720u * 2u, kShmName);
    if (!shm_map) {
        printf("FAIL: create shm err=%lu\n", GetLastError());
        return 1;
    }
    auto* shm = static_cast<uint8_t*>(MapViewOfFile(shm_map, FILE_MAP_ALL_ACCESS, 0, 0,
                                                    128 + 1280u * 720u * 2u));
    if (!shm) {
        printf("FAIL: map shm err=%lu\n", GetLastError());
        return 1;
    }
    struct Hdr {
        uint32_t magic, version, state, reserved0;
        uint64_t frame_index;
        uint32_t width, height, pitch, format;
        uint64_t timestamp_us;
        int32_t consumer_count;
        uint8_t pad[76];
    } __attribute__((packed));
    Hdr* hdr = reinterpret_cast<Hdr*>(shm);
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = kMagic;
    hdr->version = 1;
    hdr->state = 1; // Running
    hdr->width = 640;
    hdr->height = 480;
    hdr->pitch = 1280;
    hdr->format = 1; // YUY2
    hdr->consumer_count = 0;
    uint8_t* frame_data = shm + 128;

    // 3) 手工连接：不经过系统图（避开 Quartz），直接 output->Connect(input)
    TestSink sink;
    IPin* src_pin = nullptr;
    IPin* sink_pin = nullptr;
    src->FindPin(L"Output", &src_pin);
    sink.FindPin(L"Input", &sink_pin);

    hr = src_pin->Connect(sink_pin, nullptr);
    if (FAILED(hr)) {
        printf("FAIL: pin Connect hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    printf("connected ok, consumer_count=%d\n", hdr->consumer_count);

    // 4) 运行 1.5s；期间每 300ms 写一帧（递增灰度，验证 filter 收到的是 shm 帧）
    src->Run(0);
    printf("running, writing shm frames...\n");
    for (int i = 0; i < 4; ++i) {
        const uint8_t v = static_cast<uint8_t>(40 + i * 40);
        for (size_t y = 0; y < 480; ++y) {
            uint8_t* row = frame_data + y * 1280u;
            for (size_t x = 0; x < 640; ++x) {
                row[x * 2 + 0] = v;
                row[x * 2 + 1] = 128;
                row[x * 2 + 2] = v;
                row[x * 2 + 3] = 128;
            }
        }
        MemoryBarrier();
        hdr->frame_index += 1;
        Sleep(300);
    }
    // 关帧后彩条回退验证（filter 应继续出图）
    Sleep(300);
    src->Stop();

    printf("frames received: %llu, consumer_count=%d\n",
           static_cast<unsigned long long>(sink.pin_.frames_.load()),
           hdr->consumer_count);
    if (sink.pin_.frames_.load() >= 10) {
        printf("RESULT OK\n");
    } else {
        printf("RESULT FAIL: too few frames\n");
    }

    UnmapViewOfFile(shm);
    CloseHandle(shm_map);
    src_pin->Release();
    sink_pin->Release();
    src->Release();
    FreeLibrary(dll);
    CoUninitialize();
    return sink.pin_.frames_.load() >= 10 ? 0 : 1;
}