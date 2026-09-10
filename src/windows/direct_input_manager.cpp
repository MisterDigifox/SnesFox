#include "direct_input_manager.hpp"

#ifdef _WIN32

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <wbemidl.h>
#include <oleauto.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>

namespace DirectInputManager {

namespace {

struct Device {
    LPDIRECTINPUTDEVICE8 device = nullptr;
    GUID instanceGuid{};
    PadState state;
};

LPDIRECTINPUT8 g_directInput = nullptr;
HWND g_hWnd = nullptr;
std::vector<Device> g_devices;

// GUIDs already classified (as XInput or genuine DirectInput) so a repeat enumeration never
// re-runs the WMI query below for a device it's already seen — that query walks every PNP
// device on the system and is far too slow to redo every rescan.
std::vector<GUID> g_processedGuids;

bool sameGuid(const GUID& a, const GUID& b) {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           memcmp(a.Data4, b.Data4, sizeof(a.Data4)) == 0;
}

bool alreadyProcessed(const GUID& guid) {
    for (const GUID& g : g_processedGuids) {
        if (sameGuid(g, guid)) return true;
    }
    return false;
}

// Enumerates every PNP device via WMI and checks whether its hardware ID contains "IG_" (e.g.
// "VID_045E&PID_028E&IG_00") — the marker Windows gives XInput-class devices. DirectInput alone
// can't tell an Xbox pad apart from anything else, so this is the only reliable way to keep the
// same physical Xbox controller from being counted once via XInputManager and a second time
// here. Ported near-verbatim from Mesen2's Windows/DirectInputManager.cpp (same technique
// Microsoft's own DirectInput sample code for this problem uses).
bool isXInputDevice(const GUID* productGuid) {
    IWbemLocator* wbemLocator = nullptr;
    IEnumWbemClassObject* enumDevices = nullptr;
    IWbemClassObject* devices[20] = {};
    IWbemServices* wbemServices = nullptr;
    BSTR namespaceStr = nullptr;
    BSTR deviceIdProp = nullptr;
    BSTR className = nullptr;
    DWORD returned = 0;
    bool isXInput = false;

    HRESULT hr = CoInitialize(nullptr);
    const bool cleanupCom = SUCCEEDED(hr);

    hr = CoCreateInstance(__uuidof(WbemLocator), nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IWbemLocator), reinterpret_cast<LPVOID*>(&wbemLocator));
    if (FAILED(hr) || !wbemLocator) goto cleanup;

    namespaceStr = SysAllocString(L"\\\\.\\root\\cimv2");
    className = SysAllocString(L"Win32_PNPEntity");
    deviceIdProp = SysAllocString(L"DeviceID");

    hr = wbemLocator->ConnectServer(namespaceStr, nullptr, nullptr, 0L, 0L, nullptr, nullptr,
                                     &wbemServices);
    if (FAILED(hr) || !wbemServices) goto cleanup;

    CoSetProxyBlanket(wbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                       RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    hr = wbemServices->CreateInstanceEnum(className, 0, nullptr, &enumDevices);
    if (FAILED(hr) || !enumDevices) goto cleanup;

    for (;;) {
        hr = enumDevices->Next(10000, 20, devices, &returned);
        if (FAILED(hr) || returned == 0 || isXInput) break;

        for (DWORD i = 0; i < returned; i++) {
            VARIANT var;
            VariantInit(&var);
            hr = devices[i]->Get(deviceIdProp, 0L, &var, nullptr, nullptr);
            if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal &&
                wcsstr(var.bstrVal, L"IG_")) {
                DWORD vid = 0, pid = 0;
                if (wchar_t* strVid = wcsstr(var.bstrVal, L"VID_")) {
                    if (swscanf(strVid, L"VID_%4X", &vid) != 1) vid = 0;
                }
                if (wchar_t* strPid = wcsstr(var.bstrVal, L"PID_")) {
                    if (swscanf(strPid, L"PID_%4X", &pid) != 1) pid = 0;
                }
                if (MAKELONG(vid, pid) == static_cast<LONG>(productGuid->Data1)) {
                    isXInput = true;
                    VariantClear(&var);
                    devices[i]->Release();
                    devices[i] = nullptr;
                    break;
                }
            }
            VariantClear(&var);
            if (devices[i]) {
                devices[i]->Release();
                devices[i] = nullptr;
            }
        }
    }

cleanup:
    if (namespaceStr) SysFreeString(namespaceStr);
    if (deviceIdProp) SysFreeString(deviceIdProp);
    if (className) SysFreeString(className);
    for (IWbemClassObject* d : devices) {
        if (d) d->Release();
    }
    if (enumDevices) enumDevices->Release();
    if (wbemLocator) wbemLocator->Release();
    if (wbemServices) wbemServices->Release();
    if (cleanupCom) CoUninitialize();
    return isXInput;
}

int __stdcall enumAxesCallback(const DIDEVICEOBJECTINSTANCE* obj, void* context) {
    if (obj->dwType & DIDFT_AXIS) {
        auto* device = static_cast<LPDIRECTINPUTDEVICE8>(context);
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(DIPROPRANGE);
        range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        range.diph.dwHow = DIPH_BYID;
        range.diph.dwObj = obj->dwType;
        range.lMin = INT16_MIN;
        range.lMax = INT16_MAX;
        device->SetProperty(DIPROP_RANGE, &range.diph);
    }
    return DIENUM_CONTINUE;
}

int __stdcall enumJoysticksCallback(const DIDEVICEINSTANCE* instance, void* context) {
    if (alreadyProcessed(instance->guidInstance)) return DIENUM_CONTINUE;
    g_processedGuids.push_back(instance->guidInstance);

    if (isXInputDevice(&instance->guidProduct)) return DIENUM_CONTINUE; // XInputManager owns this one

    LPDIRECTINPUTDEVICE8 device = nullptr;
    if (FAILED(g_directInput->CreateDevice(instance->guidInstance, &device, nullptr))) {
        return DIENUM_CONTINUE;
    }
    if (FAILED(device->SetDataFormat(&c_dfDIJoystick2)) ||
        FAILED(device->SetCooperativeLevel(g_hWnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) {
        device->Release();
        return DIENUM_CONTINUE;
    }
    device->EnumObjects(enumAxesCallback, device, DIDFT_ALL);

    Device entry;
    entry.device = device;
    entry.instanceGuid = instance->guidInstance;
    g_devices.push_back(entry);
    return DIENUM_CONTINUE;
}

void enumerateDevices() {
    if (!g_directInput) return;
    g_directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumJoysticksCallback, nullptr,
                                DIEDFL_ALLDEVICES);
}

// Re-enumerating walks every attached game controller (and, for any brand-new one, the WMI scan
// above) — worth doing to support hot-plugging generic pads mid-session, but not worth doing
// every single emulated frame. Same throttling rationale as XInputManager's disconnected-slot
// rescan.
constexpr int kRescanEveryNCalls = 120;
int g_callsSinceRescan = 0;

} // namespace

void initialize(HWND hWnd) {
    if (g_directInput || !hWnd) return;
    g_hWnd = hWnd;
    if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
                                   reinterpret_cast<void**>(&g_directInput), nullptr))) {
        g_directInput = nullptr;
        return;
    }
    enumerateDevices();
}

void refreshAll() {
    if (!g_directInput) return;
    if ((g_callsSinceRescan++ % kRescanEveryNCalls) == 0) {
        enumerateDevices();
    }

    for (Device& entry : g_devices) {
        HRESULT hr = entry.device->Poll();
        if (FAILED(hr)) {
            hr = entry.device->Acquire();
            while (hr == DIERR_INPUTLOST) {
                hr = entry.device->Acquire();
            }
        }

        DIJOYSTATE2 raw{};
        if (FAILED(hr) || FAILED(entry.device->GetDeviceState(sizeof(DIJOYSTATE2), &raw))) {
            entry.state.connected = false;
            continue;
        }

        entry.state.connected = true;
        entry.state.x = raw.lX;
        entry.state.y = raw.lY;
        entry.state.pov = raw.rgdwPOV[0];
        entry.state.buttons = 0;
        for (int i = 0; i < 32; i++) {
            if (raw.rgbButtons[i]) entry.state.buttons |= (1u << i);
        }
    }
}

int deviceCount() {
    return static_cast<int>(g_devices.size());
}

const PadState& state(int deviceIndex) {
    static const PadState kDisconnected{};
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(g_devices.size())) return kDisconnected;
    return g_devices[deviceIndex].state;
}

} // namespace DirectInputManager

#endif
