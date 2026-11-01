#pragma once
#include <windows.h>

enum class ImeState {
    Unknown,  // 取得できない(管理者権限のウィンドウ、IME ウィンドウが無い など)
    Direct,   // 直接入力 → 「a」
    Native,   // 日本語入力 → 「あ」
};

// フォーカスのあるウィンドウの IME 状態をプロセス外から問い合わせる。
ImeState QueryImeState(HWND focus);
