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
// 另实现两个消费方（ffmpeg/OBS/Teams 等）打开设备时依赖的接口：
//   IAMStreamConfig —— 枚举/设置分辨率与像素格式
//   IKsPropertySet  —— 查询 pin 类别（PIN_CATEGORY_CAPTURE），
//                      ffmpeg 靠它判断哪个 output pin 是采集 pin（缺失即放弃设备）
class LBVcamOutputPin final : public IPin, public IAMStreamConfig, public IKsPropertySet {
public:
    explicit LBVcamOutputPin(LBVcamPushSource* owner);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt);
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt);
    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize);
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC);

    // IKsPropertySet
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
                     DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData);
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
                     DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData,
                     DWORD* pcbReturned);
    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
                                DWORD* pTypeSupport);

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

    // IAMStreamConfig 能力查询用：当前共享内存尺寸（无写端时为彩条尺寸）
    void current_size(int* w, int* h) const;

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