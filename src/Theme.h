#pragma once
#include <windows.h>
#include <d2d1.h>

// Windows の個人用設定から読み取った配色。
struct Theme {
    bool dark = false;          // アプリのテーマ(インジケーターに使う)
    bool highContrast = false;
    bool animations = true;     // 「アニメーション効果」がオン
    D2D1_COLOR_F accentLight2{0.6f, 0.87f, 1.0f, 1.0f};  // ダークテーマでの塗り(AccentFillColorDefault)
    D2D1_COLOR_F accentDark1{0.0f, 0.37f, 0.72f, 1.0f};  // ライトテーマでの塗り
};

Theme LoadTheme();

// バッジの塗りの指定。Auto はテーマに合わせた既定の配色(「あ」はアクセントカラー、「a」は無彩色)、
// Neutral は「a」と同じ無彩色、Custom は利用者が選んだ色
struct FillChoice {
    enum Kind { Auto, Neutral, Custom } kind = Auto;
    COLORREF color = 0;
    bool operator==(const FillChoice&) const = default;
};

struct BadgeColors {
    D2D1_COLOR_F fill, border, text;
    float shadowAlpha;  // 0 なら影を描かない
};

// dark: 描く場所のテーマ、native: 「あ」を描くか、fill: 塗りの指定(ハイコントラストでは無視する)
BadgeColors ColorsFor(const Theme& theme, bool dark, bool native, const FillChoice& fill = {});
