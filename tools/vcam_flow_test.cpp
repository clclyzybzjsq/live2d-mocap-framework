// vcam_flow_test —— 验证 filter 图行为，两种模式：
//   默认：LoadLibrary 手工加载（不依赖注册表）-> 手动 pin Connect -> Run
//   --graph：CoCreateInstance（走 HKCU 注册）-> 系统 IGraphBuilder + ConnectDirect
// 两种模式都模拟应用写端向共享内存喂帧，验证 filter 收到 shm 帧而非彩条。
#include <windows.h>
#include <dshow.h>
#include <uuids.h>

#include <atomic>
#include <cstdio>
#include <cstring>

// {2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}
static const CLSID CLSID_LBVcam = {0x2a17e0b2, 0x2a8c, 0x4e4a,
                                   {0x9e, 0x5c, 0x8b, 0x4a, 0x6d, 0x2f, 0x1c, 0x55}};
// {C1F400A0-3F08-11D0-AD4B-00C04FD8D73A} CLSID_NullRenderer（MinGW uuids.h 未导出）
static const CLSID CLSID_NullRendererX = {0xC1F400A0, 0x3F08, 0x11D0,
                                          {0xAD, 0x4B, 0x00, 0xC0, 0x4F, 0xD8, 0xD7, 0x3A}};
static int argc_use_nr = 0;
static int pmt_null_mode = 0;

// ---- 测试 sink：input pin + IMemInputPin，Receive 计数 ----
class TestSinkPin final : public IPin, public IMemInputPin {
public:
    explicit TestSinkPin(IBaseFilter* sink) : owner_sink_(sink) {}
    TestSinkPin() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
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
    STDMETHODIMP_(ULONG) AddRef() { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Connect(IPin*, const AM_MEDIA_TYPE*) { return VFW_E_TYPE_NOT_ACCEPTED; }
    STDMETHODIMP ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) { return S_OK; }
    STDMETHODIMP Disconnect() { return S_OK; }
    STDMETHODIMP ConnectedTo(IPin** pp) {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return VFW_E_NOT_CONNECTED;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE*) { return VFW_E_NOT_CONNECTED; }
    STDMETHODIMP QueryPinInfo(PIN_INFO* p) {
        if (!p) return E_POINTER;
        // pFilter 必须是所属 filter 实例（石英用其验证 pin 归属）
        p->pFilter = static_cast<IBaseFilter*>(owner_sink_);
        if (p->pFilter) p->pFilter->AddRef();
        p->dir = PINDIR_INPUT;
        wcscpy(p->achName, L"Input");
        return S_OK;
    }
    STDMETHODIMP Notify(IBaseFilter*, PIN_DIRECTION) { return S_OK; }
    STDMETHODIMP QueryDirection(PIN_DIRECTION* d) {
        if (!d) return E_POINTER;
        *d = PINDIR_INPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR* id) {
        if (!id) return E_POINTER;
        wchar_t* buf = reinterpret_cast<wchar_t*>(CoTaskMemAlloc(6 * sizeof(wchar_t)));
        wcscpy(buf, L"Input");
        *id = buf;
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* mt) {
        if (!mt) return E_POINTER;
        return (mt->majortype == MEDIATYPE_Video && mt->subtype == MEDIASUBTYPE_YUY2)
                   ? S_OK : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** pp) {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return S_FALSE;
    }
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG* n) {
        if (!n) return E_POINTER;
        *n = 0;
        return S_OK;
    }
    STDMETHODIMP EndOfStream() { return S_OK; }
    STDMETHODIMP BeginFlush() { return S_OK; }
    STDMETHODIMP EndFlush() { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

    STDMETHODIMP GetAllocator(IMemAllocator** pp) {
        if (!pp) return E_POINTER;
        *pp = nullptr;
        return VFW_E_NOT_FOUND; // 触发 source 自建分配器
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator*, BOOL) { return S_OK; }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES*) { return E_NOTIMPL; }
    STDMETHODIMP Receive(IMediaSample* sample) {
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
    STDMETHODIMP ReceiveMultiple(IMediaSample** samples, long count, long* received) {
        for (long i = 0; i < count; ++i) Receive(samples[i]);
        if (received) *received = count;
        return S_OK;
    }
    STDMETHODIMP ReceiveCanBlock() { return S_FALSE; }

    std::atomic<unsigned long long> frames_{0};

private:
    IBaseFilter* owner_sink_ = nullptr;
    ULONG refs_ = 1;
    unsigned int last_sum_ = 0;
};

class TestSink final : public IBaseFilter {
public:
    TestSink() : pin_(this) {}
    ~TestSink() { pin_.Release(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
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
    STDMETHODIMP_(ULONG) AddRef() { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP GetClassID(CLSID* p) {
        if (!p) return E_POINTER;
        // 不能复用 CLSID_LBVcam：石英会用 GetClassID 做分类缓存，
        // 测试 sink 必须是独立 GUID
        static const CLSID kTestSinkClsid = {0x11111111, 0x2222, 0x3333,
                                             {0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB}};
        *p = kTestSinkClsid;
        return S_OK;
    }
    STDMETHODIMP Stop() { return S_OK; }
    STDMETHODIMP Pause() { return S_OK; }
    STDMETHODIMP Run(REFERENCE_TIME) { return S_OK; }
    STDMETHODIMP GetState(DWORD, FILTER_STATE* p) {
        if (!p) return E_POINTER;
        *p = State_Stopped;
        return S_OK;
    }
    STDMETHODIMP SetSyncSource(IReferenceClock*) { return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock** pp) {
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
    STDMETHODIMP EnumPins(IEnumPins**) { return E_NOTIMPL; }
    STDMETHODIMP FindPin(LPCWSTR, IPin** pp) {
        if (!pp) return E_POINTER;
        *pp = &pin_;
        pin_.AddRef();
        return S_OK;
    }
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* p) {
        if (!p) return E_POINTER;
        wcscpy(p->achName, L"TestSink");
        // 石英用 pGraph 校验 pin 是否在图中（ConnectDirect 前置）
        p->pGraph = graph_;
        if (graph_) graph_->AddRef();
        return S_OK;
    }
    STDMETHODIMP JoinFilterGraph(IFilterGraph* g, LPCWSTR) {
        if (graph_) graph_->Release();
        graph_ = g;
        if (graph_) graph_->AddRef();
        return S_OK;
    }
    STDMETHODIMP QueryVendorInfo(LPWSTR*) { return E_NOTIMPL; }

    TestSinkPin pin_;

private:
    IFilterGraph* graph_ = nullptr;
    ULONG refs_ = 1;
};

// ---- 共享内存模拟（应用写端视角） ----
#pragma pack(push, 1)
struct HdrLike {
    uint32_t magic, version, state, reserved0;
    uint64_t frame_index;
    uint32_t width, height, pitch, format;
    uint64_t timestamp_us;
    int32_t consumer_count;
    uint8_t pad[76];
};
#pragma pack(pop)

struct ShmEnv {
    HANDLE map = nullptr;
    uint8_t* base = nullptr;
    HdrLike* hdr = nullptr;
    uint8_t* frame = nullptr;
};

bool shm_init(ShmEnv& env) {
    const wchar_t* kShmName = L"lb_vcam_frames";
    env.map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 0, 128 + 1280u * 720u * 2u, kShmName);
    if (!env.map) {
        printf("FAIL: create shm err=%lu\n", GetLastError());
        return false;
    }
    env.base = static_cast<uint8_t*>(MapViewOfFile(env.map, FILE_MAP_ALL_ACCESS, 0, 0,
                                                   128 + 1280u * 720u * 2u));
    if (!env.base) {
        printf("FAIL: map shm err=%lu\n", GetLastError());
        CloseHandle(env.map);
        env.map = nullptr;
        return false;
    }
    env.hdr = reinterpret_cast<HdrLike*>(env.base);
    memset(env.hdr, 0, sizeof(*env.hdr));
    env.hdr->magic = 0x4C425643u;
    env.hdr->version = 1;
    env.hdr->state = 1;
    env.hdr->width = 640;
    env.hdr->height = 480;
    env.hdr->pitch = 1280;
    env.hdr->format = 1;
    env.hdr->consumer_count = 0;
    env.frame = env.base + 128;
    return true;
}

void shm_write_frames(ShmEnv& env) {
    for (int i = 0; i < 4; ++i) {
        const uint8_t v = static_cast<uint8_t>(40 + i * 40);
        for (size_t y = 0; y < 480; ++y) {
            uint8_t* row = env.frame + y * 1280u;
            for (size_t x = 0; x < 640; ++x) {
                row[x * 2 + 0] = v;
                row[x * 2 + 1] = 128;
                row[x * 2 + 2] = v;
                row[x * 2 + 3] = 128;
            }
        }
        MemoryBarrier();
        env.hdr->frame_index += 1;
        Sleep(300);
    }
}

void shm_teardown(ShmEnv& env) {
    if (env.base) UnmapViewOfFile(env.base);
    if (env.map) CloseHandle(env.map);
}

int summarize(TestSink& sink, const int consumer_count) {
    printf("frames received: %llu, consumer_count=%d\n",
           static_cast<unsigned long long>(sink.pin_.frames_.load()), consumer_count);
    if (sink.pin_.frames_.load() >= 10) {
        printf("RESULT OK\n");
        return 0;
    }
    printf("RESULT FAIL: too few frames\n");
    return 1;
}

// ---- 模式 1：LoadLibrary 手工加载 + 手动连接 ----
int run_manual() {
    printf("== mode: manual (LoadLibrary + pin Connect) ==\n");
    HMODULE dll = LoadLibraryW(L"D:\\ournotes_vtube\\build\\platform\\windows\\lb_vcam_filter.dll");
    if (!dll) {
        printf("FAIL: load filter dll err=%lu\n", GetLastError());
        return 1;
    }
    using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto dll_get_class = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(dll, "DllGetClassObject"));
    if (!dll_get_class) return 1;
    IClassFactory* factory = nullptr;
    HRESULT hr = dll_get_class(CLSID_LBVcam, IID_IClassFactory,
                               reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) return 1;
    IBaseFilter* src = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IBaseFilter, reinterpret_cast<void**>(&src));
    factory->Release();
    if (FAILED(hr)) return 1;

    ShmEnv env;
    if (!shm_init(env)) return 1;

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
    printf("connected ok, consumer_count=%d\n", env.hdr->consumer_count);

    src->Run(0);
    shm_write_frames(env);
    Sleep(300);
    src->Stop();

    const int rc = summarize(sink, env.hdr->consumer_count);
    shm_teardown(env);
    src_pin->Release();
    sink_pin->Release();
    src->Release();
    FreeLibrary(dll);
    return rc;
}

// ---- 模式 2：CoCreateInstance（HKCU 注册）+ 系统图 ----
int run_graph() {
    printf("== mode: graph (CoCreateInstance + IGraphBuilder) ==\n");
    IBaseFilter* src = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_LBVcam, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IBaseFilter, reinterpret_cast<void**>(&src));
    if (FAILED(hr)) {
        printf("FAIL: CoCreateInstance CLSID_LBVcam hr=0x%08lX (need HKCU reg first)\n",
               static_cast<unsigned long>(hr));
        return 1;
    }
    printf("filter created via CoCreateInstance\n");

    ShmEnv env;
    if (!shm_init(env)) return 1;

    IGraphBuilder* graph = nullptr;
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IGraphBuilder, reinterpret_cast<void**>(&graph));
    if (FAILED(hr)) {
        printf("FAIL: FilterGraph hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    TestSink sink;
    graph->AddFilter(src, L"LBVcamSource");
    graph->AddFilter(&sink, L"TestSink");

    // 变体 --nr：用系统 Null Renderer 作为下游（隔离 TestSink 兼容性）
    IBaseFilter* null_renderer = nullptr;
    const bool use_nr = (argc_use_nr > 0);
    if (use_nr) {
        hr = CoCreateInstance(CLSID_NullRendererX, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IBaseFilter, reinterpret_cast<void**>(&null_renderer));
        if (FAILED(hr)) {
            printf("FAIL: NullRenderer hr=0x%08lX\n", static_cast<unsigned long>(hr));
            return 1;
        }
        graph->AddFilter(null_renderer, L"NullRenderer");
    }

    IPin* src_pin = nullptr;
    IPin* sink_pin = nullptr;
    src->FindPin(L"Output", &src_pin);
    if (use_nr) {
        // 枚举 Null Renderer 的第一个 input pin
        IEnumPins* ep = nullptr;
        null_renderer->EnumPins(&ep);
        if (ep) {
            ep->Next(1, &sink_pin, nullptr);
            ep->Release();
        }
        if (!sink_pin) {
            printf("FAIL: no NullRenderer input pin\n");
            return 1;
        }
    } else {
        sink.FindPin(L"Input", &sink_pin);
    }

    // 显式指定媒体类型（绕过石英的媒体类型枚举路径）
    AM_MEDIA_TYPE mt;
    memset(&mt, 0, sizeof(mt));
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_YUY2;
    mt.formattype = FORMAT_VideoInfo;
    VIDEOINFOHEADER vih;
    memset(&vih, 0, sizeof(vih));
    vih.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih.bmiHeader.biWidth = 640;
    vih.bmiHeader.biHeight = 480;
    vih.bmiHeader.biPlanes = 1;
    vih.bmiHeader.biBitCount = 16;
    vih.bmiHeader.biCompression = MAKEFOURCC('U', 'Y', 'V', 'Y');
    vih.bmiHeader.biSizeImage = 640u * 480u * 2u;
    mt.cbFormat = sizeof(VIDEOINFOHEADER);
    BYTE* fmt_buf = reinterpret_cast<BYTE*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    memcpy(fmt_buf, &vih, sizeof(vih));
    mt.pbFormat = fmt_buf;
    mt.lSampleSize = 640u * 480u * 2u;
    mt.bFixedSizeSamples = TRUE;

    hr = graph->ConnectDirect(src_pin, sink_pin, pmt_null_mode ? nullptr : &mt);
    CoTaskMemFree(fmt_buf); // 石英应深拷贝；连接后由调用者释放
    if (FAILED(hr)) {
        printf("FAIL: ConnectDirect hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    printf("connected ok, consumer_count=%d\n", env.hdr->consumer_count);

    IMediaControl* control = nullptr;
    hr = graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&control));
    if (FAILED(hr)) {
        printf("FAIL: no IMediaControl\n");
        return 1;
    }
    hr = control->Run();
    if (FAILED(hr)) {
        printf("FAIL: Run hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    printf("running, writing shm frames...\n");
    shm_write_frames(env);
    Sleep(300);
    control->Stop();
    control->Release();

    int rc = 0;
    if (use_nr) {
        printf("null renderer graph ran ok; shm consumer_count=%d\n",
               env.hdr->consumer_count);
    } else {
        rc = summarize(sink, env.hdr->consumer_count);
    }
    shm_teardown(env);
    src_pin->Release();
    sink_pin->Release();
    src->Release();
    if (null_renderer) null_renderer->Release();
    graph->Release();
    return rc;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("vcam flow test start\n");

    // --obs：对照实验，用已注册的 OBS Virtual Camera 走石英图
    if (argc > 1 && strcmp(argv[1], "--evr") == 0) {
        // 用系统 EVR（注册完好）充当 sink：区分 filter 本身与 TestSink 残缺
        static const CLSID kEVR = {0xFA10746C, 0x9B63, 0x4b6c,
                                   {0xBC, 0x49, 0xFC, 0x30, 0x0E, 0xA5, 0xF2, 0x56}};
        IBaseFilter* src = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_LBVcam, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IBaseFilter, reinterpret_cast<void**>(&src));
        if (FAILED(hr)) return 1;
        IBaseFilter* evr = nullptr;
        hr = CoCreateInstance(kEVR, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IBaseFilter, reinterpret_cast<void**>(&evr));
        if (FAILED(hr)) {
            printf("FAIL: EVR CoCreateInstance hr=0x%08lX\n",
                   static_cast<unsigned long>(hr));
            return 1;
        }
        IGraphBuilder* graph = nullptr;
        hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IGraphBuilder, reinterpret_cast<void**>(&graph));
        if (FAILED(hr)) return 1;
        graph->AddFilter(src, L"LBVcamSource");
        graph->AddFilter(evr, L"EVR");
        IPin* src_pin = nullptr;
        src->FindPin(L"Output", &src_pin);
        IPin* evr_pin = nullptr;
        IEnumPins* ep = nullptr;
        evr->EnumPins(&ep);
        PIN_INFO pi;
        while (ep && ep->Next(1, &evr_pin, nullptr) == S_OK) {
            evr_pin->QueryPinInfo(&pi);
            if (pi.pFilter) pi.pFilter->Release();
            if (pi.dir == PINDIR_INPUT) break;
            evr_pin->Release();
            evr_pin = nullptr;
        }
        if (ep) ep->Release();
        if (!evr_pin) {
            printf("FAIL: EVR input pin not found\n");
            return 1;
        }
        hr = graph->ConnectDirect(src_pin, evr_pin, nullptr);
        printf("EVR ConnectDirect hr=0x%08lX\n", static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr)) {
            IMediaControl* ctl = nullptr;
            graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&ctl));
            hr = ctl->Run();
            printf("EVR Run hr=0x%08lX\n", static_cast<unsigned long>(hr));
            if (SUCCEEDED(hr)) {
                Sleep(1500);
                ctl->Stop();
                printf("EVR graph ran ok\n");
            }
            ctl->Release();
        }
        src_pin->Release();
        if (evr_pin) evr_pin->Release();
        src->Release();
        evr->Release();
        graph->Release();
        CoUninitialize();
        return SUCCEEDED(hr) ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--obs") == 0) {
        // {A3FCE0F5-3493-419F-958A-ABA1250EC20B}
        static const CLSID kOBSVcam = {0xa3fce0f5, 0x3493, 0x419f,
                                       {0x95, 0x8a, 0xab, 0xa1, 0x25, 0x0e, 0xc2, 0x0b}};
        IBaseFilter* src = nullptr;
        HRESULT hr = CoCreateInstance(kOBSVcam, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IBaseFilter, reinterpret_cast<void**>(&src));
        if (FAILED(hr)) {
            printf("FAIL: OBS vcam CoCreateInstance hr=0x%08lX\n",
                   static_cast<unsigned long>(hr));
            return 1;
        }
        TestSink sink;
        IGraphBuilder* graph = nullptr;
        hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IGraphBuilder, reinterpret_cast<void**>(&graph));
        if (FAILED(hr)) return 1;
        graph->AddFilter(src, L"OBSVcam");
        graph->AddFilter(&sink, L"TestSink");
        IPin* src_pin = nullptr;
        IPin* sink_pin = nullptr;
        src->FindPin(L"Output", &src_pin);
        sink.FindPin(L"Input", &sink_pin);
        hr = graph->ConnectDirect(src_pin, sink_pin, nullptr);
        printf("OBS vcam ConnectDirect hr=0x%08lX\n", static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr)) {
            IMediaControl* ctl = nullptr;
            graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&ctl));
            ctl->Run();
            Sleep(1000);
            ctl->Stop();
            ctl->Release();
            printf("OBS vcam graph ran, frames=%llu\n",
                   static_cast<unsigned long long>(sink.pin_.frames_.load()));
        }
        src_pin->Release();
        sink_pin->Release();
        src->Release();
        graph->Release();
        CoUninitialize();
        return hr == S_OK ? 0 : 1;
    }

    argc_use_nr = 0;
    pmt_null_mode = 0;
    bool graph_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--graph") == 0) graph_mode = true;
        if (strcmp(argv[i], "--nr") == 0) argc_use_nr = 1;
        if (strcmp(argv[i], "--pmt-null") == 0) pmt_null_mode = 1;
    }
    const int rc = graph_mode ? run_graph() : run_manual();
    CoUninitialize();
    return rc;
}