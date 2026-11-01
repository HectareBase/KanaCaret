#include "Settings.h"

#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace {

constexpr wchar_t kSection[] = L"Indicator";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"KanaCaret";
constexpr wchar_t kAppFolder[] = L"KanaCaret";
// 0.1.0(旧名「IME インジケーター」)の設定フォルダーと Run の値。初回起動時に引き継ぐ
constexpr wchar_t kLegacyFolder[] = L"ImeIndicator";
constexpr wchar_t kLegacyRunValue[] = L"ImeIndicator";

std::wstring AppDataDir(const wchar_t* name) {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::wstring dir = std::wstring(appData) + L"\\" + name;
    CoTaskMemFree(appData);
    return dir;
}

std::wstring IniPath() {
    std::wstring dir = AppDataDir(kAppFolder);
    if (dir.empty()) return {};
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\settings.ini";
    // 新しい設定が無く旧版の設定があれば写す(旧フォルダーは消さない)
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring legacy = AppDataDir(kLegacyFolder) + L"\\settings.ini";
        CopyFileW(legacy.c_str(), path.c_str(), TRUE);
    }
    return path;
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

int ReadInt(const std::wstring& path, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(kSection, key, def, path.c_str());
}

void WriteInt(const std::wstring& path, const wchar_t* key, int value) {
    WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), path.c_str());
}

template <typename E>
E ReadEnum(const std::wstring& path, const wchar_t* key, E def, E max) {
    int v = ReadInt(path, key, (int)def);
    return (v >= 0 && v <= (int)max) ? (E)v : def;
}

}  // namespace

void Settings::Load() {
    std::wstring path = IniPath();
    if (path.empty()) return;
    mode = ReadEnum(path, L"Mode", mode, DisplayMode::NativeOnly);
    placement = ReadEnum(path, L"Placement", placement, Placement::Below);
    size = ReadEnum(path, L"Size", size, SizeOption::Large);
    accentKana = ReadInt(path, L"AccentKana", accentKana) != 0;
    mouseFallback = ReadInt(path, L"MouseFallback", mouseFallback) != 0;

    wchar_t buf[4096] = L"";
    GetPrivateProfileStringW(kSection, L"Exclude", L"", buf, _countof(buf), path.c_str());
    exclude.clear();
    std::wstring list = buf;
    for (size_t start = 0; start < list.size();) {
        size_t end = list.find(L',', start);
        if (end == std::wstring::npos) end = list.size();
        if (end > start) exclude.push_back(Lower(list.substr(start, end - start)));
        start = end + 1;
    }
}

void Settings::Save() const {
    std::wstring path = IniPath();
    if (path.empty()) return;
    // 新しく作るときは UTF-16 の BOM を書いておく(WritePrivateProfileString が Unicode で書くようになる)
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const WORD bom = 0xFEFF;
            DWORD written = 0;
            WriteFile(h, &bom, sizeof(bom), &written, nullptr);
            CloseHandle(h);
        }
    }
    WriteInt(path, L"Mode", (int)mode);
    WriteInt(path, L"Placement", (int)placement);
    WriteInt(path, L"Size", (int)size);
    WriteInt(path, L"AccentKana", accentKana);
    WriteInt(path, L"MouseFallback", mouseFallback);
    std::wstring list;
    for (const std::wstring& e : exclude) list += (list.empty() ? L"" : L",") + e;
    WritePrivateProfileStringW(kSection, L"Exclude", list.c_str(), path.c_str());
}

bool Settings::IsExcluded(const std::wstring& exe) const {
    std::wstring key = Lower(exe);
    return std::find(exclude.begin(), exclude.end(), key) != exclude.end();
}

void Settings::ToggleExclude(const std::wstring& exe) {
    std::wstring key = Lower(exe);
    auto it = std::find(exclude.begin(), exclude.end(), key);
    if (it != exclude.end()) exclude.erase(it);
    else if (!key.empty()) exclude.push_back(key);
}

int SizeDip(SizeOption size) {
    switch (size) {
    case SizeOption::Small: return 18;
    case SizeOption::Large: return 28;
    default: return 22;
    }
}

bool IsAutostartEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS;
}

void SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

void MigrateLegacyAutostart() {
    // 旧版の登録は旧 exe を指しているので、消してから、いま動いている exe で登録し直す
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kLegacyRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) !=
        ERROR_SUCCESS)
        return;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, kLegacyRunValue);
    RegCloseKey(key);
    SetAutostart(true);
}
