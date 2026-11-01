#pragma once
#include <windows.h>

#include "ImeMonitor.h"
#include "Renderer.h"
#include "Settings.h"
#include "Theme.h"

// キャレットの上に「あ」「a」を出す、クリックを透過する小さな最前面ウィンドウ。
// UpdateLayeredWindow で、角丸・影・半透明を含む画像をそのまま表示する。
// 画像は状態・テーマ・DPI が変わったときだけ描き直し、移動では位置だけを変える。
class IndicatorWindow {
public:
    bool Create(HINSTANCE instance, Renderer* renderer);
    void Destroy();

    void ShowAtCaret(const RECT& caret, ImeState state, Placement placement);
    void ShowAtPoint(POINT pt, ImeState state);
    void Hide();
    void BringToTop();
    void SetTheme(const Theme& theme);
    void SetStyle(int sizeDip, bool accentKana);

private:
    void Show(int boxX, int boxY, UINT dpi, ImeState state);
    void Redraw();
    void SetAlpha(BYTE alpha);
    void Animate(BYTE target, UINT durationMs);
    void OnAnimationTimer();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    Renderer* renderer_ = nullptr;
    Theme theme_;
    int sizeDip_ = 22;
    bool accentKana_ = true;

    // 描画済みの画像
    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    int bitmapSize_ = 0;
    bool dirty_ = true;

    ImeState state_ = ImeState::Unknown;
    UINT dpi_ = 0;
    int box_ = 0;     // 箱の大きさ(物理ピクセル)
    int margin_ = 0;  // 影のための余白
    POINT pos_{};     // ウィンドウの左上(余白を含む)

    bool shown_ = false;   // 表示する状態か(フェードアウト中は false)
    bool mapped_ = false;  // ウィンドウが実際に表示されているか
    BYTE alpha_ = 0;

    // フェード
    BYTE animFrom_ = 0, animTo_ = 0;
    ULONGLONG animStart_ = 0;
    UINT animDuration_ = 0;
};
