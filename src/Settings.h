#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "Theme.h"

enum class DisplayMode {
    Always,      // 入力欄にいる間は常に表示
    OnChange,    // IME の切り替え時と、入力欄に移ったときだけ表示
    NativeOnly,  // 日本語入力のときだけ表示(「a」は出さない)
};

enum class Placement { Above, AboveRight, Below };

// バッジの大きさ(DIP)の範囲と既定値
constexpr int kMinSizeDip = 14;
constexpr int kMaxSizeDip = 40;
constexpr int kDefaultSizeDip = 22;

// %APPDATA%\KanaCaret\settings.ini に保存する設定。
struct Settings {
    DisplayMode mode = DisplayMode::Always;
    Placement placement = Placement::Above;
    int sizeDip = kDefaultSizeDip;
    FillChoice kanaFill;        // 「あ」の塗り(Auto = アクセントカラー)
    FillChoice latinFill;       // 「a」の塗り(Neutral は使わない)
    bool mouseFallback = true;  // キャレットが取れないとき、IME の切り替え時にマウスの位置へ出す
    std::vector<std::wstring> exclude;  // 表示しないアプリの exe 名(小文字)

    void Load();
    void Save() const;

    bool IsExcluded(const std::wstring& exe) const;
    void ToggleExclude(const std::wstring& exe);
};

// Windows の起動時に実行する(HKCU の Run キー)
bool IsAutostartEnabled();
void SetAutostart(bool enable);
