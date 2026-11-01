#include "TrayIcon.h"

#include <shellapi.h>

namespace {
constexpr UINT kIconId = 1;
}

bool TrayIcon::Add(HWND owner, UINT callbackMsg) {
    owner_ = owner;
    callbackMsg_ = callbackMsg;

    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = owner;
    nid.uID = kIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = callbackMsg;
    nid.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"KanaCaret");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (added_) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
    return added_;
}

void TrayIcon::Remove() {
    if (added_) {
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd = owner_;
        nid.uID = kIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
    if (icon_) DestroyIcon(icon_);
    icon_ = nullptr;
}

void TrayIcon::SetIcon(HICON icon) {
    if (!icon) return;
    if (added_) {
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd = owner_;
        nid.uID = kIconId;
        nid.uFlags = NIF_ICON;
        nid.hIcon = icon;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    if (icon_) DestroyIcon(icon_);
    icon_ = icon;
}

bool TrayIcon::IsMenuRequest(LPARAM lp) {
    // NOTIFYICON_VERSION_4 では LOWORD(lParam) にイベントが入る
    switch (LOWORD(lp)) {
    case WM_CONTEXTMENU:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        return true;
    }
    return false;
}

UINT TrayIcon::TrackMenu(HMENU menu) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner_);  // これが無いとメニューの外をクリックしても閉じない
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner_, nullptr);
    PostMessageW(owner_, WM_NULL, 0, 0);
    return cmd;
}
