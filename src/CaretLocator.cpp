#include "CaretLocator.h"

#include <oleacc.h>
#include <shellscalingapi.h>

namespace {

bool ReadGuiThread(HWND hwnd, FocusInfo& info) {
    GUITHREADINFO gti{sizeof(gti)};
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(hwnd, nullptr), &gti)) return false;
    info.focus = gti.hwndFocus;
    info.caretWnd = gti.hwndCaret;
    info.caret = gti.rcCaret;
    return true;
}

bool OnScreen(const RECT& r) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN), bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r.left >= left && r.top >= top && r.left < right && r.bottom <= bottom;
}

// 文書全体や選択範囲ではなく、1 行分のキャレットらしい大きさか
bool CaretLike(const RECT& r) {
    LONG h = r.bottom - r.top, w = r.right - r.left;
    UINT dpi = MonitorDpi(r);
    return h >= MulDiv(6, dpi, 96) && h <= MulDiv(120, dpi, 96) && w <= h;
}

bool CaretWin32(const FocusInfo& info, RECT& out) {
    if (!info.caretWnd) return false;
    RECT r = info.caret;
    if (r.bottom - r.top <= 0) return false;
    MapWindowPoints(info.caretWnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    out = r;
    return true;
}

bool CaretMsaa(HWND focus, RECT& out) {
    IAccessible* acc = nullptr;
    if (FAILED(AccessibleObjectFromWindow(focus, (DWORD)OBJID_CARET, IID_IAccessible, (void**)&acc)) || !acc)
        return false;
    VARIANT self;
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    long x = 0, y = 0, w = 0, h = 0;
    HRESULT hr = acc->accLocation(&x, &y, &w, &h, self);
    acc->Release();
    // Chromium は前にフォーカスしていた入力欄の古い位置を幅 0 で返すことがある
    if (FAILED(hr) || h <= 0 || w <= 0 || (x == 0 && y == 0)) return false;
    out = {x, y, x + w, y + h};
    return true;
}

}  // namespace

bool PlausibleCaret(const RECT& r) { return CaretLike(r) && OnScreen(r); }

UINT MonitorDpi(const RECT& r) {
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
    return dx;
}

bool GetFocusInfo(FocusInfo& info) {
    info = {};
    info.foreground = GetForegroundWindow();
    if (!info.foreground) return false;
    ReadGuiThread(info.foreground, info);
    if (!info.focus) {
        // UWP: 入力を受けるのは ApplicationFrameWindow の子の CoreWindow(別プロセス)
        if (HWND core = FindWindowExW(info.foreground, nullptr, L"Windows.UI.Core.CoreWindow", nullptr)) {
            if (!ReadGuiThread(core, info) || !info.focus) info.focus = core;
        }
    }
    if (!info.focus) info.focus = info.foreground;
    GetWindowThreadProcessId(info.focus, &info.pid);
    return true;
}

CaretSource LocateCaret(const FocusInfo& info, RECT& out, CaretSource known) {
    if (known != CaretSource::Msaa && CaretWin32(info, out) && PlausibleCaret(out)) return CaretSource::Win32;
    if (known != CaretSource::Win32 && CaretMsaa(info.focus, out) && PlausibleCaret(out)) return CaretSource::Msaa;
    return CaretSource::None;
}
