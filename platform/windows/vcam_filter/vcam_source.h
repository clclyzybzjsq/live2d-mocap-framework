// lb_vcam_filter —— DirectShow push-source filter（手工实现，无 strmbase）
#pragma once

#include <windows.h>
#include <dshow.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "lb/core/vcam_protocol.h"

namespace lb::vcam {

class LBVcamPushSource;

// 输出 pin：实现 IPin（source 侧），内部持有下游 IMemInputPin/IMemAllocator。
class LBVcamOutputPin final : public IPin {
public:
    explicit LBVcamOutputPin(LBVcamPushSource* owner);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IPin
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP Disconnect();
    STDMETHODIMP ConnectedTo(IPin** ppPin);
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt);
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo);
    STDMETHODIMP Notify(IBaseFilter* pSelf, PIN_DIRECTION dir);
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pDir);
    STDMETHODIMP QueryId(LPWSTR* id);
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum);
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG*);
    STDMETHODIMP EndOfStream();
    STDMETHODIMP BeginFlush();
    STDMETHODIMP EndFlush();
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double);

    // filter 内部调用
    bool is_connected() const;
    IMemInputPin* mem_input() const;
    IMemAllocator* allocator() const;
    void set_allocator(IMemAllocator* a);
    void acquire_shm();
    void release_shm();
    bool has_shm() const;
    lb::core::SharedHeaderVcam* shm_header() const;
    const uint8_t* shm_frame() const;
    void on_connected();

private:
    LBVcamPushSource* owner_;
    std::atomic<ULONG> refs_{1};
    std::mutex lock_;
    IPin* peer_ = nullptr;
    IMemInputPin* mem_in_ = nullptr;
    IMemAllocator* alloc_ = nullptr;
    AM_MEDIA_TYPE connected_mt_{};
    bool connected_ = false;
    bool own_alloc_ = false;

    struct Shm {
        HANDLE map = nullptr;
        void* view = nullptr;
    } shm_;
};

// Push source：IBaseFilter + 内部推流线程。
class LBVcamPushSource final : public IBaseFilter {
public:
    LBVcamPushSource();
    ~LBVcamPushSource();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClsID);

    // IMediaFilter
    STDMETHODIMP Stop();
    STDMETHODIMP Pause();
    STDMETHODIMP Run(REFERENCE_TIME tStart);
    STDMETHODIMP GetState(DWORD dwMSecs, FILTER_STATE* pState);
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock);
    STDMETHODIMP GetSyncSource(IReferenceClock** ppClock);
    STDMETHODIMP SetStopTime(const REFERENCE_TIME* pT);
    STDMETHODIMP GetStopTime(REFERENCE_TIME* pT);
    STDMETHODIMP SetStartTime(const REFERENCE_TIME* pT);
    STDMETHODIMP GetStartTime(REFERENCE_TIME* pT);

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins** ppEnum);
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin);
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo);
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName);
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo);

    LBVcamOutputPin* output_pin() const { return pin_; }

private:
    void push_thread();
    void fill_frame(uint8_t* dst, size_t dst_pitch, int w, int h,
                    REFERENCE_TIME start, REFERENCE_TIME end);
    void fill_bars(uint8_t* dst, size_t dst_pitch, int w, int h);

    std::atomic<ULONG> refs_{1};
    LBVcamOutputPin* pin_;
    std::mutex state_lock_;
    FILTER_STATE state_ = State_Stopped;
    IFilterGraph* graph_ = nullptr;
    IReferenceClock* clock_ = nullptr;

    std::thread thread_;
    std::atomic<bool> run_thread_{false};
    REFERENCE_TIME frame_duration_{333333}; // 30fps (100ns units)
    REFERENCE_TIME next_ts_{0};
};

// 媒体类型工具
void make_video_type(AM_MEDIA_TYPE* mt, int w, int h);
void free_media_type(AM_MEDIA_TYPE* mt);

} // namespace lb::vcam