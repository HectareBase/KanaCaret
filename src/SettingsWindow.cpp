#include "SettingsWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <iterator>

#include "version.h"  // CMakeLists.txt のバージョン番号から生成される

namespace {

constexpr wchar_t kClassName[] = L"HectareBase.KanaCaret.Settings";
constexpr int kClientW = 680, kClientH = 470;  // DIP
constexpr int kNavItemH = 36;
constexpr int kContentX = 196, kContentW = 460;
constexpr int kSwatchDip = 32, kSwatchGapDip = 6, kSwatchBadgeDip = 24;
constexpr int kMarginDip = 6;  // バッジの影の余白(IndicatorWindow と同じ)

enum ControlId : int {
    kIdNav = 1000,
    kIdMode, kIdPlacement, kIdMouseFallback,
    kIdSize, kIdSizeValue, kIdPreview,
    kIdAppList, kIdAppEdit, kIdAppAdd, kIdAppRemove, kIdAppRecent,
    kIdAutostart,
    kIdKanaSwatch = 1100,   // + 見本の番号
    kIdLatinSwatch = 1200,
};

const wchar_t* const kPages[] = {L"表示", L"見た目", L"アプリ", L"全般"};

// 見本の色(Windows のアクセントカラーの候補から選んだもの)
struct Preset {
    const wchar_t* name;
    COLORREF color;
};
const Preset kPresets[] = {
    {L"青", RGB(0, 120, 212)},     {L"水色", RGB(0, 153, 188)}, {L"緑", RGB(16, 124, 16)},
    {L"黄", RGB(255, 185, 0)},     {L"オレンジ", RGB(247, 99, 12)}, {L"赤", RGB(232, 17, 35)},
    {L"ピンク", RGB(227, 0, 140)}, {L"紫", RGB(135, 100, 184)},
};
constexpr int kPresetCount = (int)std::size(kPresets);

// 見本の並び: 「あ」は アクセントカラー・グレー・プリセット・その他、「a」は 自動・プリセット・その他
int FixedCount(bool kana) { return kana ? 2 : 1; }
int SwatchCount(bool kana) { return FixedCount(kana) + kPresetCount + 1; }
bool IsOther(bool kana, int index) { return index == SwatchCount(kana) - 1; }

FillChoice SwatchFill(bool kana, int index) {
    if (index == 0) return {FillChoice::Auto};
    if (kana && index == 1) return {FillChoice::Neutral};
    return {FillChoice::Custom, kPresets[index - FixedCount(kana)].color};
}

const wchar_t* SwatchName(bool kana, int index) {
    if (index == 0) return kana ? L"アクセントカラー" : L"自動(テーマに合わせる)";
    if (kana && index == 1) return L"グレー(「a」と同じ)";
    if (IsOther(kana, index)) return L"その他の色...";
    return kPresets[index - FixedCount(kana)].name;
}

int SelectedSwatch(bool kana, const FillChoice& fill) {
    for (int i = 0; i < SwatchCount(kana) - 1; ++i)
        if (SwatchFill(kana, i) == fill) return i;
    return fill.kind == FillChoice::Custom ? SwatchCount(kana) - 1 : 0;
}

// 「その他の色」の色の作成で使う 16 色(はじめはプリセットを入れておく)
COLORREF g_customColors[16] = {
    RGB(0, 120, 212), RGB(0, 153, 188), RGB(16, 124, 16), RGB(255, 185, 0),
    RGB(247, 99, 12), RGB(232, 17, 35), RGB(227, 0, 140), RGB(135, 100, 184),
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
};

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t"), e = s.find_last_not_of(L" \t");
    return b == std::wstring::npos ? L"" : s.substr(b, e - b + 1);
}

std::wstring WindowText(HWND hwnd) {
    std::wstring s(GetWindowTextLengthW(hwnd) + 1, L'\0');
    s.resize(GetWindowTextW(hwnd, s.data(), (int)s.size()));
    return s;
}

}  // namespace

void SettingsWindow::Open(HINSTANCE instance, Settings* settings, Renderer* renderer, std::function<void()> changed) {
    if (hwnd_) {
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        return;
    }
    instance_ = instance;
    settings_ = settings;
    renderer_ = renderer;
    changed_ = std::move(changed);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(instance, kClassName, &wc)) {
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), 0);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    }

    // 通知領域のアイコンがあるモニター(= マウスカーソルの位置)の中央に出す
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpi_ = dpiX;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, S(kClientW), S(kClientH)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi_);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;

    hwnd_ = CreateWindowExW(0, kClassName, L"KanaCaret の設定", style, x, y, w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) return;
    // Windows 11 ではタイトルバーを Mica にする(クライアント領域は標準のコントロールに合わせて不透明のまま)
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    CreateFonts();
    CreateControls();
    Layout();
    Refresh();
    ShowPage(page_);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool SettingsWindow::PreTranslate(MSG* msg) {
    return hwnd_ && IsDialogMessageW(hwnd_, msg);
}

void SettingsWindow::SetTheme(const Theme& theme) {
    theme_ = theme;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::SetRecentApp(const std::wstring& exe) {
    recentApp_ = exe;
    if (hwnd_) RefreshAppList();
}

HWND SettingsWindow::Add(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w,
                         int h, bool heading) {
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | style | (page < 0 ? WS_VISIBLE : 0), 0, 0, 0, 0, hwnd_,
                                (HMENU)(INT_PTR)id, instance_, nullptr);
    items_.push_back({hwnd, page, x, y, w, h, heading});
    return hwnd;
}

void SettingsWindow::CreateControls() {
    items_.clear();
    const int x = kContentX, w = kContentW;

    HWND nav = Add(-1, WC_LISTBOXW, L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, kIdNav, 12, 12,
                   164, kClientH - 24);
    for (const wchar_t* p : kPages) SendMessageW(nav, LB_ADDSTRING, 0, (LPARAM)p);

    // 表示
    Add(0, WC_STATICW, kPages[0], 0, 0, x, 16, w, 32, true);
    Add(0, WC_STATICW, L"表示するとき", 0, 0, x, 68, w, 20);
    HWND mode = Add(0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, kIdMode, x, 90, 300, 200);
    for (const wchar_t* s : {L"入力欄にいる間は常に表示", L"切り替えたときだけ表示", L"日本語入力のときだけ表示"})
        SendMessageW(mode, CB_ADDSTRING, 0, (LPARAM)s);
    Add(0, WC_STATICW, L"位置", 0, 0, x, 134, w, 20);
    HWND place = Add(0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, kIdPlacement, x, 156, 300, 200);
    for (const wchar_t* s : {L"キャレットの上", L"キャレットの右上", L"キャレットの下"})
        SendMessageW(place, CB_ADDSTRING, 0, (LPARAM)s);
    Add(0, WC_BUTTONW, L"入力位置が分からないときは、IME の切り替え時にマウスの位置に表示する",
        WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, kIdMouseFallback, x, 204, w, 40);

    // 見た目
    Add(1, WC_STATICW, kPages[1], 0, 0, x, 16, w, 32, true);
    Add(1, WC_STATICW, L"大きさ", 0, 0, x, 68, w, 20);
    HWND size = Add(1, TRACKBAR_CLASSW, L"大きさ", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, kIdSize, x - 6, 90, 330, 30);
    SendMessageW(size, TBM_SETRANGE, FALSE, MAKELPARAM(kMinSizeDip, kMaxSizeDip));
    SendMessageW(size, TBM_SETPAGESIZE, 0, 2);
    Add(1, WC_STATICW, L"", 0, kIdSizeValue, x + 334, 95, 80, 20);
    for (int row = 0; row < 2; ++row) {
        bool kana = row == 0;
        int y = kana ? 140 : 214;
        Add(1, WC_STATICW, kana ? L"「あ」の色(日本語入力)" : L"「a」の色(直接入力)", 0, 0, x, y, w, 20);
        for (int i = 0; i < SwatchCount(kana); ++i) {
            HWND sw = Add(1, WC_BUTTONW, SwatchName(kana, i), WS_TABSTOP | BS_OWNERDRAW | (i == 0 ? WS_GROUP : 0),
                          (kana ? kIdKanaSwatch : kIdLatinSwatch) + i, x + i * (kSwatchDip + kSwatchGapDip), y + 24,
                          kSwatchDip, kSwatchDip);
            if (!tooltip_) {
                tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0,
                                           0, hwnd_, nullptr, instance_, nullptr);
            }
            TTTOOLINFOW ti{sizeof(ti)};
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwnd_;
            ti.uId = (UINT_PTR)sw;
            ti.lpszText = const_cast<wchar_t*>(SwatchName(kana, i));
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
    }
    Add(1, WC_STATICW, L"プレビュー", 0, 0, x, 288, w, 20);
    Add(1, WC_STATICW, L"", SS_OWNERDRAW, kIdPreview, x, 310, w, 130);

    // アプリ
    Add(2, WC_STATICW, kPages[2], 0, 0, x, 16, w, 32, true);
    Add(2, WC_STATICW, L"次のアプリでは表示しません。", 0, 0, x, 68, w, 20);
    Add(2, WC_LISTBOXW, L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_SORT | LBS_NOINTEGRALHEIGHT,
        kIdAppList, x, 92, 300, 200);
    Add(2, WC_BUTTONW, L"削除", WS_TABSTOP | BS_PUSHBUTTON, kIdAppRemove, x + 312, 92, 140, 30);
    HWND edit = Add(2, WC_EDITW, L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kIdAppEdit, x, 307, 300, 24);
    SendMessageW(edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"exe の名前(例: notepad.exe)");
    Add(2, WC_BUTTONW, L"追加", WS_TABSTOP | BS_PUSHBUTTON, kIdAppAdd, x + 312, 305, 140, 30);
    Add(2, WC_BUTTONW, L"", WS_TABSTOP | BS_PUSHBUTTON, kIdAppRecent, x, 348, 300, 30);

    // 全般
    Add(3, WC_STATICW, kPages[3], 0, 0, x, 16, w, 32, true);
    Add(3, WC_BUTTONW, L"Windows の起動時に実行", WS_TABSTOP | BS_AUTOCHECKBOX, kIdAutostart, x, 68, w, 24);
    Add(3, WC_STATICW, L"KanaCaret バージョン " APP_VERSION_STR, 0, 0, x, 116, w, 20);
    Add(3, WC_STATICW, L"設定は %APPDATA%\\KanaCaret\\settings.ini に保存されます。", 0, 0, x, 140, w, 20);
}

void SettingsWindow::CreateFonts() {
    if (font_) DeleteObject(font_);
    if (headingFont_) DeleteObject(headingFont_);
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_);
    font_ = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW heading = ncm.lfMessageFont;
    heading.lfHeight = -S(20);
    heading.lfWeight = FW_SEMIBOLD;
    headingFont_ = CreateFontIndirectW(&heading);
}

void SettingsWindow::Layout() {
    for (const Item& it : items_) {
        SendMessageW(it.hwnd, WM_SETFONT, (WPARAM)(it.heading ? headingFont_ : font_), FALSE);
        SetWindowPos(it.hwnd, nullptr, S(it.x), S(it.y), S(it.w), S(it.h), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SendDlgItemMessageW(hwnd_, kIdNav, LB_SETITEMHEIGHT, 0, S(kNavItemH));
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::ShowPage(int page) {
    page_ = page;
    SendDlgItemMessageW(hwnd_, kIdNav, LB_SETCURSEL, page, 0);
    for (const Item& it : items_)
        if (it.page >= 0) ShowWindow(it.hwnd, it.page == page ? SW_SHOWNA : SW_HIDE);
}

void SettingsWindow::Refresh() {
    if (!hwnd_) return;
    const Settings& s = *settings_;
    SendDlgItemMessageW(hwnd_, kIdMode, CB_SETCURSEL, (WPARAM)s.mode, 0);
    SendDlgItemMessageW(hwnd_, kIdPlacement, CB_SETCURSEL, (WPARAM)s.placement, 0);
    CheckDlgButton(hwnd_, kIdMouseFallback, s.mouseFallback ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hwnd_, kIdSize, TBM_SETPOS, TRUE, s.sizeDip);
    SetDlgItemTextW(hwnd_, kIdSizeValue, (std::to_wstring(s.sizeDip) + L" px").c_str());
    CheckDlgButton(hwnd_, kIdAutostart, IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
    RefreshAppList();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::RefreshAppList() {
    HWND list = GetDlgItem(hwnd_, kIdAppList);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& e : settings_->exclude) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)e.c_str());
    EnableWindow(GetDlgItem(hwnd_, kIdAppRemove), FALSE);

    HWND recent = GetDlgItem(hwnd_, kIdAppRecent);
    bool can = !recentApp_.empty() && !settings_->IsExcluded(recentApp_);
    SetWindowTextW(recent, recentApp_.empty() ? L"最後に使ったアプリを追加"
                                              : (L"「" + recentApp_ + L"」を追加").c_str());
    EnableWindow(recent, can);
}

void SettingsWindow::Changed() {
    if (changed_) changed_();
}

void SettingsWindow::OnCommand(int id, int code, HWND ctl) {
    Settings& s = *settings_;
    if (id == kIdNav && code == LBN_SELCHANGE) {
        int sel = (int)SendMessageW(ctl, LB_GETCURSEL, 0, 0);
        if (sel >= 0) ShowPage(sel);
    } else if (id == kIdMode && code == CBN_SELCHANGE) {
        s.mode = (DisplayMode)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
        Changed();
    } else if (id == kIdPlacement && code == CBN_SELCHANGE) {
        s.placement = (Placement)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
        Changed();
    } else if (id == kIdMouseFallback && code == BN_CLICKED) {
        s.mouseFallback = IsDlgButtonChecked(hwnd_, id) == BST_CHECKED;
        Changed();
    } else if (id == kIdAutostart && code == BN_CLICKED) {
        SetAutostart(IsDlgButtonChecked(hwnd_, id) == BST_CHECKED);
    } else if (id >= kIdKanaSwatch && id < kIdKanaSwatch + SwatchCount(true) && code == BN_CLICKED) {
        PickSwatch(true, id - kIdKanaSwatch);
    } else if (id >= kIdLatinSwatch && id < kIdLatinSwatch + SwatchCount(false) && code == BN_CLICKED) {
        PickSwatch(false, id - kIdLatinSwatch);
    } else if (id == kIdAppList && code == LBN_SELCHANGE) {
        EnableWindow(GetDlgItem(hwnd_, kIdAppRemove), SendMessageW(ctl, LB_GETCURSEL, 0, 0) >= 0);
    } else if (id == kIdAppRemove && code == BN_CLICKED) {
        HWND list = GetDlgItem(hwnd_, kIdAppList);
        int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
        if (sel < 0) return;
        std::wstring name(SendMessageW(list, LB_GETTEXTLEN, sel, 0) + 1, L'\0');
        name.resize(SendMessageW(list, LB_GETTEXT, sel, (LPARAM)name.data()));
        if (s.IsExcluded(name)) s.ToggleExclude(name);
        RefreshAppList();
        SetFocus(list);
        Changed();
    } else if ((id == kIdAppAdd && code == BN_CLICKED) || (id == IDOK && GetFocus() == GetDlgItem(hwnd_, kIdAppEdit))) {
        // exe 名だけを受け付ける。拡張子が無ければ .exe を補う(区切りのカンマは使えない)
        HWND edit = GetDlgItem(hwnd_, kIdAppEdit);
        std::wstring name = Trim(WindowText(edit));
        name.erase(std::remove(name.begin(), name.end(), L','), name.end());
        if (size_t slash = name.find_last_of(L"\\/"); slash != std::wstring::npos) name.erase(0, slash + 1);
        if (name.empty()) return;
        if (name.find(L'.') == std::wstring::npos) name += L".exe";
        if (!s.IsExcluded(name)) s.ToggleExclude(name);
        SetWindowTextW(edit, L"");
        RefreshAppList();
        Changed();
    } else if (id == kIdAppRecent && code == BN_CLICKED) {
        if (!s.IsExcluded(recentApp_)) s.ToggleExclude(recentApp_);
        RefreshAppList();
        Changed();
    } else if (id == IDCANCEL) {  // Esc キーで閉じる
        Close();
    }
}

void SettingsWindow::PickSwatch(bool kana, int index) {
    FillChoice& fill = kana ? settings_->kanaFill : settings_->latinFill;
    if (IsOther(kana, index)) {
        CHOOSECOLORW cc{sizeof(cc)};
        cc.hwndOwner = hwnd_;
        cc.rgbResult = fill.kind == FillChoice::Custom ? fill.color : kPresets[0].color;
        cc.lpCustColors = g_customColors;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        if (!ChooseColorW(&cc)) return;
        fill = {FillChoice::Custom, cc.rgbResult};
    } else {
        fill = SwatchFill(kana, index);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    for (int i = 0; i < SwatchCount(kana); ++i)
        InvalidateRect(GetDlgItem(hwnd_, (kana ? kIdKanaSwatch : kIdLatinSwatch) + i), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd_, kIdPreview), nullptr, FALSE);
    Changed();
}

void SettingsWindow::OnDrawItem(const DRAWITEMSTRUCT* dis) {
    int id = (int)dis->CtlID;
    if (id == kIdNav) DrawNavItem(dis);
    else if (id == kIdPreview) DrawPreview(dis->hDC, dis->rcItem);
    else if (id >= kIdKanaSwatch && id < kIdKanaSwatch + SwatchCount(true)) DrawSwatch(dis, true, id - kIdKanaSwatch);
    else if (id >= kIdLatinSwatch && id < kIdLatinSwatch + SwatchCount(false)) DrawSwatch(dis, false, id - kIdLatinSwatch);
}

void SettingsWindow::DrawNavItem(const DRAWITEMSTRUCT* dis) {
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
    if ((int)dis->itemID < 0) return;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    if (selected) {
        // Windows 11 の設定アプリのように、選んだ項目を薄く塗り、左端にアクセントの縦線を引く
        RECT bg = rc;
        InflateRect(&bg, -S(2), -S(2));
        HGDIOBJ oldBrush = SelectObject(dc, GetSysColorBrush(COLOR_BTNFACE));
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, bg.left, bg.top, bg.right + 1, bg.bottom + 1, S(8), S(8));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        int cy = (rc.top + rc.bottom) / 2;
        RECT bar{rc.left + S(4), cy - S(8), rc.left + S(7), cy + S(8)};
        FillRect(dc, &bar, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
    wchar_t text[32] = L"";
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    HGDIOBJ oldFont = SelectObject(dc, font_);
    RECT tr = rc;
    tr.left += S(16);
    DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = rc;
        InflateRect(&fr, -S(1), -S(1));
        DrawFocusRect(dc, &fr);
    }
}

void SettingsWindow::DrawBadgeAt(HDC dc, int x, int y, int box, bool native, const BadgeColors& colors) {
    int margin = S(kMarginDip), size = box + margin * 2;
    void* bits = nullptr;
    HBITMAP dib = CreateDib(size, size, &bits);
    if (!dib) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, dib);
    if (renderer_->DrawBadge(mem, size, size, margin, box, dpi_, native ? ImeState::Native : ImeState::Direct, colors)) {
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        GdiAlphaBlend(dc, x - margin, y - margin, size, size, mem, 0, 0, size, size, blend);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(dib);
}

void SettingsWindow::DrawSwatch(const DRAWITEMSTRUCT* dis, bool kana, int index) {
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
    const FillChoice& current = kana ? settings_->kanaFill : settings_->latinFill;
    bool selected = SelectedSwatch(kana, current) == index;
    int box = S(kSwatchBadgeDip);
    int bx = (rc.left + rc.right - box) / 2, by = (rc.top + rc.bottom - box) / 2;

    if (IsOther(kana, index) && !selected) {
        // 「その他の色」: 点線の枠に「…」
        HPEN pen = CreatePen(PS_DOT, 1, GetSysColor(COLOR_GRAYTEXT));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, bx, by, bx + box, by + box, S(10), S(10));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        HGDIOBJ oldFont = SelectObject(dc, font_);
        RECT tr{bx, by, bx + box, by + box};
        DrawTextW(dc, L"…", -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    } else {
        // 見本は実際のバッジと同じ絵にする(設定画面はライトの見た目なので、ライトの配色で描く)
        FillChoice fill = IsOther(kana, index) ? current : SwatchFill(kana, index);
        DrawBadgeAt(dc, bx, by, box, kana, ColorsFor(theme_, false, kana, fill));
    }

    if (selected) {
        // アクセント色の見本の上でも見えるよう、選択の枠は文字の色で描く
        HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), GetSysColor(COLOR_WINDOWTEXT));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, S(12), S(12));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = rc;
        InflateRect(&fr, -S(3), -S(3));
        DrawFocusRect(dc, &fr);
    }
}

void SettingsWindow::DrawPreview(HDC dc, RECT rc) {
    // ライトとダークの背景に、キャレットとその上のバッジを並べる(実際の配置と同じく、左端をキャレットにそろえる)
    const int halfW = (rc.right - rc.left) / 2;
    const int box = S(settings_->sizeDip), gap = S(4), lineH = S(20);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    for (int half = 0; half < 2; ++half) {
        bool dark = half == 1;
        RECT area{rc.left + half * halfW, rc.top, half ? rc.right : rc.left + halfW, rc.bottom};
        HBRUSH bg = CreateSolidBrush(dark ? RGB(32, 32, 32) : RGB(243, 243, 243));
        FillRect(dc, &area, bg);
        DeleteObject(bg);
        COLORREF textColor = dark ? RGB(255, 255, 255) : RGB(27, 27, 27);

        RECT label{area.left + S(8), area.top + S(6), area.right, area.top + S(26)};
        SetTextColor(dc, dark ? RGB(160, 160, 160) : RGB(110, 110, 110));
        DrawTextW(dc, dark ? L"ダーク" : L"ライト", -1, &label, DT_SINGLELINE | DT_NOPREFIX);

        int lineTop = area.bottom - S(16) - lineH;
        for (int j = 0; j < 2; ++j) {
            bool native = j == 0;
            int caretX = area.left + S(84) + j * S(110);
            RECT tr{caretX - S(70), lineTop, caretX - S(1), lineTop + lineH};
            SetTextColor(dc, textColor);
            DrawTextW(dc, native ? L"かな" : L"abc", -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            RECT caret{caretX, lineTop, caretX + std::max(1, S(1)), lineTop + lineH};
            HBRUSH caretBrush = CreateSolidBrush(textColor);
            FillRect(dc, &caret, caretBrush);
            DeleteObject(caretBrush);
            const FillChoice& fill = native ? settings_->kanaFill : settings_->latinFill;
            DrawBadgeAt(dc, caretX, lineTop - gap - box, box, native, ColorsFor(theme_, dark, native, fill));
        }
    }
    SelectObject(dc, oldFont);
    FrameRect(dc, &rc, GetSysColorBrush(COLOR_BTNSHADOW));
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_COMMAND:
        if (self) self->OnCommand(LOWORD(wp), HIWORD(wp), (HWND)lp);
        return 0;
    case WM_HSCROLL:
        if (self && (HWND)lp == GetDlgItem(hwnd, kIdSize)) {
            int pos = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
            if (pos != self->settings_->sizeDip) {
                self->settings_->sizeDip = pos;
                SetDlgItemTextW(hwnd, kIdSizeValue, (std::to_wstring(pos) + L" px").c_str());
                InvalidateRect(GetDlgItem(hwnd, kIdPreview), nullptr, FALSE);
                self->Changed();
            }
        }
        return 0;
    case WM_MEASUREITEM:
        if (self && wp == kIdNav) {
            reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight = self->S(kNavItemH);
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (self) {
            self->OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:  // 文字・チェックボックス・スライダーの背景をウィンドウの背景に合わせる
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DPICHANGED:
        if (self) {
            self->dpi_ = HIWORD(wp);
            self->CreateFonts();
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->Layout();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (self) {
            // 子ウィンドウはこの時点で破棄済みなので、フォントを消してよい
            if (self->font_) DeleteObject(self->font_);
            if (self->headingFont_) DeleteObject(self->headingFont_);
            self->font_ = self->headingFont_ = nullptr;
            self->tooltip_ = nullptr;
            self->items_.clear();
            self->hwnd_ = nullptr;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
