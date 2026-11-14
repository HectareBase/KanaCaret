#include "SettingsWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <iterator>

#include "AppList.h"
#include "DarkMode.h"
#include "version.h"  // CMakeLists.txt のバージョン番号から生成される

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"HectareBase.KanaCaret.Settings";
constexpr int kClientW = 680, kClientH = 560;  // DIP
constexpr int kNavItemH = 36;
constexpr int kContentX = 196, kContentW = 460;
constexpr int kSwatchDip = 32, kSwatchGapDip = 6, kSwatchBadgeDip = 24;
constexpr int kMarginDip = 6;  // バッジの影の余白(IndicatorWindow と同じ)
constexpr int kComboItemH = 28;  // コンボボックスの一覧の 1 行の高さ
// コンボボックスは自前で描く。一覧の行は WM_DRAWITEM、閉じた欄はサブクラスの WM_PAINT で描く。
// 標準のままだと、ダークのテーマで一覧の選んでいる行が白地に白い文字になり読めない。
// 一覧だけを自前で描く(CBS_OWNERDRAWFIXED)と、閉じた欄が白い枠と箱の付いた矢印の古い形になるので、欄も描く
constexpr DWORD kComboStyle = WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;

enum ControlId : int {
    kIdNav = 1000,
    kIdMode, kIdPlacement, kIdPreview,
    kIdAppScope, kIdAppHint, kIdAppList, kIdAppRefresh,
    kIdAutostart,
    kIdSize = 1020, kIdOpacity, kIdGap, kIdShadow,  // スライダー
    kIdKanaSwatch = 1100,   // + 見本の番号
    kIdLatinSwatch = 1200,
    kIdValueOffset = 500,   // スライダーの値を出す文字は、スライダーの ID + これ
};

const wchar_t* const kPages[] = {L"表示", L"見た目", L"アプリ", L"全般"};

// 見た目のページのスライダー
struct SliderDef {
    int id;
    const wchar_t* label;
    int min, max, line, page;
    const wchar_t* unit;
    int BadgeStyle::*field;
};
const SliderDef kSliders[] = {
    {kIdSize, L"大きさ", kMinSizeDip, kMaxSizeDip, 1, 2, L" px", &BadgeStyle::sizeDip},
    {kIdOpacity, L"不透明度", kMinOpacity, kMaxOpacity, 5, 10, L"%", &BadgeStyle::opacity},
    {kIdGap, L"キャレットとの間隔", 0, kMaxGapDip, 1, 2, L" px", &BadgeStyle::gapDip},
    {kIdShadow, L"影の濃さ", 0, kMaxShadow, 10, 20, L"%", &BadgeStyle::shadow},
};

const SliderDef* FindSlider(int id) {
    for (const SliderDef& d : kSliders)
        if (d.id == id) return &d;
    return nullptr;
}

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

COLORREF ToColorRef(const D2D1_COLOR_F& c) {
    return RGB((BYTE)(c.r * 255 + 0.5f), (BYTE)(c.g * 255 + 0.5f), (BYTE)(c.b * 255 + 0.5f));
}

D2D1_COLOR_F ToD2D(COLORREF c) { return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f); }

bool FocusCuesHidden(HWND ctl) { return (SendMessageW(ctl, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) != 0; }

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

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    if (!d2d_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());

    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(instance, kClassName, &wc)) {
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2;
    int y = std::max<int>(mi.rcWork.top, (mi.rcWork.top + mi.rcWork.bottom - h) / 2);

    hwnd_ = CreateWindowExW(0, kClassName, L"KanaCaret の設定", style, x, y, w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) return;
    // Windows 11 ではタイトルバーを Mica にする(クライアント領域は標準のコントロールに合わせて不透明のまま)
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    CreateFonts();
    CreateControls();
    ApplyPalette();
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
    if (hwnd_) ApplyPalette();
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
    HWND mode = Add(0, WC_COMBOBOXW, L"", kComboStyle, kIdMode, x, 90, 300, 200);
    for (const wchar_t* s : {L"入力欄にいる間は常に表示", L"切り替えたときだけ表示", L"日本語入力のときだけ表示"})
        SendMessageW(mode, CB_ADDSTRING, 0, (LPARAM)s);

    // 見た目: 左に項目名、右に部品を並べる
    const int labelW = 118, ctlX = x + labelW;
    Add(1, WC_STATICW, kPages[1], 0, 0, x, 16, w, 32, true);
    Add(1, WC_STATICW, L"位置", 0, 0, x, 68, labelW, 20);
    HWND place = Add(1, WC_COMBOBOXW, L"", kComboStyle, kIdPlacement, ctlX, 64, 262, 200);
    for (const wchar_t* s : {L"キャレットの上", L"キャレットの右上", L"キャレットの下"})
        SendMessageW(place, CB_ADDSTRING, 0, (LPARAM)s);
    for (int i = 0; i < (int)std::size(kSliders); ++i) {
        const SliderDef& d = kSliders[i];
        int y = 104 + i * 36;
        Add(1, WC_STATICW, d.label, 0, 0, x, y + 6, labelW, 20);
        HWND tb = Add(1, TRACKBAR_CLASSW, d.label, WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH, d.id,
                      ctlX - 6, y, 274, 32);
        SendMessageW(tb, TBM_SETRANGE, FALSE, MAKELPARAM(d.min, d.max));
        SendMessageW(tb, TBM_SETLINESIZE, 0, d.line);
        SendMessageW(tb, TBM_SETPAGESIZE, 0, d.page);
        Add(1, WC_STATICW, L"", 0, d.id + kIdValueOffset, ctlX + 276, y + 6, 70, 20);
    }
    for (int row = 0; row < 2; ++row) {
        bool kana = row == 0;
        int y = kana ? 256 : 324;
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
    Add(1, WC_STATICW, L"", SS_OWNERDRAW, kIdPreview, x, 396, w, 144);

    // アプリ
    Add(2, WC_STATICW, kPages[2], 0, 0, x, 16, w, 32, true);
    Add(2, WC_STATICW, L"表示するアプリ", 0, 0, x, 68, w, 20);
    HWND scope = Add(2, WC_COMBOBOXW, L"", kComboStyle, kIdAppScope, x, 90, 300, 200);
    for (const wchar_t* s : {L"すべてのアプリ", L"チェックしたアプリだけ"}) SendMessageW(scope, CB_ADDSTRING, 0, (LPARAM)s);
    for (HWND combo : {mode, place, scope}) SetWindowSubclass(combo, ComboProc, 0, (DWORD_PTR)this);
    Add(2, WC_STATICW, L"", 0, kIdAppHint, x, 130, w, 40);
    // 実行中のアプリの一覧。チェックを付けるだけで対象にできる(exe 名を打たなくてよい)
    HWND list = Add(2, WC_LISTVIEWW, L"アプリ",
                    WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    kIdAppList, x, 176, w, 306);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    // 列は 1 つだけにして、exe 名は行の右端に描く(列を分けると、ダークのテーマで境目に縦線が出る)
    LVCOLUMNW col{LVCF_WIDTH};
    ListView_InsertColumn(list, 0, &col);
    Add(2, WC_BUTTONW, L"一覧を更新", WS_TABSTOP | BS_PUSHBUTTON, kIdAppRefresh, x, 494, 140, 30);

    // 全般
    Add(3, WC_STATICW, kPages[3], 0, 0, x, 16, w, 32, true);
    Add(3, WC_BUTTONW, L"Windows の起動時に実行", WS_TABSTOP | BS_AUTOCHECKBOX, kIdAutostart, x, 68, w, 28);
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

// アプリのテーマ(ライト/ダーク)とハイコントラストから配色を決め、標準のコントロールのテーマも切り替える
void SettingsWindow::ApplyPalette() {
    Palette& p = palette_;
    if (theme_.highContrast) {
        p = {false,
             GetSysColor(COLOR_WINDOW),   GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_GRAYTEXT),
             GetSysColor(COLOR_BTNFACE),  GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_WINDOWTEXT),
             GetSysColor(COLOR_GRAYTEXT), GetSysColor(COLOR_HIGHLIGHT),  GetSysColor(COLOR_HIGHLIGHTTEXT)};
    } else if (theme_.dark && DarkModeAvailable()) {
        // Fluent のダークの色(背景、文字、ControlFill、ControlStrongStroke など)
        p = {true,
             RGB(32, 32, 32),    RGB(255, 255, 255), RGB(197, 197, 197),
             RGB(45, 45, 45),    RGB(43, 43, 43),    RGB(154, 154, 154),
             RGB(120, 120, 120), ToColorRef(theme_.accentLight2), RGB(0, 0, 0)};
    } else {
        p = {false,
             RGB(243, 243, 243), RGB(27, 27, 27),    RGB(96, 96, 96),
             RGB(234, 234, 234), RGB(255, 255, 255), RGB(138, 138, 138),
             RGB(196, 196, 196), ToColorRef(theme_.accentDark1), RGB(255, 255, 255)};
    }
    if (bgBrush_) DeleteObject(bgBrush_);
    if (controlBrush_) DeleteObject(controlBrush_);
    bgBrush_ = CreateSolidBrush(p.bg);
    controlBrush_ = CreateSolidBrush(p.controlBg);

    SetWindowDark(hwnd_, p.dark);
    for (const Item& it : items_) {
        wchar_t cls[32] = L"";
        GetClassNameW(it.hwnd, cls, _countof(cls));
        if (!_wcsicmp(cls, WC_COMBOBOXW)) {
            SetControlDark(it.hwnd, p.dark, L"DarkMode_CFD");
            COMBOBOXINFO cbi{sizeof(cbi)};
            if (GetComboBoxInfo(it.hwnd, &cbi)) SetControlDark(cbi.hwndList, p.dark, L"DarkMode_Explorer");
        } else if (!_wcsicmp(cls, WC_EDITW)) {
            SetControlDark(it.hwnd, p.dark, L"DarkMode_CFD");
        } else if (!_wcsicmp(cls, WC_LISTVIEWW)) {
            SetControlDark(it.hwnd, p.dark, L"DarkMode_Explorer");
            ListView_SetBkColor(it.hwnd, p.controlBg);
            ListView_SetTextBkColor(it.hwnd, p.controlBg);
            ListView_SetTextColor(it.hwnd, p.text);
        } else if (!_wcsicmp(cls, WC_LISTBOXW) ||
                   (!_wcsicmp(cls, WC_BUTTONW) && (GetWindowLongW(it.hwnd, GWL_STYLE) & BS_TYPEMASK) == BS_PUSHBUTTON)) {
            SetControlDark(it.hwnd, p.dark, L"DarkMode_Explorer");
        }
    }
    if (tooltip_) SetControlDark(tooltip_, p.dark, L"DarkMode_Explorer");
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void SettingsWindow::Layout() {
    for (const Item& it : items_) {
        SendMessageW(it.hwnd, WM_SETFONT, (WPARAM)(it.heading ? headingFont_ : font_), FALSE);
        SetWindowPos(it.hwnd, nullptr, S(it.x), S(it.y), S(it.w), S(it.h), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    for (const SliderDef& d : kSliders) SendDlgItemMessageW(hwnd_, d.id, TBM_SETTHUMBLENGTH, S(20), 0);
    SendDlgItemMessageW(hwnd_, kIdNav, LB_SETITEMHEIGHT, 0, S(kNavItemH));
    for (int id : {kIdMode, kIdPlacement, kIdAppScope}) {
        SendDlgItemMessageW(hwnd_, id, CB_SETITEMHEIGHT, (WPARAM)-1, ComboItemHeight(true));
        SendDlgItemMessageW(hwnd_, id, CB_SETITEMHEIGHT, 0, ComboItemHeight(false));
    }
    ListView_SetColumnWidth(GetDlgItem(hwnd_, kIdAppList), 0, LVSCW_AUTOSIZE_USEHEADER);  // 幅いっぱいにする
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
    for (const SliderDef& d : kSliders) {
        int v = s.style.*d.field;
        SendDlgItemMessageW(hwnd_, d.id, TBM_SETPOS, TRUE, v);
        SetDlgItemTextW(hwnd_, d.id + kIdValueOffset, (std::to_wstring(v) + d.unit).c_str());
    }
    CheckDlgButton(hwnd_, kIdAutostart, IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
    RefreshAppList();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::RefreshAppList() {
    HWND list = GetDlgItem(hwnd_, kIdAppList);
    // 作り直しても、見ていた位置と選んでいた行はそのままにする
    int top = ListView_GetTopIndex(list);
    int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    std::wstring selExe = sel >= 0 && sel < (int)appRows_.size() ? appRows_[sel] : L"";

    std::vector<std::pair<std::wstring, std::wstring>> rows;  // exe 名、表示名
    for (const RunningApp& app : ListRunningApps()) rows.emplace_back(app.exe, app.label);
    // 対象に挙げてあるが今はウィンドウの無いアプリも、外せるように後ろに並べる
    for (const std::wstring& exe : settings_->apps) {
        if (std::none_of(rows.begin(), rows.end(), [&](const auto& r) { return SameExe(r.first, exe); }))
            rows.emplace_back(exe, exe + L"(実行していません)");
    }

    fillingApps_ = true;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    appRows_.clear();
    int selRow = -1;
    for (const auto& [exe, label] : rows) {
        LVITEMW item{LVIF_TEXT};
        item.iItem = (int)appRows_.size();
        item.pszText = const_cast<wchar_t*>(label.c_str());
        int row = ListView_InsertItem(list, &item);
        if (row < 0) continue;
        ListView_SetCheckState(list, row, settings_->IsListed(exe));
        if (SameExe(exe, selExe)) selRow = row;
        appRows_.push_back(exe);
    }
    if (selRow >= 0) ListView_SetItemState(list, selRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    RECT ir;
    if (top > 0 && ListView_GetItemRect(list, 0, &ir, LVIR_BOUNDS)) ListView_Scroll(list, 0, top * (ir.bottom - ir.top));
    ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE_USEHEADER);  // スクロールバーが出たり消えたりした分を合わせる
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    fillingApps_ = false;
    UpdateAppHint();
}

void SettingsWindow::UpdateAppHint() {
    const Settings& s = *settings_;
    SendDlgItemMessageW(hwnd_, kIdAppScope, CB_SETCURSEL, s.allApps ? 0 : 1, 0);
    SetDlgItemTextW(hwnd_, kIdAppHint,
                    s.allApps ? L"すべてのアプリで表示します。下の一覧でチェックを付けると、チェックしたアプリだけで"
                                L"表示するように切り替わります。"
                              : L"チェックを付けたアプリでだけ表示します。一覧には、いまウィンドウを開いているアプリが"
                                L"並びます。");
}

// 一覧のチェックを付け外ししたとき。すべてのアプリが対象のときにチェックを付けたら、
// 「チェックしたアプリだけ」に切り替える(選ぶだけで済むように)
void SettingsWindow::OnAppChecked(const NMLISTVIEW* nm) {
    if (fillingApps_ || !(nm->uChanged & LVIF_STATE) || nm->iItem < 0 || nm->iItem >= (int)appRows_.size()) return;
    if (!((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK)) return;
    bool checked = (nm->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
    Settings& s = *settings_;
    const std::wstring& exe = appRows_[nm->iItem];
    if (checked == s.IsListed(exe) && !(checked && s.allApps)) return;
    s.SetListed(exe, checked);
    if (checked) s.allApps = false;
    UpdateAppHint();
    Changed();
}

// 一覧の各行の右端に、exe 名を表示名より薄い色で描く(表示名と exe 名が同じなら描かない)
LRESULT SettingsWindow::DrawAppListItem(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        cd->clrText = palette_.text;
        cd->clrTextBk = palette_.controlBg;
        return CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPOSTPAINT: {
        int row = (int)cd->nmcd.dwItemSpec;
        if (row < 0 || row >= (int)appRows_.size()) break;
        const std::wstring& exe = appRows_[row];
        wchar_t label[260] = L"";
        ListView_GetItemText(cd->nmcd.hdr.hwndFrom, row, 0, label, _countof(label));
        if (SameExe(label, exe)) break;
        RECT rc;
        ListView_GetItemRect(cd->nmcd.hdr.hwndFrom, row, &rc, LVIR_BOUNDS);
        rc.right -= S(8);
        HGDIOBJ oldFont = SelectObject(cd->nmcd.hdc, font_);
        SetBkMode(cd->nmcd.hdc, TRANSPARENT);
        SetTextColor(cd->nmcd.hdc, palette_.subtle);
        DrawTextW(cd->nmcd.hdc, exe.c_str(), -1, &rc, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
        SelectObject(cd->nmcd.hdc, oldFont);
        break;
    }
    }
    return CDRF_DODEFAULT;
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
        InvalidateRect(GetDlgItem(hwnd_, kIdPreview), nullptr, FALSE);
        Changed();
    } else if (id == kIdAutostart && code == BN_CLICKED) {
        SetAutostart(IsDlgButtonChecked(hwnd_, id) == BST_CHECKED);
    } else if (id >= kIdKanaSwatch && id < kIdKanaSwatch + SwatchCount(true) && code == BN_CLICKED) {
        PickSwatch(true, id - kIdKanaSwatch);
    } else if (id >= kIdLatinSwatch && id < kIdLatinSwatch + SwatchCount(false) && code == BN_CLICKED) {
        PickSwatch(false, id - kIdLatinSwatch);
    } else if (id == kIdAppScope && code == CBN_SELCHANGE) {
        s.allApps = SendMessageW(ctl, CB_GETCURSEL, 0, 0) == 0;
        UpdateAppHint();
        Changed();
    } else if (id == kIdAppRefresh && code == BN_CLICKED) {
        RefreshAppList();
    } else if (id == IDCANCEL) {  // Esc キーで閉じる
        Close();
    }
}

void SettingsWindow::OnSlider(HWND ctl) {
    const SliderDef* d = FindSlider(GetDlgCtrlID(ctl));
    if (!d) return;
    // つまみを丸く大きく描いているので、つまみの矩形の外も含めて描き直す
    InvalidateRect(ctl, nullptr, FALSE);
    int pos = (int)SendMessageW(ctl, TBM_GETPOS, 0, 0);
    int& value = settings_->style.*d->field;
    if (pos == value) return;
    value = pos;
    SetDlgItemTextW(hwnd_, d->id + kIdValueOffset, (std::to_wstring(pos) + d->unit).c_str());
    InvalidateRect(GetDlgItem(hwnd_, kIdPreview), nullptr, FALSE);
    Changed();
}

void SettingsWindow::PickSwatch(bool kana, int index) {
    FillChoice& fill = kana ? settings_->style.kanaFill : settings_->style.latinFill;
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
    for (int i = 0; i < SwatchCount(kana); ++i)
        InvalidateRect(GetDlgItem(hwnd_, (kana ? kIdKanaSwatch : kIdLatinSwatch) + i), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd_, kIdPreview), nullptr, FALSE);
    Changed();
}

bool SettingsWindow::BeginD2D(HDC dc, const RECT& rc) {
    if (!target_) {
        // 不透明な DC に描くだけなので、アルファは使わない。小さな部品なので GPU は使わない
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f, 96.0f);
        if (!d2d_ || FAILED(d2d_->CreateDCRenderTarget(&props, target_.GetAddressOf()))) return false;
    }
    if (FAILED(target_->BindDC(dc, &rc))) return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Translation(-(float)rc.left, -(float)rc.top));
    return true;
}

void SettingsWindow::EndD2D() {
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
}

ComPtr<ID2D1SolidColorBrush> SettingsWindow::Brush(COLORREF color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    target_->CreateSolidColorBrush(ToD2D(color), &brush);
    return brush;
}

LRESULT SettingsWindow::OnCustomDraw(NMCUSTOMDRAW* cd) {
    int id = (int)cd->hdr.idFrom;
    if (id == kIdAutostart) {
        if (cd->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
        DrawCheckBox(cd);
        return CDRF_SKIPDEFAULT;
    }
    if (FindSlider(id)) {
        if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
            DrawSliderPart(cd);
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

// Windows 11 のチェックボックス: 角丸の箱。オンならアクセントで塗ってチェックを描く
void SettingsWindow::DrawCheckBox(NMCUSTOMDRAW* cd) {
    const Palette& p = palette_;
    HDC dc = cd->hdc;
    RECT rc = cd->rc;
    FillRect(dc, &rc, bgBrush_);
    bool checked = SendMessageW(cd->hdr.hwndFrom, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const float box = Sf(20), radius = Sf(4);
    const float top = (rc.top + rc.bottom - box) / 2, left = (float)rc.left + Sf(1);
    if (BeginD2D(dc, rc)) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(left, top, left + box, top + box), radius, radius);
        if (checked) {
            target_->FillRoundedRectangle(rr, Brush(p.accent).Get());
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(d2d_->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(left + box * 0.27f, top + box * 0.52f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(left + box * 0.43f, top + box * 0.68f));
                sink->AddLine(D2D1::Point2F(left + box * 0.74f, top + box * 0.35f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                target_->DrawGeometry(path.Get(), Brush(p.onAccent).Get(), Sf(1.6f));
            }
        } else {
            D2D1_ROUNDED_RECT inner = rr;
            inner.rect = D2D1::RectF(left + 0.5f, top + 0.5f, left + box - 0.5f, top + box - 0.5f);
            target_->FillRoundedRectangle(inner, Brush(p.controlBg).Get());
            target_->DrawRoundedRectangle(inner, Brush(p.stroke).Get(), 1.0f);
        }
        EndD2D();
    }
    wchar_t text[64] = L"";
    GetWindowTextW(cd->hdr.hwndFrom, text, _countof(text));
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, p.text);
    RECT tr = rc;
    tr.left = (LONG)(left + box) + S(8);
    RECT measured = tr;
    DrawTextW(dc, text, -1, &measured, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
    DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    if ((cd->uItemState & CDIS_FOCUS) && !FocusCuesHidden(cd->hdr.hwndFrom)) {
        int h = measured.bottom - measured.top;
        RECT fr{tr.left - S(2), (rc.top + rc.bottom - h) / 2 - S(1), measured.right + S(2),
                (rc.top + rc.bottom + h) / 2 + S(1)};
        DrawFocusRect(dc, &fr);
    }
}

// Windows 11 のスライダー: 細い角丸の溝(つまみより左はアクセント)と、丸いつまみ
void SettingsWindow::DrawSliderPart(NMCUSTOMDRAW* cd) {
    const Palette& p = palette_;
    HWND tb = cd->hdr.hwndFrom;
    RECT client;
    GetClientRect(tb, &client);
    RECT thumb{}, channel{};
    SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
    SendMessageW(tb, TBM_GETCHANNELRECT, 0, (LPARAM)&channel);
    const float cx = (thumb.left + thumb.right) / 2.0f, cy = (thumb.top + thumb.bottom) / 2.0f;
    if (cd->dwItemSpec == TBCD_CHANNEL) {
        // 溝の描画のときに背景も塗る(つまみの外に描いた部分を消すため)
        FillRect(cd->hdc, &client, bgBrush_);
        if (!BeginD2D(cd->hdc, client)) return;
        const float h = Sf(4), l = (float)channel.left, r = (float)channel.right;
        D2D1_ROUNDED_RECT all = D2D1::RoundedRect(D2D1::RectF(l, cy - h / 2, r, cy + h / 2), h / 2, h / 2);
        target_->FillRoundedRectangle(all, Brush(p.track).Get());
        D2D1_ROUNDED_RECT done = D2D1::RoundedRect(D2D1::RectF(l, cy - h / 2, cx, cy + h / 2), h / 2, h / 2);
        target_->FillRoundedRectangle(done, Brush(p.accent).Get());
        EndD2D();
    } else if (cd->dwItemSpec == TBCD_THUMB) {
        if (!BeginD2D(cd->hdc, client)) return;
        const float outer = Sf(10), inner = Sf(6);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), outer, outer),
                             Brush(p.dark ? RGB(69, 69, 69) : RGB(255, 255, 255)).Get());
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), outer - 0.5f, outer - 0.5f),
                             Brush(p.dark ? RGB(80, 80, 80) : RGB(214, 214, 214)).Get(), 1.0f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), inner, inner), Brush(p.accent).Get());
        EndD2D();
    }
}

void SettingsWindow::OnDrawItem(const DRAWITEMSTRUCT* dis) {
    int id = (int)dis->CtlID;
    if (dis->CtlType == ODT_COMBOBOX) DrawComboItem(dis);
    else if (id == kIdNav) DrawNavItem(dis);
    else if (id == kIdPreview) DrawPreview(dis->hDC, dis->rcItem);
    else if (id >= kIdKanaSwatch && id < kIdKanaSwatch + SwatchCount(true)) DrawSwatch(dis, true, id - kIdKanaSwatch);
    else if (id >= kIdLatinSwatch && id < kIdLatinSwatch + SwatchCount(false)) DrawSwatch(dis, false, id - kIdLatinSwatch);
}

// コンボボックスの高さ。閉じた欄(field)は文字の高さに合わせ、一覧の行は少し広げる
int SettingsWindow::ComboItemHeight(bool field) const {
    if (!field) return S(kComboItemH);
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    return tm.tmHeight + S(1);
}

// コンボボックスの閉じた欄と一覧の行。一覧で選んでいる行は、左のカテゴリと同じく薄く塗ってアクセントの縦線を引く
void SettingsWindow::DrawComboItem(const DRAWITEMSTRUCT* dis) {
    const Palette& p = palette_;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool field = (dis->itemState & ODS_COMBOBOXEDIT) != 0;
    bool selected = !field && (dis->itemState & ODS_SELECTED);
    COLORREF text = p.text;
    if (!field) {
        // 閉じた欄の背景はテーマが描くので塗らない。一覧の行は背景から塗る
        FillRect(dc, &rc, controlBrush_);
        if (selected && theme_.highContrast) {
            HBRUSH hl = CreateSolidBrush(p.accent);
            FillRect(dc, &rc, hl);
            DeleteObject(hl);
            text = p.onAccent;
        } else if (selected && BeginD2D(dc, rc)) {
            // ダークでは一覧の背景(controlBg)とカテゴリの選択色が近いので、少し明るくする
            COLORREF fill = p.dark ? RGB(61, 61, 61) : p.navSelected;
            D2D1_RECT_F bg = D2D1::RectF(rc.left + Sf(2), rc.top + Sf(2), rc.right - Sf(2), rc.bottom - Sf(2));
            target_->FillRoundedRectangle(D2D1::RoundedRect(bg, Sf(4), Sf(4)), Brush(fill).Get());
            float cy = (rc.top + rc.bottom) / 2.0f;
            D2D1_RECT_F bar = D2D1::RectF(rc.left + Sf(2), cy - Sf(7), rc.left + Sf(5), cy + Sf(7));
            target_->FillRoundedRectangle(D2D1::RoundedRect(bar, Sf(1.5f), Sf(1.5f)), Brush(p.accent).Get());
            EndD2D();
        }
    }
    if ((int)dis->itemID < 0) return;
    std::wstring s((size_t)SendMessageW(dis->hwndItem, CB_GETLBTEXTLEN, dis->itemID, 0) + 1, L'\0');
    s.resize((size_t)SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)s.data()));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, IsWindowEnabled(dis->hwndItem) ? text : p.subtle);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    RECT tr = rc;
    tr.left += field ? S(4) : S(12);
    DrawTextW(dc, s.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    if (field && (dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = rc;
        InflateRect(&fr, -S(1), -S(1));
        DrawFocusRect(dc, &fr);
    }
}

// 閉じたコンボボックス: Windows 11 の形(角丸の欄、右端に山形の矢印)
void SettingsWindow::PaintCombo(HWND combo) {
    const Palette& p = palette_;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(combo, &ps);
    RECT rc;
    GetClientRect(combo, &rc);
    FillRect(dc, &rc, bgBrush_);
    bool enabled = IsWindowEnabled(combo) != FALSE;
    if (BeginD2D(dc, rc)) {
        COLORREF fill = theme_.highContrast ? p.controlBg : p.dark ? RGB(45, 45, 45) : RGB(251, 251, 251);
        COLORREF border = theme_.highContrast ? p.stroke : p.dark ? RGB(70, 70, 70) : RGB(204, 204, 204);
        D2D1_RECT_F f = D2D1::RectF(rc.left + 0.5f, rc.top + 0.5f, rc.right - 0.5f, rc.bottom - 0.5f);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(f, Sf(4), Sf(4));
        target_->FillRoundedRectangle(rr, Brush(fill).Get());
        target_->DrawRoundedRectangle(rr, Brush(border).Get(), 1.0f);
        // 山形の矢印
        float cx = rc.right - Sf(16), cy = (rc.top + rc.bottom) / 2.0f, w = Sf(4.5f), h = Sf(2.5f);
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(d2d_->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx - w, cy - h), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx, cy + h));
            sink->AddLine(D2D1::Point2F(cx + w, cy - h));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            target_->DrawGeometry(path.Get(), Brush(p.subtle).Get(), Sf(1.2f));
        }
        EndD2D();
    }
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        std::wstring s((size_t)SendMessageW(combo, CB_GETLBTEXTLEN, sel, 0) + 1, L'\0');
        s.resize((size_t)SendMessageW(combo, CB_GETLBTEXT, sel, (LPARAM)s.data()));
        HGDIOBJ oldFont = SelectObject(dc, font_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? p.text : p.subtle);
        RECT tr{rc.left + S(11), rc.top, rc.right - S(32), rc.bottom};
        DrawTextW(dc, s.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
    }
    if (GetFocus() == combo && !FocusCuesHidden(combo)) {
        RECT fr = rc;
        InflateRect(&fr, -S(3), -S(3));
        DrawFocusRect(dc, &fr);
    }
    EndPaint(combo, &ps);
}

LRESULT CALLBACK SettingsWindow::ComboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<SettingsWindow*>(ref);
    switch (msg) {
    case WM_PAINT:
        self->PaintCombo(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_UPDATEUISTATE:
    case CB_SETCURSEL:
    case WM_KEYDOWN:
    case WM_CHAR:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_COMMAND:
    case WM_CAPTURECHANGED: {
        // 標準の処理が欄を直接描くことがあるので、後から描き直す
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ComboProc, 0);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void SettingsWindow::DrawNavItem(const DRAWITEMSTRUCT* dis) {
    const Palette& p = palette_;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(dc, &rc, bgBrush_);
    if ((int)dis->itemID < 0) return;
    if ((dis->itemState & ODS_SELECTED) && BeginD2D(dc, rc)) {
        // Windows 11 の設定アプリのように、選んだ項目を薄く塗り、左端にアクセントの縦線を引く
        D2D1_RECT_F bg = D2D1::RectF(rc.left + Sf(2), rc.top + Sf(2), rc.right - Sf(2), rc.bottom - Sf(2));
        target_->FillRoundedRectangle(D2D1::RoundedRect(bg, Sf(4), Sf(4)), Brush(p.navSelected).Get());
        float cy = (rc.top + rc.bottom) / 2.0f;
        D2D1_RECT_F bar = D2D1::RectF(rc.left + Sf(2), cy - Sf(8), rc.left + Sf(5), cy + Sf(8));
        target_->FillRoundedRectangle(D2D1::RoundedRect(bar, Sf(1.5f), Sf(1.5f)), Brush(p.accent).Get());
        EndD2D();
    }
    wchar_t text[32] = L"";
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, p.text);
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

void SettingsWindow::DrawBadgeAt(HDC dc, int x, int y, int box, bool native, BadgeColors colors, int opacity,
                                 int shadow) {
    int margin = S(kMarginDip), size = box + margin * 2;
    void* bits = nullptr;
    HBITMAP dib = CreateDib(size, size, &bits);
    if (!dib) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, dib);
    colors.shadowAlpha *= shadow / 100.0f;
    if (renderer_->DrawBadge(mem, size, size, margin, box, dpi_, native ? ImeState::Native : ImeState::Direct, colors)) {
        BLENDFUNCTION blend{AC_SRC_OVER, 0, (BYTE)(255 * opacity / 100), AC_SRC_ALPHA};
        GdiAlphaBlend(dc, x - margin, y - margin, size, size, mem, 0, 0, size, size, blend);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(dib);
}

void SettingsWindow::DrawSwatch(const DRAWITEMSTRUCT* dis, bool kana, int index) {
    const Palette& p = palette_;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(dc, &rc, bgBrush_);
    const FillChoice& current = kana ? settings_->style.kanaFill : settings_->style.latinFill;
    bool selected = SelectedSwatch(kana, current) == index;
    int box = S(kSwatchBadgeDip);
    int bx = (rc.left + rc.right - box) / 2, by = (rc.top + rc.bottom - box) / 2;

    if (IsOther(kana, index) && !selected) {
        // 「その他の色」: 点線の枠に「…」
        if (BeginD2D(dc, rc)) {
            ComPtr<ID2D1StrokeStyle> dash;
            d2d_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                                D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
                                                                D2D1_DASH_STYLE_DASH),
                                    nullptr, 0, &dash);
            float r = box * 5.0f / 22;
            D2D1_RECT_F f = D2D1::RectF(bx + 0.5f, by + 0.5f, bx + box - 0.5f, by + box - 0.5f);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(f, r, r), Brush(p.subtle).Get(), 1.0f, dash.Get());
            EndD2D();
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, p.text);
        HGDIOBJ oldFont = SelectObject(dc, font_);
        RECT tr{bx, by, bx + box, by + box};
        DrawTextW(dc, L"…", -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    } else {
        // 見本は実際のバッジと同じ絵にする(画面のテーマの配色で描く)
        FillChoice fill = IsOther(kana, index) ? current : SwatchFill(kana, index);
        DrawBadgeAt(dc, bx, by, box, kana, ColorsFor(theme_, p.dark, kana, fill), 100, 100);
    }

    if (selected && BeginD2D(dc, rc)) {
        // アクセント色の見本の上でも見えるよう、選択の枠は文字の色で描く
        float w = Sf(2);
        D2D1_RECT_F f = D2D1::RectF(rc.left + w / 2, rc.top + w / 2, rc.right - w / 2, rc.bottom - w / 2);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(f, Sf(7), Sf(7)), Brush(p.text).Get(), w);
        EndD2D();
    }
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = rc;
        InflateRect(&fr, -S(3), -S(3));
        DrawFocusRect(dc, &fr);
    }
}

void SettingsWindow::DrawPreview(HDC dc, RECT rc) {
    // ライトとダークの背景に、キャレットとバッジを実際の配置・間隔・不透明度・影で並べる
    const BadgeStyle& style = settings_->style;
    const int halfW = (rc.right - rc.left) / 2;
    const int box = S(style.sizeDip), gap = S(style.gapDip), lineH = S(20), caretW = std::max(1, S(1));
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    for (int half = 0; half < 2; ++half) {
        bool dark = half == 1;
        RECT area{rc.left + half * halfW, rc.top, half ? rc.right : rc.left + halfW, rc.bottom};
        int saved = SaveDC(dc);
        IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
        HBRUSH bg = CreateSolidBrush(dark ? RGB(32, 32, 32) : RGB(243, 243, 243));
        FillRect(dc, &area, bg);
        DeleteObject(bg);
        COLORREF textColor = dark ? RGB(255, 255, 255) : RGB(27, 27, 27);

        RECT label{area.left + S(8), area.top + S(6), area.right, area.top + S(26)};
        SetTextColor(dc, dark ? RGB(160, 160, 160) : RGB(110, 110, 110));
        DrawTextW(dc, dark ? L"ダーク" : L"ライト", -1, &label, DT_SINGLELINE | DT_NOPREFIX);

        int lineTop = (area.top + area.bottom - lineH) / 2;
        for (int j = 0; j < 2; ++j) {
            bool native = j == 0;
            int caretX = area.left + S(70) + j * S(100);
            RECT tr{caretX - S(60), lineTop, caretX - S(1), lineTop + lineH};
            SetTextColor(dc, textColor);
            DrawTextW(dc, native ? L"かな" : L"abc", -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            RECT caret{caretX, lineTop, caretX + caretW, lineTop + lineH};
            HBRUSH caretBrush = CreateSolidBrush(textColor);
            FillRect(dc, &caret, caretBrush);
            DeleteObject(caretBrush);

            // IndicatorWindow::ShowAtCaret と同じ配置
            int bx = caretX, by = lineTop - gap - box;
            if (settings_->placement == Placement::AboveRight) {
                bx = caret.right + gap;
                by = lineTop - box;
            } else if (settings_->placement == Placement::Below) {
                by = caret.bottom + gap;
            }
            const FillChoice& fill = native ? style.kanaFill : style.latinFill;
            DrawBadgeAt(dc, bx, by, box, native, ColorsFor(theme_, dark, native, fill), style.opacity, style.shadow);
        }
        RestoreDC(dc, saved);
    }
    SelectObject(dc, oldFont);
    HBRUSH frame = CreateSolidBrush(palette_.stroke);
    FrameRect(dc, &rc, frame);
    DeleteObject(frame);
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
        if (self && lp) self->OnSlider((HWND)lp);
        return 0;
    case WM_NOTIFY:
        if (self) {
            auto* hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr->idFrom == kIdAppList) {
                if (hdr->code == NM_CUSTOMDRAW) return self->DrawAppListItem(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
                if (hdr->code == LVN_ITEMCHANGED) self->OnAppChecked(reinterpret_cast<NMLISTVIEW*>(lp));
                return 0;
            }
            if (hdr->code == NM_CUSTOMDRAW) return self->OnCustomDraw(reinterpret_cast<NMCUSTOMDRAW*>(lp));
        }
        break;
    case WM_ACTIVATE:
        // 他のアプリを開いてから戻ってきたときに、一覧にすぐ出るようにする
        if (self && LOWORD(wp) != WA_INACTIVE && !self->items_.empty()) self->RefreshAppList();
        break;
    case WM_MEASUREITEM:
        if (self) {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis->CtlType == ODT_COMBOBOX) {
                mis->itemHeight = self->ComboItemHeight(mis->itemID == (UINT)-1);
                return TRUE;
            }
            if (wp == kIdNav) {
                mis->itemHeight = self->S(kNavItemH);
                return TRUE;
            }
        }
        break;
    case WM_DRAWITEM:
        if (self) {
            self->OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        if (self && self->bgBrush_) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, self->bgBrush_);
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:  // 文字・スライダーの背景
    case WM_CTLCOLORBTN:     // ボタンの角の外側
        if (self && self->bgBrush_) {
            SetBkColor((HDC)wp, self->palette_.bg);
            SetTextColor((HDC)wp, self->palette_.text);
            return (LRESULT)self->bgBrush_;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:  // 入力欄・一覧・コンボボックスの一覧(左のカテゴリだけは背景と同じ色)
        if (self && self->controlBrush_) {
            bool nav = (HWND)lp == GetDlgItem(hwnd, kIdNav);
            SetBkColor((HDC)wp, nav ? self->palette_.bg : self->palette_.controlBg);
            SetTextColor((HDC)wp, self->palette_.text);
            return (LRESULT)(nav ? self->bgBrush_ : self->controlBrush_);
        }
        break;
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
            // 子ウィンドウはこの時点で破棄済みなので、フォントやブラシを消してよい
            for (HGDIOBJ* obj : {(HGDIOBJ*)&self->font_, (HGDIOBJ*)&self->headingFont_, (HGDIOBJ*)&self->bgBrush_,
                                 (HGDIOBJ*)&self->controlBrush_}) {
                if (*obj) DeleteObject(*obj);
                *obj = nullptr;
            }
            self->target_.Reset();
            self->tooltip_ = nullptr;
            self->items_.clear();
            self->hwnd_ = nullptr;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
