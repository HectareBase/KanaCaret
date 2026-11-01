#include "Theme.h"

#include <cmath>

namespace {

constexpr wchar_t kPersonalize[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAccent[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";

bool ReadDword(const wchar_t* key, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

D2D1_COLOR_F Rgb(BYTE r, BYTE g, BYTE b, float a = 1.0f) { return {r / 255.0f, g / 255.0f, b / 255.0f, a}; }

D2D1_COLOR_F FromColorRef(COLORREF c) { return Rgb(GetRValue(c), GetGValue(c), GetBValue(c)); }

// WCAG の相対輝度
float RelativeLuminance(const D2D1_COLOR_F& c) {
    auto linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b);
}

D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
}

}  // namespace

Theme LoadTheme() {
    Theme t;
    DWORD light = 1;
    if (ReadDword(kPersonalize, L"AppsUseLightTheme", light)) t.dark = light == 0;
    light = 1;
    if (ReadDword(kPersonalize, L"SystemUsesLightTheme", light)) t.systemDark = light == 0;

    // AccentPalette は 8 色 × RGBA。0..2 = Light3..Light1、3 = 基本色、4..6 = Dark1..Dark3
    BYTE palette[32];
    DWORD size = sizeof(palette);
    if (RegGetValueW(HKEY_CURRENT_USER, kAccent, L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size) ==
            ERROR_SUCCESS &&
        size >= 28) {
        t.accentLight2 = Rgb(palette[4], palette[5], palette[6]);
        t.accentDark1 = Rgb(palette[16], palette[17], palette[18]);
    } else if (DWORD menu = 0; ReadDword(kAccent, L"AccentColorMenu", menu)) {  // 0xAABBGGRR
        t.accentLight2 = t.accentDark1 = FromColorRef(menu & 0x00FFFFFF);
    }

    HIGHCONTRASTW hc{sizeof(hc)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) t.highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    BOOL anim = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0)) t.animations = anim != FALSE;
    return t;
}

BadgeColors ColorsFor(const Theme& theme, bool dark, bool native, const FillChoice& fill) {
    if (theme.highContrast) {
        D2D1_COLOR_F text = FromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        if (native) return {FromColorRef(GetSysColor(COLOR_HIGHLIGHT)), text, FromColorRef(GetSysColor(COLOR_HIGHLIGHTTEXT)), 0};
        return {FromColorRef(GetSysColor(COLOR_WINDOW)), text, text, 0};
    }
    const float shadow = dark ? 0.30f : 0.16f;
    if (fill.kind == FillChoice::Custom) {
        // 文字は背景との差が大きいほう(白か黒)にし、枠は背景を文字色へ少し寄せて輪郭を出す
        D2D1_COLOR_F f = FromColorRef(fill.color);
        bool light = RelativeLuminance(f) > 0.18f;  // 白・黒とのコントラスト比が等しくなる明るさ
        D2D1_COLOR_F text = light ? Rgb(0, 0, 0) : Rgb(255, 255, 255);
        return {f, Mix(f, text, 0.25f), text, shadow};
    }
    if (native && fill.kind == FillChoice::Auto) {
        // Fluent の AccentFillColorDefault / TextOnAccentFillColorPrimary に合わせる
        D2D1_COLOR_F f = dark ? theme.accentLight2 : theme.accentDark1;
        return {f, f, dark ? Rgb(0, 0, 0) : Rgb(255, 255, 255), shadow};
    }
    // 「a」は同系色の背景に埋もれないよう、ControlStrongStroke 相当の濃い枠を付ける
    if (dark) return {Rgb(58, 58, 58), Rgb(154, 154, 154), Rgb(255, 255, 255), shadow};
    return {Rgb(249, 249, 249), Rgb(138, 138, 138), Rgb(27, 27, 27), shadow};
}
