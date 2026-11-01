// ime-probe: 検証ツール。
// 手前のアプリの IME 状態と、3 つの方法(Win32 / MSAA / UIA)で取得したキャレット座標を記録する。
// 記録は local\logs\probe-<日時>.log、終了時(Ctrl+C)のアプリごとの成功率は同じ名前の .csv に書き出す。
#include <windows.h>
#include <imm.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <shlobj.h>

#include <cstdio>
#include <cwchar>
#include <map>
#include <string>

#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif

namespace {

// ---- 共通ユーティリティ ----------------------------------------------------

DWORD g_mainTid = 0;
HANDLE g_console = INVALID_HANDLE_VALUE;
FILE* g_log = nullptr;
std::wstring g_logPath, g_csvPath;
IUIAutomation* g_uia = nullptr;
bool g_overlay = true;
UINT g_intervalMs = 100;

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

void Out(const std::wstring& line) {
    std::wstring s = line + L"\n";
    DWORD written = 0;
    if (!WriteConsoleW(g_console, s.data(), (DWORD)s.size(), &written, nullptr)) {
        std::string u = ToUtf8(s);
        WriteFile(g_console, u.data(), (DWORD)u.size(), &written, nullptr);
    }
    if (g_log) {
        std::string u = ToUtf8(s);
        fwrite(u.data(), 1, u.size(), g_log);
        fflush(g_log);
    }
}

std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}

long long NowUs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000 / freq.QuadPart;
}

std::wstring ExeName(DWORD pid) {
    static std::map<DWORD, std::wstring> cache;
    if (auto it = cache.find(pid); it != cache.end()) return it->second;
    std::wstring name = L"?";
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &len)) {
            const wchar_t* slash = wcsrchr(path, L'\\');
            name = slash ? slash + 1 : path;
        }
        CloseHandle(h);
    }
    cache[pid] = name;
    return name;
}

std::wstring ClassOf(HWND hwnd) {
    wchar_t buf[128] = L"";
    if (hwnd) GetClassNameW(hwnd, buf, _countof(buf));
    return buf;
}

std::wstring DpiInfo(HWND hwnd) {
    DPI_AWARENESS_CONTEXT ctx = GetWindowDpiAwarenessContext(hwnd);
    const wchar_t* aw = L"?";
    if (AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) aw = L"PMv2";
    else switch (GetAwarenessFromDpiAwarenessContext(ctx)) {
        case DPI_AWARENESS_UNAWARE: aw = L"Unaware"; break;
        case DPI_AWARENESS_SYSTEM_AWARE: aw = L"System"; break;
        case DPI_AWARENESS_PER_MONITOR_AWARE: aw = L"PMv1"; break;
        default: break;
    }
    return Fmt(L"%s/%u", aw, GetDpiForWindow(hwnd));
}

UINT MonitorDpi(const RECT& r) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static Fn getDpi = (Fn)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
    UINT dx = 96, dy = 96;
    if (HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST); getDpi && mon) getDpi(mon, 0, &dx, &dy);
    return dx;
}

// 文書全体や選択範囲ではなく、1 行分のキャレットらしい大きさか
bool CaretLike(const RECT& r) {
    LONG h = r.bottom - r.top, w = r.right - r.left;
    UINT dpi = MonitorDpi(r);
    return h >= MulDiv(6, dpi, 96) && h <= MulDiv(120, dpi, 96) && w <= h;
}

bool OnScreen(const RECT& r) {
    RECT vs = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    vs.right = vs.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vs.bottom = vs.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r.left >= vs.left && r.top >= vs.top && r.left < vs.right && r.top < vs.bottom;
}

std::wstring RectStr(bool ok, const RECT& r) {
    if (!ok) return L"-";
    return Fmt(L"(%ld,%ld %ldx%ld)%s", r.left, r.top, r.right - r.left, r.bottom - r.top,
               OnScreen(r) ? L"" : L"!画面外");
}

// ---- 対象ウィンドウの特定 --------------------------------------------------

struct Target {
    HWND fg = nullptr;
    HWND focus = nullptr;
    HWND caretWnd = nullptr;
    RECT caret{};
};

bool ReadGuiThread(HWND hwnd, Target& t) {
    GUITHREADINFO gti{sizeof(gti)};
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(hwnd, nullptr), &gti)) return false;
    t.focus = gti.hwndFocus;
    t.caretWnd = gti.hwndCaret;
    t.caret = gti.rcCaret;
    return true;
}

bool ResolveTarget(Target& t) {
    t = {};
    t.fg = GetForegroundWindow();
    if (!t.fg) return false;
    ReadGuiThread(t.fg, t);
    if (!t.focus) {
        // UWP: 入力を受けるのは ApplicationFrameWindow の子の CoreWindow(別プロセス)
        if (HWND core = FindWindowExW(t.fg, nullptr, L"Windows.UI.Core.CoreWindow", nullptr)) {
            if (!ReadGuiThread(core, t) || !t.focus) t.focus = core;
        }
    }
    if (!t.focus) t.focus = t.fg;
    return true;
}

// ---- IME 状態 --------------------------------------------------------------

// 戻り値: 1 = あ, 0 = a, -1 = 取得失敗
int QueryIme(HWND focus, DWORD& open, DWORD& conv) {
    open = conv = 0;
    HWND ime = ImmGetDefaultIMEWnd(focus);
    if (!ime) return -1;
    DWORD_PTR r = 0;
    if (!SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0, SMTO_ABORTIFHUNG, 50, &r)) return -1;
    open = (DWORD)r;
    if (!SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG, 50, &r)) return -1;
    conv = (DWORD)r;
    return (open && (conv & IME_CMODE_NATIVE)) ? 1 : 0;
}

// ---- キャレット: 方法 1 (Win32) --------------------------------------------

bool CaretWin32(const Target& t, RECT& out) {
    if (!t.caretWnd) return false;
    RECT r = t.caret;
    if (r.bottom - r.top <= 0) return false;
    MapWindowPoints(t.caretWnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    out = r;
    return true;
}

// ---- キャレット: 方法 2 (MSAA) ---------------------------------------------

bool CaretMsaa(HWND focus, RECT& out) {
    IAccessible* acc = nullptr;
    if (FAILED(AccessibleObjectFromWindow(focus, (DWORD)OBJID_CARET, IID_IAccessible, (void**)&acc)) || !acc)
        return false;
    VARIANT self;
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    long x = 0, y = 0, w = 0, h = 0;
    HRESULT hr = acc->accLocation(&x, &y, &w, &h, self);
    acc->Release();
    // Chromium は前にフォーカスしていた入力欄の古い位置を幅 0 で返すことがあるため、幅 0 は失敗として扱う
    if (FAILED(hr) || h <= 0 || w <= 0 || (x == 0 && y == 0)) return false;
    out = {x, y, x + w, y + h};
    return true;
}

// ---- キャレット: 方法 3 (UI Automation) ------------------------------------

bool RangeRect(IUIAutomationTextRange* range, RECT& out) {
    SAFEARRAY* sa = nullptr;
    if (FAILED(range->GetBoundingRectangles(&sa)) || !sa) return false;
    bool ok = false;
    LONG lb = 0, ub = -1;
    SafeArrayGetLBound(sa, 1, &lb);
    SafeArrayGetUBound(sa, 1, &ub);
    double* d = nullptr;
    if (ub - lb + 1 >= 4 && SUCCEEDED(SafeArrayAccessData(sa, (void**)&d))) {
        // 先頭の矩形 (left, top, width, height)
        out = {(LONG)d[0], (LONG)d[1], (LONG)(d[0] + d[2]), (LONG)(d[1] + d[3])};
        ok = d[3] > 0;
        SafeArrayUnaccessData(sa);
    }
    SafeArrayDestroy(sa);
    return ok;
}

// via: 1 = 範囲そのもの, 2 = 直後の 1 文字の左端, 3 = 直前の 1 文字の右端
bool CaretUia(RECT& out, int& via) {
    via = 0;
    if (!g_uia) return false;
    IUIAutomationElement* el = nullptr;
    if (FAILED(g_uia->GetFocusedElement(&el)) || !el) return false;

    IUIAutomationTextRange* caret = nullptr;
    IUIAutomationTextPattern2* tp2 = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&tp2))) && tp2) {
        BOOL active = FALSE;
        tp2->GetCaretRange(&active, &caret);
        tp2->Release();
    }
    if (!caret) {
        IUIAutomationTextPattern* tp = nullptr;
        if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) && tp) {
            IUIAutomationTextRangeArray* sel = nullptr;
            if (SUCCEEDED(tp->GetSelection(&sel)) && sel) {
                int len = 0;
                sel->get_Length(&len);
                if (len > 0) sel->GetElement(0, &caret);
                sel->Release();
            }
            tp->Release();
        }
    }
    el->Release();
    if (!caret) return false;

    bool ok = RangeRect(caret, out) && CaretLike(out);
    if (ok) via = 1;
    // 選択範囲や文書全体が返った場合は、先頭側に縮めて幅のない範囲にする
    caret->MoveEndpointByRange(TextPatternRangeEndpoint_End, caret, TextPatternRangeEndpoint_Start);
    if (!ok) {
        IUIAutomationTextRange* r = nullptr;
        if (SUCCEEDED(caret->Clone(&r)) && r) {
            if (SUCCEEDED(r->ExpandToEnclosingUnit(TextUnit_Character)) && RangeRect(r, out) && CaretLike(out)) {
                out.right = out.left + 1;
                via = 2;
                ok = true;
            }
            r->Release();
        }
    }
    if (!ok) {
        IUIAutomationTextRange* r = nullptr;
        if (SUCCEEDED(caret->Clone(&r)) && r) {
            int moved = 0;
            r->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved);
            if (moved != 0 && RangeRect(r, out) && CaretLike(out)) {
                out.left = out.right - 1;
                via = 3;
                ok = true;
            }
            r->Release();
        }
    }
    caret->Release();
    return ok;
}

// ---- 位置確認用マーカー ----------------------------------------------------
// キャレットの上に方法ごとの色の四角を並べ、取得した座標が正しいかを目で確かめる。
//   赤 = Win32(すぐ上) / 緑 = MSAA(その上) / 青 = UIA(いちばん上)

constexpr COLORREF kMarkerColors[3] = {RGB(232, 17, 35), RGB(16, 124, 16), RGB(0, 103, 192)};
HWND g_markers[3] = {};

LRESULT CALLBACK MarkerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush((COLORREF)GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        FillRect((HDC)wp, &rc, br);
        DeleteObject(br);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CreateMarkers() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = MarkerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ImeProbeMarker";
    RegisterClassW(&wc);
    for (int i = 0; i < 3; ++i) {
        g_markers[i] = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        SetWindowLongPtrW(g_markers[i], GWLP_USERDATA, kMarkerColors[i]);
        SetLayeredWindowAttributes(g_markers[i], 0, 220, LWA_ALPHA);
    }
}

void PlaceMarker(int i, bool ok, const RECT& r) {
    HWND m = g_markers[i];
    if (!m) return;
    if (!ok) {
        ShowWindow(m, SW_HIDE);
        return;
    }
    UINT dpi = MonitorDpi(r);
    int size = MulDiv(8, dpi, 96);
    int gap = MulDiv(3, dpi, 96);
    int y = r.top - (i + 1) * (size + gap);
    SetWindowPos(m, HWND_TOPMOST, r.left, y, size, size, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ---- 集計 ------------------------------------------------------------------

struct Stat {
    int samples = 0;  // いずれかの方法でキャレットが取れた回数
    int imeOk = 0, w32 = 0, msaa = 0, uia = 0;
    int caretEvents = 0;
    long long uiaUsSum = 0, uiaUsMax = 0;
    int uiaCalls = 0;
    std::wstring dpi, cls;
};
std::map<std::wstring, Stat> g_stats;

// ---- 1 回分の調査 ----------------------------------------------------------

std::wstring g_lastLine;

void Probe() {
    Target t;
    if (!ResolveTarget(t)) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(t.focus, &pid);
    std::wstring exe = ExeName(pid);
    std::wstring cls = ClassOf(t.focus);
    std::wstring dpi = DpiInfo(t.focus);

    DWORD open = 0, conv = 0;
    int ime = QueryIme(t.focus, open, conv);

    RECT rW{}, rM{}, rU{};
    bool okW = CaretWin32(t, rW);
    bool okM = CaretMsaa(t.focus, rM);
    int via = 0;
    long long t0 = NowUs();
    bool okU = CaretUia(rU, via);
    long long uiaUs = NowUs() - t0;

    if (g_overlay) {
        PlaceMarker(0, okW, rW);
        PlaceMarker(1, okM, rM);
        PlaceMarker(2, okU, rU);
    }

    Stat& s = g_stats[exe];
    s.dpi = dpi;
    s.cls = cls;
    s.uiaCalls++;
    s.uiaUsSum += uiaUs;
    if (uiaUs > s.uiaUsMax) s.uiaUsMax = uiaUs;
    if (okW || okM || okU) {
        s.samples++;
        if (ime >= 0) s.imeOk++;
        if (okW) s.w32++;
        if (okM) s.msaa++;
        if (okU) s.uia++;
    }

    const wchar_t* imeStr = ime == 1 ? L"あ" : ime == 0 ? L"a " : L"? ";
    std::wstring line = Fmt(L"%s [%s] %s | IME %s open=%lu conv=0x%04lx", exe.c_str(), cls.c_str(), dpi.c_str(),
                            imeStr, open, conv);
    line += L" | W32 " + RectStr(okW, rW);
    line += L" | MSAA " + RectStr(okM, rM);
    line += L" | UIA " + RectStr(okU, rU) + (okU ? Fmt(L"/%d", via) : L"");
    if (line == g_lastLine) return;
    g_lastLine = line;

    SYSTEMTIME st;
    GetLocalTime(&st);
    Out(Fmt(L"%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds) + line +
        Fmt(L"  (UIA %lldus)", uiaUs));
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
        if (idObject != OBJID_CARET || !hwnd) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        g_stats[ExeName(pid)].caretEvents++;
        return;
    }
    Probe();  // フォーカス・フォアグラウンドの変化
}

// ---- 結果の書き出し --------------------------------------------------------

int Pct(int n, int d) { return d ? n * 100 / d : 0; }

void WriteResults() {
    Out(L"");
    Out(L"=== アプリ別の結果(キャレットが取れたサンプルに対する割合) ===");
    Out(L"exe                          samples  IME   W32  MSAA   UIA  UIA平均us  UIA最大us  caret通知  DPI");
    FILE* csv = nullptr;
    if (_wfopen_s(&csv, g_csvPath.c_str(), L"wb") == 0 && csv) {
        fwrite("\xEF\xBB\xBF", 1, 3, csv);
        std::string head = "exe,class,dpi,samples,ime_ok_pct,win32_pct,msaa_pct,uia_pct,uia_avg_us,uia_max_us,caret_events\n";
        fwrite(head.data(), 1, head.size(), csv);
    }
    for (const auto& [exe, s] : g_stats) {
        long long avg = s.uiaCalls ? s.uiaUsSum / s.uiaCalls : 0;
        Out(Fmt(L"%-28s %7d %4d%% %4d%% %4d%% %4d%% %10lld %10lld %10d  %s", exe.c_str(), s.samples,
                Pct(s.imeOk, s.samples), Pct(s.w32, s.samples), Pct(s.msaa, s.samples), Pct(s.uia, s.samples), avg,
                s.uiaUsMax, s.caretEvents, s.dpi.c_str()));
        if (csv) {
            std::string row = ToUtf8(Fmt(L"%s,%s,%s,%d,%d,%d,%d,%d,%lld,%lld,%d\n", exe.c_str(), s.cls.c_str(),
                                         s.dpi.c_str(), s.samples, Pct(s.imeOk, s.samples), Pct(s.w32, s.samples),
                                         Pct(s.msaa, s.samples), Pct(s.uia, s.samples), avg, s.uiaUsMax,
                                         s.caretEvents));
            fwrite(row.data(), 1, row.size(), csv);
        }
    }
    if (csv) {
        fclose(csv);
        Out(L"保存しました: " + g_csvPath);
    }
}

// 出力先の既定値: リポジトリの local\logs(exe は build\probe\ にある)。
// 計測ログには使っているアプリ名や画面座標が入るので、公開しないフォルダーに置く
std::wstring DefaultOutDir() {
    wchar_t exe[MAX_PATH], full[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L'\\')) + L"\\..\\..\\local\\logs";
    return GetFullPathNameW(dir.c_str(), MAX_PATH, full, nullptr) ? full : dir;
}

void SetOutputPaths(const std::wstring& dir) {
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring base = dir + Fmt(L"\\probe-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
                                  st.wMinute, st.wSecond);
    g_logPath = base + L".log";
    g_csvPath = base + L".csv";
}

BOOL WINAPI OnConsoleCtrl(DWORD type) {
    PostThreadMessageW(g_mainTid, WM_QUIT, 0, 0);
    if (type == CTRL_CLOSE_EVENT) Sleep(3000);  // 書き出しが終わるまで待つ
    return TRUE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    UINT durationSec = 0;  // 0 = Ctrl+C まで
    std::wstring outDir = DefaultOutDir();
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--no-overlay")) g_overlay = false;
        else if (!wcscmp(argv[i], L"--interval") && i + 1 < argc) g_intervalMs = (UINT)_wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--duration") && i + 1 < argc) durationSec = (UINT)_wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--out") && i + 1 < argc) outDir = argv[++i];
        else {
            fwprintf(stderr, L"usage: ime-probe [--no-overlay] [--interval ms] [--duration sec] [--out dir]\n");
            return 2;
        }
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_mainTid = GetCurrentThreadId();
    g_console = GetStdHandle(STD_OUTPUT_HANDLE);
    SetOutputPaths(outDir);
    _wfopen_s(&g_log, g_logPath.c_str(), L"ab");
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia))))
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia));
    IUIAutomation2* uia2 = nullptr;
    if (g_uia && SUCCEEDED(g_uia->QueryInterface(IID_PPV_ARGS(&uia2)))) {
        uia2->put_ConnectionTimeout(1000);
        uia2->put_TransactionTimeout(1000);
        uia2->Release();
    }

    if (g_overlay) CreateMarkers();

    DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    HWINEVENTHOOK hooks[] = {
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnWinEvent, 0, 0, flags),
        SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, OnWinEvent, 0, 0, flags),
        SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnWinEvent, 0, 0, flags),
    };

    Out(L"ime-probe: 調べたいアプリで文字を入力してください。Ctrl+C で終了して結果を保存します。");
    Out(g_overlay ? L"マーカー: 赤=Win32  緑=MSAA  青=UIA(キャレットの上に並びます)" : L"マーカー: なし");
    Out(Fmt(L"UIA: %s / 間隔 %ums", g_uia ? L"使用可" : L"使用不可", g_intervalMs));
    Out(L"記録先: " + g_logPath);

    UINT_PTR probeTimer = SetTimer(nullptr, 0, g_intervalMs, nullptr);
    UINT_PTR quitTimer = durationSec ? SetTimer(nullptr, 0, durationSec * 1000, nullptr) : 0;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER && !msg.hwnd) {
            if (quitTimer && msg.wParam == quitTimer) break;
            if (msg.wParam == probeTimer) Probe();
            continue;
        }
        DispatchMessageW(&msg);
    }

    for (HWINEVENTHOOK h : hooks) if (h) UnhookWinEvent(h);
    WriteResults();
    if (g_uia) g_uia->Release();
    CoUninitialize();
    if (g_log) fclose(g_log);
    return 0;
}
