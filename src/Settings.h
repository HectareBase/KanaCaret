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

// 見た目の設定の範囲と既定値
constexpr int kMinSizeDip = 14, kMaxSizeDip = 40, kDefaultSizeDip = 22;  // 大きさ(DIP)
constexpr int kMinOpacity = 30, kMaxOpacity = 100;                       // 不透明度(%)
constexpr int kMaxGapDip = 16, kDefaultGapDip = 4;                       // キャレットとの間隔(DIP)
constexpr int kMaxShadow = 200;                                          // 影の濃さ(既定の濃さに対する %)

// バッジの見た目(位置以外)
struct BadgeStyle {
    int sizeDip = kDefaultSizeDip;
    FillChoice kanaFill;   // 「あ」の塗り(Auto = アクセントカラー)
    FillChoice latinFill;  // 「a」の塗り(Neutral は使わない)
    int opacity = kMaxOpacity;
    int gapDip = kDefaultGapDip;
    int shadow = 100;
    bool operator==(const BadgeStyle&) const = default;
};

// %APPDATA%\KanaCaret\settings.ini に保存する設定。
struct Settings {
    DisplayMode mode = DisplayMode::Always;
    Placement placement = Placement::Above;
    BadgeStyle style;
    bool allApps = true;             // すべてのアプリで表示する。false なら apps に挙げたアプリだけ
    std::vector<std::wstring> apps;  // 表示するアプリの exe 名(小文字)。allApps の間も消さずに残す

    void Load();
    void Save() const;

    bool IsTarget(const std::wstring& exe) const;  // このアプリで表示するか
    bool IsListed(const std::wstring& exe) const;  // apps に挙げているか
    void SetListed(const std::wstring& exe, bool listed);
};

// Windows の起動時に実行する(HKCU の Run キー)
bool IsAutostartEnabled();
void SetAutostart(bool enable);
