// KanaCaret
// テキストキャレットの上に、IME が日本語入力なら「あ」、直接入力なら「a」を表示する常駐アプリ。
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "CaretLocator.h"
#include "ImeMonitor.h"
#include "IndicatorWindow.h"
#include "Renderer.h"
#include "Settings.h"
#include "Theme.h"
#include "TrayIcon.h"
#include "UiaWorker.h"

namespace {

// 他のアプリと重ならないよう、持ち主名を含めた一意の名前にする
constexpr wchar_t kMainClass[] = L"HectareBase.KanaCaret.Main";
constexpr wchar_t kMutexName[] = L"Local\\HectareBase.KanaCaret.SingleInstance";
constexpr wchar_t kLegacyMainClass[] = L"ImeIndicator.Main";  // 0.1.0(旧名)のメインウィンドウ
constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTickMs = 100;              // IME 状態の確認間隔
constexpr ULONGLONG kRelocateMs = 1000;    // 通知が来るアプリでも、取りこぼし対策としてこの間隔で問い合わせ直す
constexpr ULONGLONG kCaretGraceMs = 300;   // 再描画のたびにキャレットを隠すアプリ対策
constexpr ULONGLONG kFlashMs = 1500;       // 切り替え時だけ表示するときの表示時間
constexpr UINT kUiaResultMsg = WM_APP + 2;
constexpr ULONGLONG kUiaPollMs = 250;      // UIA で取れるアプリ(通知が来ない)の問い合わせ間隔
// キー入力やマウス操作が無い間は、IME の状態もキャレットもほとんど変わらないので問い合わせを減らす
constexpr DWORD kImeActiveMs = 500;        // 入力からこの時間は kTickMs ごとに IME を確かめる
constexpr ULONGLONG kImeIdlePollMs = 1000; // それ以外の間隔
constexpr DWORD kUiaActiveMs = 2000;       // 入力からこの時間は kUiaPollMs ごとに UIA で問い合わせる
constexpr ULONGLONG kUiaIdlePollMs = 2000; // それ以外の間隔(ターミナルの出力などで動く分を拾う)
constexpr ULONGLONG kIdleRelocateMs = 5000;  // 入力が無い間の、取りこぼし対策の問い合わせ間隔
constexpr ULONGLONG kFullscreenCheckMs = 1000;
constexpr UINT kRehookMsg = WM_APP + 3;
constexpr UINT_PTR kCaretTimerId = 2;
constexpr UINT kCaretCoalesceMs = 33;      // キャレットの通知はこの間隔(約 30 回/秒)にまとめる

enum class Reason { Tick, Focus, Caret, UiaResult };

// --stats で起動すると、負荷の内訳を 10 秒ごとに %TEMP%\kanacaret-stats.log へ書く(調査用)。
// --stats=<パス> で書き出し先を変えられる(tools\measure-load.ps1 は local\logs\ を指定する)
struct Stats {
    bool enabled = false;
    std::wstring path;
    ULONGLONG since = 0;
    ULONGLONG cpuSince = 0;  // 100ns 単位
    unsigned events = 0, caretEvents = 0, focusEvents = 0;
    unsigned updates = 0, imeQueries = 0, locates = 0, uiaRequests = 0;
} st;

ULONGLONG ProcessCpu() {
    FILETIME create, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    return (((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) +
           (((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime);
}

void FlushStats(ULONGLONG now) {
    if (!st.enabled || now - st.since < 10000) return;
    ULONGLONG cpu = ProcessCpu();
    FILE* f = nullptr;
    if (_wfopen_s(&f, st.path.c_str(), L"a") == 0 && f) {
        fprintf(f, "%llums cpu=%.1fms events=%u(caret=%u focus=%u) updates=%u ime=%u locate=%u uia=%u\n",
                now - st.since, (cpu - st.cpuSince) / 10000.0, st.events, st.caretEvents, st.focusEvents, st.updates,
                st.imeQueries, st.locates, st.uiaRequests);
        fclose(f);
    }
    unsigned* counters[] = {&st.events, &st.caretEvents, &st.focusEvents, &st.updates, &st.imeQueries, &st.locates, &st.uiaRequests};
    for (unsigned* c : counters) *c = 0;
    st.since = now;
    st.cpuSince = cpu;
}

// 通知領域のメニューのコマンド
enum Command : UINT {
    kCmdModeAlways = 100, kCmdModeOnChange, kCmdModeNativeOnly,
    kCmdPlaceAbove = 110, kCmdPlaceAboveRight, kCmdPlaceBelow,
    kCmdSizeSmall = 120, kCmdSizeMedium, kCmdSizeLarge,
    kCmdAccent = 130, kCmdMouseFallback, kCmdExclude, kCmdAutostart, kCmdPause, kCmdExit,
};

struct App {
    HWND main = nullptr;
    Settings settings;
    Renderer renderer;
    Theme theme;
    IndicatorWindow indicator;
    TrayIcon tray;
    ImeState trayState = ImeState::Direct;
    UiaWorker uia;
    UINT taskbarCreatedMsg = 0;
    HWINEVENTHOOK hooks[4] = {};  // 0, 1: フォアグラウンドとフォーカス(全体)、2, 3: キャレット(フォーカス中のアプリ)
    DWORD caretHookPid = 0;
    bool caretTimerPending = false;
    bool paused = false;
    bool updating = false;

    HWND lastFocus = nullptr;
    DWORD lastFocusPid = 0;
    ImeState lastIme = ImeState::Unknown;
    ULONGLONG lastImeQuery = 0;
    bool caretEvents = false;  // フォーカス中のアプリがキャレット移動を通知してくるか
    bool lastLocateOk = false;
    CaretSource lastSource = CaretSource::None;
    RECT lastCaret{};
    ULONGLONG lastCaretOk = 0;
    ULONGLONG lastLocate = 0;
    ULONGLONG flashUntil = 0;  // IME 切り替え後、キャレットが取れなければマウス位置に出す期限
    POINT flashPt{};
    ULONGLONG showUntil = 0;   // 「切り替え時だけ表示」で表示を続ける期限
    bool fullscreen = false;
    ULONGLONG fullscreenChecked = 0;
    std::wstring focusExe;     // 最後にフォーカスのあった他アプリの exe 名(除外メニューに使う)

    // UIA でしか取れなかった入力欄(exe 名|ウィンドウクラス)。次からは Win32/MSAA を試さずに UIA へ進む。
    // 同じ exe でも入力欄で方法が違う(エクスプローラーの名前変更は Win32、検索ボックスは UIA)ため、クラスも含める
    std::unordered_set<std::wstring> uiaKeys;
    std::unordered_map<DWORD, std::wstring> exeNames;
    std::wstring focusKey;
    bool preferUia = false;
} g;

const std::wstring& ExeName(DWORD pid) {
    auto it = g.exeNames.find(pid);
    if (it == g.exeNames.end()) {
        std::wstring name;
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t path[MAX_PATH];
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, path, &len)) {
                const wchar_t* slash = wcsrchr(path, L'\\');
                name = slash ? slash + 1 : path;
            }
            CloseHandle(h);
        }
        it = g.exeNames.emplace(pid, name).first;
    }
    return it->second;
}

std::wstring FocusKey(const FocusInfo& fi) {
    wchar_t cls[128] = L"";
    GetClassNameW(fi.focus, cls, _countof(cls));
    return ExeName(fi.pid) + L"|" + cls;
}

// フルスクリーンのアプリ・ゲーム・プレゼンテーション中か(問い合わせは 1 秒に 1 回まで)
bool IsFullscreenBusy(ULONGLONG now) {
    if (now - g.fullscreenChecked >= kFullscreenCheckMs) {
        QUERY_USER_NOTIFICATION_STATE state;
        g.fullscreen = SUCCEEDED(SHQueryUserNotificationState(&state)) &&
                       (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE);
        g.fullscreenChecked = now;
    }
    return g.fullscreen;
}

// 通知領域のアイコンも、いまの IME 状態のバッジにする(タスクバーは Windows のテーマに従う)
void UpdateTrayIcon(ImeState state) {
    g.trayState = state;
    int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(g.main));
    bool accent = g.settings.accentKana && state == ImeState::Native;
    g.tray.SetIcon(g.renderer.CreateIcon(size, state, ColorsFor(g.theme, g.theme.systemDark, accent)));
}

void ApplyTheme() {
    g.theme = LoadTheme();
    g.indicator.SetTheme(g.theme);
    UpdateTrayIcon(g.trayState);
}

void ApplySettings() {
    g.indicator.SetStyle(SizeDip(g.settings.size), g.settings.accentKana);
    UpdateTrayIcon(g.trayState);
}

// Chromium/Electron の UIA は古い位置を返すことがあるため(docs/app-compatibility.md)、MSAA だけを使う
bool UiaAllowed(HWND focus) {
    wchar_t cls[64] = L"";
    GetClassNameW(focus, cls, _countof(cls));
    return wcsncmp(cls, L"Chrome_", 7) != 0;
}

void RequestUia(const FocusInfo& fi, ULONGLONG now) {
    if (g.uia.Request(fi.focus)) {
        g.lastLocate = now;
        st.uiaRequests++;
    }
}

void OnUiaResult(UiaWorker::Result* r) {
    g.uia.Done();
    if (r->focus == g.lastFocus && r->pid == g.lastFocusPid) {
        if (r->ok) {
            g.lastSource = CaretSource::Uia;
            g.lastCaret = r->caret;
            g.lastCaretOk = GetTickCount64();
            g.lastLocateOk = true;
            g.uiaKeys.insert(g.focusKey);
        } else if (g.lastSource == CaretSource::Uia) {
            g.lastLocateOk = false;
        } else {
            g.preferUia = false;  // 覚えていた方法で取れなかったので、Win32/MSAA から試し直す
        }
    }
    delete r;
}

void Update(Reason reason) {
    if (g.updating) return;  // MSAA の呼び出し中に届いたイベントで再入しない
    g.updating = true;
    struct Reset {
        ~Reset() { g.updating = false; }
    } reset;
    st.updates++;

    FocusInfo fi;
    if (g.paused || !GetFocusInfo(fi) || fi.pid == GetCurrentProcessId()) {
        g.indicator.Hide();
        return;
    }

    ULONGLONG now = GetTickCount64();
    if (fi.focus != g.lastFocus) {
        g.lastFocus = fi.focus;
        g.lastFocusPid = fi.pid;
        g.caretEvents = false;
        g.lastLocateOk = false;
        g.lastSource = CaretSource::None;
        g.lastCaretOk = 0;
        g.focusKey = FocusKey(fi);
        g.focusExe = ExeName(fi.pid);
        g.preferUia = g.uiaKeys.count(g.focusKey) != 0;
        g.showUntil = now + kFlashMs;
        g.indicator.BringToTop();
        // フックの付け替えは、通知の処理中を避けてメッセージループに戻ってから行う
        if (fi.pid != g.caretHookPid) PostMessageW(g.main, kRehookMsg, fi.pid, 0);
    }

    if (g.settings.IsExcluded(g.focusExe) || IsFullscreenBusy(now)) {
        g.indicator.Hide();
        return;
    }

    // IME の状態を問い合わせるのは、フォーカスが変わったときと、入力の直後(キャレット移動のたびではなく
    // kTickMs ごと)だけ。入力が無い間も、念のため kImeIdlePollMs ごとに確かめる
    LASTINPUTINFO input{sizeof(input)};
    GetLastInputInfo(&input);
    DWORD sinceInput = GetTickCount() - input.dwTime;
    ImeState ime = g.lastIme;
    bool imeDue = reason == Reason::Focus || ime == ImeState::Unknown || now - g.lastImeQuery >= kImeIdlePollMs ||
                  (sinceInput < kImeActiveMs && now - g.lastImeQuery >= kTickMs);
    if (imeDue) {
        st.imeQueries++;
        ime = QueryImeState(fi.focus);
        g.lastImeQuery = now;
    }
    if (ime == ImeState::Unknown) {
        g.lastIme = ime;
        g.indicator.Hide();
        return;
    }
    if (ime != g.trayState) UpdateTrayIcon(ime);
    if (ime != g.lastIme) {
        g.lastIme = ime;
        g.flashUntil = g.showUntil = now + kFlashMs;
        GetCursorPos(&g.flashPt);
    }

    // 表示モードで隠す間は、キャレットの位置も問い合わせない(CPU を使わない)
    bool wanted = false;
    switch (g.settings.mode) {
    case DisplayMode::Always: wanted = true; break;
    case DisplayMode::OnChange: wanted = now < g.showUntil; break;
    case DisplayMode::NativeOnly: wanted = ime == ImeState::Native; break;
    }
    if (!wanted) {
        g.indicator.Hide();
        return;
    }

    RECT caret = g.lastCaret;
    bool ok = g.lastLocateOk;
    // 定期確認で位置を問い合わせるのは、通知の来ないアプリでキャレットが見えている間だけ。
    // それ以外(通知が来る/キャレットが無い)は通知に任せ、取りこぼし対策として kRelocateMs ごとに確かめる
    bool pollCaret = g.lastLocateOk && !g.caretEvents;
    if (reason == Reason::UiaResult) {
        // UIA の結果は受け取ったときに反映済みなので、表示だけ更新する
    } else if (g.lastSource == CaretSource::Uia || (g.lastSource == CaretSource::None && g.preferUia)) {
        RECT win32{};
        if (LocateCaret(fi, win32, CaretSource::Win32) == CaretSource::Win32) {
            // システムキャレットが現れたら Win32 に戻す(メモ帳は開いた直後だけキャレットが無く、UIA で取れてしまう)
            g.uiaKeys.erase(g.focusKey);
            g.preferUia = false;
            g.lastSource = CaretSource::Win32;
            g.lastCaret = caret = win32;
            g.lastCaretOk = g.lastLocate = now;
            g.lastLocateOk = ok = true;
        } else {
            // UIA で取れるウィンドウは通知が来ないので、一定間隔で問い合わせる。結果は非同期で届く。
            // 入力がしばらく無ければ間隔を延ばす(UIA は 1 回で約 3ms の CPU を使う)
            ULONGLONG interval = sinceInput < kUiaActiveMs ? kUiaPollMs : kUiaIdlePollMs;
            if (reason != Reason::Tick || now - g.lastLocate >= interval) RequestUia(fi, now);
        }
    } else if (reason != Reason::Tick || pollCaret ||
               now - g.lastLocate >= (sinceInput < kUiaActiveMs ? kRelocateMs : kIdleRelocateMs)) {
        // Win32 で取れたウィンドウでは Win32 だけを試す。MSAA で取れたウィンドウは、
        // Chromium のように入力欄が切り替わると取れなくなることがあるので、両方試す
        CaretSource known = g.lastSource == CaretSource::Win32 ? CaretSource::Win32 : CaretSource::None;
        st.locates++;
        CaretSource source = LocateCaret(fi, caret, known);
        ok = source != CaretSource::None;
        g.lastLocate = now;
        g.lastLocateOk = ok;
        if (ok) {
            g.lastSource = source;
            g.lastCaret = caret;
            g.lastCaretOk = now;
        } else if (g.lastSource != CaretSource::Win32 && UiaAllowed(fi.focus)) {
            RequestUia(fi, now);
        }
    }
    if (!ok && g.lastCaretOk && (g.lastSource == CaretSource::Win32 || now - g.lastCaretOk < kCaretGraceMs)) {
        // メモ帳などは、しばらく操作しないとフォーカスを保ったままシステムキャレットを破棄する。
        // Win32 で取れていた場合は、フォーカスが移るまで最後の位置に出し続ける
        ok = true;
        caret = g.lastCaret;
    }

    if (ok) g.indicator.ShowAtCaret(caret, ime, g.settings.placement);
    else if (g.settings.mouseFallback && now < g.flashUntil) g.indicator.ShowAtPoint(g.flashPt, ime);
    else g.indicator.Hide();
}

HMENU BuildMenu() {
    const Settings& s = g.settings;
    auto check = [](bool on) { return MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED); };

    HMENU mode = CreatePopupMenu();
    AppendMenuW(mode, MF_STRING, kCmdModeAlways, L"入力欄にいる間は常に表示");
    AppendMenuW(mode, MF_STRING, kCmdModeOnChange, L"切り替えたときだけ表示");
    AppendMenuW(mode, MF_STRING, kCmdModeNativeOnly, L"日本語入力のときだけ表示");
    CheckMenuRadioItem(mode, kCmdModeAlways, kCmdModeNativeOnly, kCmdModeAlways + (UINT)s.mode, MF_BYCOMMAND);

    HMENU place = CreatePopupMenu();
    AppendMenuW(place, MF_STRING, kCmdPlaceAbove, L"キャレットの上");
    AppendMenuW(place, MF_STRING, kCmdPlaceAboveRight, L"キャレットの右上");
    AppendMenuW(place, MF_STRING, kCmdPlaceBelow, L"キャレットの下");
    CheckMenuRadioItem(place, kCmdPlaceAbove, kCmdPlaceBelow, kCmdPlaceAbove + (UINT)s.placement, MF_BYCOMMAND);

    HMENU size = CreatePopupMenu();
    AppendMenuW(size, MF_STRING, kCmdSizeSmall, L"小");
    AppendMenuW(size, MF_STRING, kCmdSizeMedium, L"中");
    AppendMenuW(size, MF_STRING, kCmdSizeLarge, L"大");
    CheckMenuRadioItem(size, kCmdSizeSmall, kCmdSizeLarge, kCmdSizeSmall + (UINT)s.size, MF_BYCOMMAND);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)mode, L"表示するとき");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)place, L"位置");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)size, L"大きさ");
    AppendMenuW(menu, check(s.accentKana), kCmdAccent, L"「あ」をアクセントカラーにする");
    AppendMenuW(menu, check(s.mouseFallback), kCmdMouseFallback, L"入力位置が分からないときはマウスの位置に表示");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    if (!g.focusExe.empty()) {
        std::wstring label = L"「" + g.focusExe + L"」では表示しない";
        AppendMenuW(menu, check(s.IsExcluded(g.focusExe)), kCmdExclude, label.c_str());
    }
    AppendMenuW(menu, check(IsAutostartEnabled()), kCmdAutostart, L"Windows の起動時に実行");
    AppendMenuW(menu, check(g.paused), kCmdPause, L"一時停止");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"終了");
    return menu;
}

void OnCommand(UINT cmd) {
    Settings& s = g.settings;
    if (cmd >= kCmdModeAlways && cmd <= kCmdModeNativeOnly) s.mode = (DisplayMode)(cmd - kCmdModeAlways);
    else if (cmd >= kCmdPlaceAbove && cmd <= kCmdPlaceBelow) s.placement = (Placement)(cmd - kCmdPlaceAbove);
    else if (cmd >= kCmdSizeSmall && cmd <= kCmdSizeLarge) s.size = (SizeOption)(cmd - kCmdSizeSmall);
    else if (cmd == kCmdAccent) s.accentKana = !s.accentKana;
    else if (cmd == kCmdMouseFallback) s.mouseFallback = !s.mouseFallback;
    else if (cmd == kCmdExclude) s.ToggleExclude(g.focusExe);
    else if (cmd == kCmdAutostart) SetAutostart(!IsAutostartEnabled());
    else if (cmd == kCmdPause) g.paused = !g.paused;
    else if (cmd == kCmdExit) {
        DestroyWindow(g.main);
        return;
    } else {
        return;
    }
    s.Save();
    ApplySettings();
    g.showUntil = GetTickCount64() + kFlashMs;  // 変更がすぐ分かるよう、いったん表示する
    Update(Reason::Focus);
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    st.events++;
    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
    case EVENT_OBJECT_FOCUS:
        st.focusEvents++;
        Update(Reason::Focus);
        return;
    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_LOCATIONCHANGE: {
        if (idObject != OBJID_CARET || !hwnd) return;
        st.caretEvents++;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == g.lastFocusPid) g.caretEvents = true;
        // Chromium は 1 回のキー入力で何度も通知するので、続けて来たものはまとめて 1 回だけ問い合わせる
        if (GetTickCount64() - g.lastLocate < kCaretCoalesceMs) {
            if (!g.caretTimerPending) {
                g.caretTimerPending = true;
                SetTimer(g.main, kCaretTimerId, kCaretCoalesceMs, nullptr);
            }
            return;
        }
        Update(Reason::Caret);
        return;
    }
    }
}

HWINEVENTHOOK Hook(DWORD from, DWORD to, DWORD pid) {
    return SetWinEventHook(from, to, nullptr, OnWinEvent, pid, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void InstallHooks() {
    g.hooks[0] = Hook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, 0);
    g.hooks[1] = Hook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, 0);
}

// キャレットの表示・移動の通知は、フォーカスのあるアプリからだけ受け取る。
// システム全体で受けると、ほかのウィンドウの移動・表示の通知が毎秒 100 件以上届く(docs/design.md の「負荷を抑える仕組み」)
void HookCaretEvents(DWORD pid) {
    if (pid == g.caretHookPid) return;
    for (int i = 2; i < 4; ++i) {
        if (g.hooks[i]) UnhookWinEvent(g.hooks[i]);
        g.hooks[i] = nullptr;
    }
    g.caretHookPid = pid;
    if (!pid) return;
    g.hooks[2] = Hook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, pid);
    g.hooks[3] = Hook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, pid);
}

void RemoveHooks() {
    for (HWINEVENTHOOK& h : g.hooks) {
        if (h) UnhookWinEvent(h);
        h = nullptr;
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId) {
            Update(Reason::Tick);
            FlushStats(GetTickCount64());
        } else if (wp == kCaretTimerId) {
            KillTimer(hwnd, kCaretTimerId);
            g.caretTimerPending = false;
            Update(Reason::Caret);
        }
        return 0;
    case kTrayMsg:
        if (TrayIcon::IsMenuRequest(lp)) {
            HMENU menu = BuildMenu();
            UINT cmd = g.tray.TrackMenu(menu);
            DestroyMenu(menu);
            OnCommand(cmd);
        }
        return 0;
    case kUiaResultMsg:
        if (g.updating) {  // MSAA の呼び出し中に届いたら、終わってから処理する
            PostMessageW(hwnd, msg, wp, lp);
            return 0;
        }
        OnUiaResult(reinterpret_cast<UiaWorker::Result*>(lp));
        Update(Reason::UiaResult);
        return 0;
    case kRehookMsg:
        HookCaretEvents((DWORD)wp);
        return 0;
    case WM_SETTINGCHANGE:  // テーマ、ハイコントラスト、アニメーション効果の変更など
    case WM_SYSCOLORCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        ApplyTheme();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        RemoveHooks();
        g.uia.Stop();
        g.tray.Remove();
        g.indicator.Destroy();
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g.taskbarCreatedMsg && msg != 0) {
        g.tray.Add(hwnd, kTrayMsg);  // Explorer が再起動したらアイコンを登録し直す
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    if (const wchar_t* opt = wcsstr(cmdLine, L"--stats")) {
        st.enabled = true;
        st.since = GetTickCount64();
        st.cpuSince = ProcessCpu();
        if (opt[7] == L'=') {
            st.path = opt + 8;
            st.path.erase(std::remove(st.path.begin(), st.path.end(), L'"'), st.path.end());
        } else {
            wchar_t temp[MAX_PATH];
            GetTempPathW(MAX_PATH, temp);
            st.path = std::wstring(temp) + L"kanacaret-stats.log";
        }
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    // 旧版(0.1.0)が動いていれば、インジケーターが 2 つ出ないよう終了させる
    if (HWND legacy = FindWindowW(kLegacyMainClass, nullptr)) PostMessageW(legacy, WM_CLOSE, 0, 0);
    MigrateLegacyAutostart();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMainClass;
    RegisterClassW(&wc);
    // 通知領域とブロードキャスト(テーマ変更・TaskbarCreated)を受けるための非表示ウィンドウ
    g.main = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, L"KanaCaret", WS_POPUP, 0, 0, 0, 0, nullptr,
                             nullptr, instance, nullptr);
    if (!g.main || !g.renderer.Init() || !g.indicator.Create(instance, &g.renderer)) return 1;

    g.settings.Load();
    ApplySettings();
    ApplyTheme();
    g.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    g.tray.Add(g.main, kTrayMsg);
    g.uia.Start(g.main, kUiaResultMsg);
    InstallHooks();
    // 入力が無い間は Update が問い合わせをしないので、0.1 秒ごとに起きても負荷はほとんど無い。
    // 間引くと半角/全角キーの反映が遅れる(キー入力を受ける仕組みは、セキュリティソフトに警戒されるので使わない)
    SetCoalescableTimer(g.main, kTimerId, kTickMs, nullptr, 20);
    Update(Reason::Focus);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
