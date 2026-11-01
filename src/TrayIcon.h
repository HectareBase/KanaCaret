#pragma once
#include <windows.h>

// 通知領域のアイコンと右クリックメニュー。
class TrayIcon {
public:
    bool Add(HWND owner, UINT callbackMsg);
    void Remove();
    // アイコンを差し替える。icon の所有権を受け取り、不要になったら破棄する
    void SetIcon(HICON icon);

    // 通知領域からのメッセージが、メニューを開く操作(右クリック・クリック・キーボード)か
    static bool IsMenuRequest(LPARAM lp);
    // メニューを表示し、選ばれたコマンドを返す(なければ 0)。menu は呼び出し側が破棄する
    UINT TrackMenu(HMENU menu);

private:
    HICON icon_ = nullptr;
    HWND owner_ = nullptr;
    UINT callbackMsg_ = 0;
    bool added_ = false;
};
