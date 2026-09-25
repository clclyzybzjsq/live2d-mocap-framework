// vcam_probe —— 验证 DirectShow 设备枚举对 HKCU 注册的可见性
// 用法：vcam_probe [enum] —— 枚举视频设备类别并尝试 CoCreateInstance 各设备
#include <windows.h>
#include <dshow.h>
#include <uuids.h>
#include <cstdio>

static void hr_name_print(HRESULT hr) {
    printf("hr=0x%08lX\n", static_cast<unsigned long>(hr));
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ICreateDevEnum* dev_enum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, reinterpret_cast<void**>(&dev_enum));
    if (FAILED(hr)) { printf("CoCreateInstance devenum failed "); hr_name_print(hr); return 1; }

    IEnumMoniker* enum_mon = nullptr;
    hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_mon, 0);
    if (hr == S_FALSE) {
        printf("no video capture devices at all (CreateClassEnumerator S_FALSE)\n");
        return 2;
    }
    if (FAILED(hr)) { printf("CreateClassEnumerator failed "); hr_name_print(hr); return 1; }

    printf("video devices:\n");
    IMoniker* mon = nullptr;
    int idx = 0;
    while (enum_mon->Next(1, &mon, nullptr) == S_OK) {
        IPropertyBag* bag = nullptr;
        if (SUCCEEDED(mon->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                         reinterpret_cast<void**>(&bag)))) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(bag->Read(L"FriendlyName", &v, nullptr)) && v.vt == VT_BSTR) {
                printf("  [%d] %ls\n", idx, v.bstrVal);
            } else {
                printf("  [%d] <no name>\n", idx);
            }
            VariantClear(&v);
            bag->Release();
        } else {
            printf("  [%d] <no property bag>\n", idx);
        }
        mon->Release();
        mon = nullptr;
        ++idx;
    }
    enum_mon->Release();
    dev_enum->Release();
    CoUninitialize();
    printf("total=%d\n", idx);
    return 0;
}