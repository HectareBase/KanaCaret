#include "DarkMode.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace {

enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };

using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using RefreshImmersiveColorPolicyStateFn = void(WINAPI*)();
using FlushMenuThemesFn = void(WINAPI*)();

struct Api {
    SetPreferredAppModeFn setPreferredAppMode = nullptr;              // 序数 135
    AllowDarkModeForWindowFn allowDarkModeForWindow = nullptr;        // 序数 133
    RefreshImmersiveColorPolicyStateFn refreshColorPolicy = nullptr;  // 序数 104
    FlushMenuThemesFn flushMenuThemes = nullptr;                      // 序数 136
};

DWORD BuildNumber() {
    // GetVersionEx は manifest によって値が変わるので、ntdll の RtlGetVersion で読む
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
    OSVERSIONINFOW vi{sizeof(vi)};
    return rtlGetVersion && rtlGetVersion(&vi) == 0 ? vi.dwBuildNumber : 0;
}

const Api& GetApi() {
    static Api api = [] {
        Api a;
        // 序数の意味はビルドによって違うので、意味が分かっているビルドだけで取り出す
        if (BuildNumber() < 18362) return a;
        HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!ux) return a;
        auto get = [ux](int ordinal) { return reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(ordinal))); };
        a.setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(get(135));
        a.allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(get(133));
        a.refreshColorPolicy = reinterpret_cast<RefreshImmersiveColorPolicyStateFn>(get(104));
        a.flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(get(136));
        if (!a.setPreferredAppMode || !a.allowDarkModeForWindow || !a.refreshColorPolicy || !a.flushMenuThemes)
            a = {};
        return a;
    }();
    return api;
}

}  // namespace

void InitDarkMode() {
    const Api& api = GetApi();
    if (!api.setPreferredAppMode) return;
    api.setPreferredAppMode(PreferredAppMode::AllowDark);
    RefreshDarkMode();
}

void RefreshDarkMode() {
    const Api& api = GetApi();
    if (!api.refreshColorPolicy) return;
    api.refreshColorPolicy();
    api.flushMenuThemes();
}

void SetWindowDark(HWND hwnd, bool dark) {
    BOOL value = dark;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    if (const Api& api = GetApi(); api.allowDarkModeForWindow) api.allowDarkModeForWindow(hwnd, dark);
}

void SetControlDark(HWND ctl, bool dark, const wchar_t* darkClass) {
    const Api& api = GetApi();
    if (!api.allowDarkModeForWindow) return;
    api.allowDarkModeForWindow(ctl, dark);
    SetWindowTheme(ctl, dark ? darkClass : nullptr, nullptr);
    SendMessageW(ctl, WM_THEMECHANGED, 0, 0);
}

bool DarkModeAvailable() {
    return GetApi().allowDarkModeForWindow != nullptr;
}
