#include "AppList.h"

#include <appmodel.h>
#include <dwmapi.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace {

// Alt+Tab に出るウィンドウか(見えている、所有者が無い、ツールウィンドウでない、隠されていない)
bool IsAppWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return false;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return false;
    if (ex & WS_EX_NOACTIVATE) return false;
    // UWP は閉じた後や裏にいる間も、見えないよう隠された(cloaked)ウィンドウを残す
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    if (GetWindowTextLengthW(hwnd) == 0) return false;
    wchar_t cls[64] = L"";
    GetClassNameW(hwnd, cls, _countof(cls));
    return wcscmp(cls, L"Progman") != 0;  // デスクトップ
}

// 入力を受けるプロセス。UWP は ApplicationFrameHost の子の CoreWindow が、アプリ本体のプロセス(CaretLocator と同じ)
DWORD InputPid(HWND hwnd) {
    HWND target = hwnd;
    if (HWND core = FindWindowExW(hwnd, nullptr, L"Windows.UI.Core.CoreWindow", nullptr)) target = core;
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    return pid;
}

// パッケージのアプリ(ストアのメモ帳、設定など)の表示名。スタートメニューに出る名前と同じ。
// これらは exe の説明が「Notepad.exe」のように exe 名のままのことがある
std::wstring PackagedAppName(HANDLE process) {
    wchar_t aumid[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
    UINT32 len = _countof(aumid);
    if (GetApplicationUserModelId(process, &len, aumid) != ERROR_SUCCESS) return {};
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, aumid, IID_PPV_ARGS(&item)))) return {};
    std::wstring name;
    PWSTR text = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &text))) {
        name = text;
        CoTaskMemFree(text);
    }
    item->Release();
    return name;
}

// exe のパスと、パッケージのアプリならその表示名
bool ReadProcess(DWORD pid, std::wstring& path, std::wstring& packagedName) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t buf[MAX_PATH];
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
        path.assign(buf, len);
        packagedName = PackagedAppName(h);
    }
    CloseHandle(h);
    return !path.empty();
}

// exe のバージョン情報の FileDescription(「メモ帳」など)
std::wstring FileDescription(const std::wstring& path) {
    DWORD size = GetFileVersionInfoSizeExW(FILE_VER_GET_LOCALISED | FILE_VER_GET_NEUTRAL, path.c_str(), nullptr);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoExW(FILE_VER_GET_LOCALISED | FILE_VER_GET_NEUTRAL, path.c_str(), 0, size, data.data()))
        return {};
    struct Translation {
        WORD language, codePage;
    }* tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &len) || len < sizeof(*tr))
        return {};
    wchar_t key[64];
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->language, tr->codePage);
    wchar_t* text = nullptr;
    if (!VerQueryValueW(data.data(), key, (void**)&text, &len) || !text || len <= 1) return {};
    std::wstring s(text);
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

struct EnumState {
    DWORD self = GetCurrentProcessId();
    std::vector<DWORD> pids;
};

BOOL CALLBACK OnWindow(HWND hwnd, LPARAM lp) {
    auto* state = reinterpret_cast<EnumState*>(lp);
    if (!IsAppWindow(hwnd)) return TRUE;
    DWORD pid = InputPid(hwnd);
    if (pid && pid != state->self && std::find(state->pids.begin(), state->pids.end(), pid) == state->pids.end())
        state->pids.push_back(pid);
    return TRUE;
}

}  // namespace

bool SameExe(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::vector<RunningApp> ListRunningApps() {
    EnumState state;
    EnumWindows(OnWindow, reinterpret_cast<LPARAM>(&state));

    std::vector<RunningApp> apps;
    for (DWORD pid : state.pids) {
        std::wstring path, label;
        if (!ReadProcess(pid, path, label)) continue;  // 管理者権限のアプリなどは読めないので出さない
        const wchar_t* slash = wcsrchr(path.c_str(), L'\\');
        std::wstring exe = slash ? slash + 1 : path;
        // 同じ exe のウィンドウが複数のプロセスにある(ブラウザーなど)ときは 1 つにまとめる
        if (std::any_of(apps.begin(), apps.end(), [&](const RunningApp& a) { return SameExe(a.exe, exe); }))
            continue;
        if (label.empty()) label = FileDescription(path);
        apps.push_back({exe, label.empty() ? exe : label});
    }
    std::sort(apps.begin(), apps.end(), [](const RunningApp& a, const RunningApp& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.label.c_str(), -1, b.label.c_str(), -1,
                               nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return apps;
}
