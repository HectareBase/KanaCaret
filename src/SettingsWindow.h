#pragma once
#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

#include "Renderer.h"
#include "Settings.h"
#include "Theme.h"

// 設定画面。開いている間だけウィンドウを作り、閉じたら破棄する(常駐中のメモリを増やさない)。
// 部品は標準のコントロール(キーボード操作と読み上げソフトへの対応が付いてくる)で、アプリのテーマ(ライト/ダーク)に合わせる。
// 色の見本とプレビューは Renderer で、チェックボックスとスライダーは NM_CUSTOMDRAW で Direct2D を使って描く。
// 変更はすぐ settings に書き込み、changed を呼ぶ(保存と反映は呼び出し側)。
class SettingsWindow {
public:
    void Open(HINSTANCE instance, Settings* settings, Renderer* renderer, std::function<void()> changed);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }
    // Tab キーでの移動などを処理したら true を返す(メッセージループから呼ぶ)
    bool PreTranslate(MSG* msg);
    void SetTheme(const Theme& theme);
    // 設定が画面の外で変わったときに、表示を合わせる
    void Refresh();

private:
    struct Item {
        HWND hwnd;
        int page;  // -1 はどのページでも表示する
        int x, y, w, h;  // DIP
        bool heading;
    };
    // 画面の配色(テーマから決める)
    struct Palette {
        bool dark = false;
        COLORREF bg, text, subtle, navSelected, controlBg, stroke, track, accent, onAccent;
    };

    HWND Add(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h,
             bool heading = false);
    void CreateControls();
    void CreateFonts();
    void ApplyPalette();
    void Layout();
    void ShowPage(int page);
    void OnCommand(int id, int code, HWND ctl);
    void OnSlider(HWND ctl);
    LRESULT OnCustomDraw(NMCUSTOMDRAW* cd);
    LRESULT DrawAppListItem(NMLVCUSTOMDRAW* cd);
    void OnAppChecked(const NMLISTVIEW* nm);
    void DrawCheckBox(NMCUSTOMDRAW* cd);
    void DrawSliderPart(NMCUSTOMDRAW* cd);
    void OnDrawItem(const DRAWITEMSTRUCT* dis);
    void DrawNavItem(const DRAWITEMSTRUCT* dis);
    void DrawComboItem(const DRAWITEMSTRUCT* dis);
    void PaintCombo(HWND combo);
    static LRESULT CALLBACK ComboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR self);
    int ComboItemHeight(bool field) const;
    void DrawSwatch(const DRAWITEMSTRUCT* dis, bool kana, int index);
    void DrawPreview(HDC dc, RECT rc);
    void DrawBadgeAt(HDC dc, int x, int y, int box, bool native, BadgeColors colors, int opacity, int shadow);
    // Direct2D で dc の rc の範囲に描く(座標は dc のまま使える)
    bool BeginD2D(HDC dc, const RECT& rc);
    void EndD2D();
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> Brush(COLORREF color);
    void PickSwatch(bool kana, int index);
    void Changed();
    // 「アプリ」のページの一覧を、実行中のアプリと対象に挙げたアプリで作り直す
    void RefreshAppList();
    void UpdateAppHint();
    int S(int dip) const { return MulDiv(dip, dpi_, 96); }
    float Sf(float dip) const { return dip * dpi_ / 96.0f; }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Settings* settings_ = nullptr;
    Renderer* renderer_ = nullptr;
    std::function<void()> changed_;
    Theme theme_;
    Palette palette_{};
    HBRUSH bgBrush_ = nullptr, controlBrush_ = nullptr;
    std::vector<std::wstring> appRows_;  // 「アプリ」の一覧の各行の exe 名
    bool fillingApps_ = false;           // 一覧を作っている間は、チェックの変化を設定に反映しない
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT headingFont_ = nullptr;
    HWND tooltip_ = nullptr;
    std::vector<Item> items_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    int page_ = 0;  // 閉じても覚えておき、次に開いたときに同じページを出す
};
