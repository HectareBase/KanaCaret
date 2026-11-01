#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "Renderer.h"
#include "Settings.h"
#include "Theme.h"

// 設定画面。開いている間だけウィンドウを作り、閉じたら破棄する(常駐中のメモリを増やさない)。
// 部品は標準のコントロール(キーボード操作と読み上げソフトへの対応が付いてくる)で、
// 色の見本とプレビューだけを Renderer で描く。変更はすぐ settings に書き込み、changed を呼ぶ(保存と反映は呼び出し側)。
class SettingsWindow {
public:
    void Open(HINSTANCE instance, Settings* settings, Renderer* renderer, std::function<void()> changed);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }
    // Tab キーでの移動などを処理したら true を返す(メッセージループから呼ぶ)
    bool PreTranslate(MSG* msg);
    void SetTheme(const Theme& theme);
    // 「アプリ」のページで追加の候補に出す、最後に使った他アプリの exe 名
    void SetRecentApp(const std::wstring& exe);
    // 設定が画面の外(通知領域のメニュー)で変わったときに、表示を合わせる
    void Refresh();

private:
    struct Item {
        HWND hwnd;
        int page;  // -1 はどのページでも表示する
        int x, y, w, h;  // DIP
        bool heading;
    };

    HWND Add(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h,
             bool heading = false);
    void CreateControls();
    void CreateFonts();
    void Layout();
    void ShowPage(int page);
    void OnCommand(int id, int code, HWND ctl);
    void OnDrawItem(const DRAWITEMSTRUCT* dis);
    void DrawNavItem(const DRAWITEMSTRUCT* dis);
    void DrawSwatch(const DRAWITEMSTRUCT* dis, bool kana, int index);
    void DrawPreview(HDC dc, RECT rc);
    void DrawBadgeAt(HDC dc, int x, int y, int box, bool native, const BadgeColors& colors);
    void PickSwatch(bool kana, int index);
    void Changed();
    void RefreshAppList();
    int S(int dip) const { return MulDiv(dip, dpi_, 96); }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Settings* settings_ = nullptr;
    Renderer* renderer_ = nullptr;
    std::function<void()> changed_;
    Theme theme_;
    std::wstring recentApp_;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT headingFont_ = nullptr;
    HWND tooltip_ = nullptr;
    std::vector<Item> items_;
    int page_ = 0;  // 閉じても覚えておき、次に開いたときに同じページを出す
};
