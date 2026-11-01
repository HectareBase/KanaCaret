#include "IndicatorWindow.h"

#include <algorithm>

#include "CaretLocator.h"

namespace {

constexpr wchar_t kClassName[] = L"ImeIndicator.Indicator";
constexpr int kMarginDip = 6;      // 影を描く余白(Renderer の影の広がり + 下へのずれ)
constexpr int kGapDip = 4;         // キャレットとの間隔
constexpr UINT kFadeInMs = 83;     // Fluent の Fast
constexpr UINT kFadeOutMs = 167;   // Fluent の Normal
constexpr UINT_PTR kAnimTimer = 1;

}  // namespace

bool IndicatorWindow::Create(HINSTANCE instance, Renderer* renderer) {
    renderer_ = renderer;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    memDC_ = CreateCompatibleDC(nullptr);
    return memDC_ != nullptr;
}

void IndicatorWindow::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (memDC_) {
        if (oldBitmap_) SelectObject(memDC_, oldBitmap_);
        DeleteDC(memDC_);
    }
    if (dib_) DeleteObject(dib_);
    memDC_ = nullptr;
    dib_ = nullptr;
}

void IndicatorWindow::ShowAtCaret(const RECT& caret, ImeState state, Placement placement) {
    UINT dpi = MonitorDpi(caret);
    int size = MulDiv(sizeDip_, dpi, 96), gap = MulDiv(kGapDip, dpi, 96);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &mi);

    int x = caret.left;
    int above = caret.top - gap - size, below = caret.bottom + gap;
    int y = above;
    switch (placement) {
    case Placement::Above:
        break;
    case Placement::AboveRight:  // キャレットの右側で、下端をキャレットの上端にそろえる(真上の文字を隠さない)
        x = caret.right + gap;
        y = caret.top - size;
        break;
    case Placement::Below:
        y = below;
        break;
    }
    // 画面の端からはみ出すときは、上下を入れ替える
    if (y < mi.rcWork.top) y = below;
    else if (y + size > mi.rcWork.bottom) y = above;
    x = std::clamp<int>(x, mi.rcWork.left, mi.rcWork.right - size);
    Show(x, y, dpi, state);
}

void IndicatorWindow::ShowAtPoint(POINT pt, ImeState state) {
    RECT r{pt.x, pt.y, pt.x + 1, pt.y + 1};
    UINT dpi = MonitorDpi(r);
    int size = MulDiv(sizeDip_, dpi, 96);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);

    int x = pt.x - size / 2;
    int y = pt.y - size - MulDiv(8, dpi, 96);
    if (y < mi.rcWork.top) y = pt.y + MulDiv(24, dpi, 96);  // カーソルの下側へ
    x = std::clamp<int>(x, mi.rcWork.left, mi.rcWork.right - size);
    Show(x, y, dpi, state);
}

void IndicatorWindow::Hide() {
    if (!shown_) return;
    shown_ = false;
    Animate(0, kFadeOutMs);
}

void IndicatorWindow::BringToTop() {
    if (mapped_) SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

void IndicatorWindow::SetTheme(const Theme& theme) {
    theme_ = theme;
    dirty_ = true;
    if (mapped_) Redraw();
}

void IndicatorWindow::SetStyle(int sizeDip, bool accentKana) {
    if (sizeDip == sizeDip_ && accentKana == accentKana_) return;
    sizeDip_ = sizeDip;
    accentKana_ = accentKana;
    dirty_ = true;
    // 大きさが変わると位置も変わるので、次の表示で置き直す
    if (mapped_) {
        ShowWindow(hwnd_, SW_HIDE);
        mapped_ = shown_ = false;
        KillTimer(hwnd_, kAnimTimer);
    }
}

void IndicatorWindow::Show(int boxX, int boxY, UINT dpi, ImeState state) {
    if (dpi != dpi_ || state != state_ || dirty_) {
        dpi_ = dpi;
        state_ = state;
        box_ = MulDiv(sizeDip_, dpi, 96);
        margin_ = MulDiv(kMarginDip, dpi, 96);
        Redraw();
    }

    POINT pos{boxX - margin_, boxY - margin_};
    if (!mapped_) {
        pos_ = pos;
        alpha_ = 0;
        SetAlpha(0);
        SetWindowPos(hwnd_, HWND_TOPMOST, pos.x, pos.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_SHOWWINDOW);
        mapped_ = true;
    } else if (pos.x != pos_.x || pos.y != pos_.y) {
        pos_ = pos;
        SetWindowPos(hwnd_, HWND_TOPMOST, pos.x, pos.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
    }
    if (!shown_) {
        shown_ = true;
        Animate(255, kFadeInMs);
    }
}

void IndicatorWindow::Redraw() {
    int size = box_ + margin_ * 2;
    if (size != bitmapSize_) {
        if (oldBitmap_) SelectObject(memDC_, oldBitmap_);
        if (dib_) DeleteObject(dib_);
        void* bits = nullptr;
        dib_ = CreateDib(size, size, &bits);
        oldBitmap_ = SelectObject(memDC_, dib_);
        bitmapSize_ = size;
    }
    BadgeColors colors = ColorsFor(theme_, theme_.dark, accentKana_ && state_ == ImeState::Native);
    if (!renderer_->DrawBadge(memDC_, size, size, margin_, box_, dpi_, state_, colors)) return;
    dirty_ = false;

    POINT src{0, 0};
    SIZE sz{size, size};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha_, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &sz, memDC_, &src, 0, &blend, ULW_ALPHA);
}

void IndicatorWindow::SetAlpha(BYTE alpha) {
    alpha_ = alpha;
    // 画像はそのままで、全体の不透明度だけを変える
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
}

void IndicatorWindow::Animate(BYTE target, UINT durationMs) {
    if (!theme_.animations) {  // 「アニメーション効果」がオフなら即座に切り替える
        animTo_ = target;
        animDuration_ = 0;
        OnAnimationTimer();
        return;
    }
    animFrom_ = alpha_;
    animTo_ = target;
    animStart_ = GetTickCount64();
    // 途中から反転したときは、残りの距離に応じて短くする
    animDuration_ = std::max<UINT>(1, durationMs * (UINT)std::abs(target - alpha_) / 255);
    SetTimer(hwnd_, kAnimTimer, USER_TIMER_MINIMUM, nullptr);
}

void IndicatorWindow::OnAnimationTimer() {
    float t = animDuration_ ? std::min(1.0f, (GetTickCount64() - animStart_) / (float)animDuration_) : 1.0f;
    float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // ease-out(3 次)
    SetAlpha((BYTE)(animFrom_ + (animTo_ - animFrom_) * eased + 0.5f));
    if (t < 1.0f) return;

    KillTimer(hwnd_, kAnimTimer);
    SetAlpha(animTo_);
    if (animTo_ == 0) {
        ShowWindow(hwnd_, SW_HIDE);
        mapped_ = false;
    }
}

LRESULT CALLBACK IndicatorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<IndicatorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_TIMER:
        if (self && wp == kAnimTimer) self->OnAnimationTimer();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
