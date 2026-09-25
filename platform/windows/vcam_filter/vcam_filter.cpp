// lb_vcam_filter.dll —— LiveBridge 虚拟摄像头 DirectShow push-source filter
// MinGW 手工实现（无 strmbase），用户态注册（HKCU），零配置。
#include <windows.h>
#include <dshow.h>
#include <uuids.h>

#include "vcam_source.h"

namespace {

// {2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}
constexpr GUID CLSID_LBVcam = {0x2a17e0b2, 0x2a8c, 0x4e4a,
                               {0x9e, 0x5c, 0x8b, 0x4a, 0x6d, 0x2f, 0x1c, 0x55}};

constexpr const wchar_t* kFriendlyName = L"LiveBridge Virtual Camera";

// ---- 工厂 ----
class LBVcamClassFactory final : public IClassFactory {
public:
    LBVcamClassFactory() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
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

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        lb::vcam::LBVcamPushSource* src = new lb::vcam::LBVcamPushSource();
        HRESULT hr = src->QueryInterface(riid, ppv);
        src->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }

private:
    ULONG refs_ = 1;
};

// ---- 注册辅助 ----
// 目标：HKLM\Software\Classes（标准 DirectShow 设备注册）。
// ICreateDevEnum 只读 HKLM 视图，HKCU 注册对消费程序不可见（实测）。
bool write_reg_string(HKEY root, const wchar_t* path, const wchar_t* value,
                      const wchar_t* data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, value, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(data),
                            static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool delete_reg_tree(HKEY root, const wchar_t* path) {
    return RegDeleteTreeW(root, path) == ERROR_SUCCESS ||
           RegDeleteKeyExW(root, path, KEY_WOW64_64KEY, 0) == ERROR_SUCCESS;
}

void clsid_subkey(wchar_t* buf, size_t size, const wchar_t* suffix) {
    swprintf(buf, size, L"Software\\Classes\\CLSID\\{2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}%ls",
             suffix ? suffix : L"");
}

void category_subkey(wchar_t* buf, size_t size) {
    swprintf(buf, size, L"Software\\Classes\\MediaCategories\\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\{2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}");
}

} // namespace

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_LBVcam) return CLASS_E_CLASSNOTAVAILABLE;
    LBVcamClassFactory* factory = new LBVcamClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void) {
    wchar_t dll_path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, dll_path, MAX_PATH);

    // 标准注册目标：HKLM（需要一次管理员权限，见 scripts/register_vcam.ps1）
    const HKEY root = HKEY_LOCAL_MACHINE;
    wchar_t key[MAX_PATH] = {0};
    clsid_subkey(key, MAX_PATH, L"\\InprocServer32");
    if (!write_reg_string(root, key, nullptr, dll_path)) return E_ACCESSDENIED;
    if (!write_reg_string(root, key, L"ThreadingModel", L"Both")) return E_ACCESSDENIED;
    clsid_subkey(key, MAX_PATH, L"\\FriendlyName");
    if (!write_reg_string(root, key, nullptr, kFriendlyName)) return E_ACCESSDENIED;

    category_subkey(key, MAX_PATH);
    if (!write_reg_string(root, key, nullptr, kFriendlyName)) return E_ACCESSDENIED;

    // 冗余写 HKCU（对枚举无效，但便于其它工具查询；失败不致命）
    clsid_subkey(key, MAX_PATH, L"\\InprocServer32");
    write_reg_string(HKEY_CURRENT_USER, key, nullptr, dll_path);
    write_reg_string(HKEY_CURRENT_USER, key, L"ThreadingModel", L"Both");
    clsid_subkey(key, MAX_PATH, L"\\FriendlyName");
    write_reg_string(HKEY_CURRENT_USER, key, nullptr, kFriendlyName);
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void) {
    wchar_t key[MAX_PATH] = {0};
    clsid_subkey(key, MAX_PATH, nullptr);
    delete_reg_tree(HKEY_LOCAL_MACHINE, key);
    delete_reg_tree(HKEY_CURRENT_USER, key);
    category_subkey(key, MAX_PATH);
    delete_reg_tree(HKEY_LOCAL_MACHINE, key);
    delete_reg_tree(HKEY_CURRENT_USER, key);
    return S_OK;
}

} // extern "C"