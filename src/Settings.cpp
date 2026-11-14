#include "Settings.h"

#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace {

constexpr wchar_t kSection[] = L"Indicator";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"KanaCaret";
constexpr wchar_t kAppFolder[] = L"KanaCaret";

std::wstring IniPath() {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::wstring dir = std::wstring(appData) + L"\\" + kAppFolder;
    CoTaskMemFree(appData);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
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

// 塗りの指定は auto / neutral / #RRGGBB の文字列で持つ
FillChoice ReadFill(const std::wstring& path, const wchar_t* key, FillChoice def) {
    wchar_t buf[16] = L"";
    GetPrivateProfileStringW(kSection, key, L"", buf, _countof(buf), path.c_str());
    std::wstring v = Lower(buf);
    if (v == L"auto") return {FillChoice::Auto};
    if (v == L"neutral") return {FillChoice::Neutral};
    if (v.size() == 7 && v[0] == L'#' && v.find_first_not_of(L"0123456789abcdef", 1) == std::wstring::npos) {
        unsigned long rgb = wcstoul(v.c_str() + 1, nullptr, 16);
        return {FillChoice::Custom, RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)};
    }
    return def;
}

void WriteFill(const std::wstring& path, const wchar_t* key, const FillChoice& fill) {
    wchar_t buf[16] = L"auto";
    if (fill.kind == FillChoice::Neutral) wcscpy_s(buf, L"neutral");
    else if (fill.kind == FillChoice::Custom)
        swprintf_s(buf, L"#%02X%02X%02X", GetRValue(fill.color), GetGValue(fill.color), GetBValue(fill.color));
    WritePrivateProfileStringW(kSection, key, buf, path.c_str());
}

}  // namespace

void Settings::Load() {
    std::wstring path = IniPath();
    if (path.empty()) return;
    mode = ReadEnum(path, L"Mode", mode, DisplayMode::NativeOnly);
    placement = ReadEnum(path, L"Placement", placement, Placement::Below);
    hideWhileTyping = ReadInt(path, L"HideWhileTyping", hideWhileTyping) != 0;
    // SizeDip が無ければ、以前の 3 段階の Size(0 = 小、1 = 中、2 = 大)から読み替える
    constexpr int kOldSizes[] = {18, 22, 28};
    int oldSize = ReadInt(path, L"Size", 1);
    style.sizeDip =
        std::clamp(ReadInt(path, L"SizeDip", kOldSizes[std::clamp(oldSize, 0, 2)]), kMinSizeDip, kMaxSizeDip);
    // KanaColor が無ければ、以前の AccentKana(0 なら「a」と同じ配色)から読み替える
    style.kanaFill =
        ReadFill(path, L"KanaColor", {ReadInt(path, L"AccentKana", 1) ? FillChoice::Auto : FillChoice::Neutral});
    style.latinFill = ReadFill(path, L"LatinColor", {});
    if (style.latinFill.kind == FillChoice::Neutral) style.latinFill = {};
    style.opacity = std::clamp(ReadInt(path, L"Opacity", style.opacity), kMinOpacity, kMaxOpacity);
    style.gapDip = std::clamp(ReadInt(path, L"GapDip", style.gapDip), 0, kMaxGapDip);
    style.shadow = std::clamp(ReadInt(path, L"Shadow", style.shadow), 0, kMaxShadow);

    allApps = ReadInt(path, L"AllApps", allApps) != 0;
    wchar_t buf[4096] = L"";
    GetPrivateProfileStringW(kSection, L"Apps", L"", buf, _countof(buf), path.c_str());
    apps.clear();
    std::wstring list = buf;
    for (size_t start = 0; start < list.size();) {
        size_t end = list.find(L',', start);
        if (end == std::wstring::npos) end = list.size();
        if (end > start) apps.push_back(Lower(list.substr(start, end - start)));
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
    WriteInt(path, L"HideWhileTyping", hideWhileTyping);
    WriteInt(path, L"SizeDip", style.sizeDip);
    WriteFill(path, L"KanaColor", style.kanaFill);
    WriteFill(path, L"LatinColor", style.latinFill);
    WriteInt(path, L"Opacity", style.opacity);
    WriteInt(path, L"GapDip", style.gapDip);
    WriteInt(path, L"Shadow", style.shadow);
    WriteInt(path, L"AllApps", allApps);
    std::wstring list;
    for (const std::wstring& e : apps) list += (list.empty() ? L"" : L",") + e;
    WritePrivateProfileStringW(kSection, L"Apps", list.c_str(), path.c_str());
    // 以前のキーは、新しいキーと食い違わないよう消す(MouseFallback と Exclude は廃止した設定)
    for (const wchar_t* old : {L"Size", L"AccentKana", L"MouseFallback", L"Exclude"})
        WritePrivateProfileStringW(kSection, old, nullptr, path.c_str());
}

bool Settings::IsTarget(const std::wstring& exe) const {
    return allApps || IsListed(exe);
}

bool Settings::IsListed(const std::wstring& exe) const {
    std::wstring key = Lower(exe);
    return std::find(apps.begin(), apps.end(), key) != apps.end();
}

void Settings::SetListed(const std::wstring& exe, bool listed) {
    std::wstring key = Lower(exe);
    auto it = std::find(apps.begin(), apps.end(), key);
    if (!listed && it != apps.end()) apps.erase(it);
    else if (listed && it == apps.end() && !key.empty()) apps.push_back(key);
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