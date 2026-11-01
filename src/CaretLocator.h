#pragma once
#include <windows.h>

struct FocusInfo {
    HWND foreground = nullptr;
    HWND focus = nullptr;     // キーボード入力を受けるウィンドウ
    DWORD pid = 0;            // focus のプロセス
    HWND caretWnd = nullptr;  // Win32 のシステムキャレットを持つウィンドウ
    RECT caret{};             // caretWnd のクライアント座標
};

// 手前のアプリでフォーカスのあるウィンドウを調べる。
bool GetFocusInfo(FocusInfo& info);

enum class CaretSource { None, Win32, Msaa, Uia };

// キャレットの位置を画面座標(物理ピクセル)で返す。Win32 → MSAA の順に試し、取れた方法を返す。
// known に前回取れた方法を渡すと、それだけを試す(MSAA はプロセス間通信になるので、不要なら呼ばない)。
CaretSource LocateCaret(const FocusInfo& info, RECT& out, CaretSource known = CaretSource::None);

// 1 行分のキャレットらしい大きさで、画面内にあるか
bool PlausibleCaret(const RECT& r);

UINT MonitorDpi(const RECT& r);
