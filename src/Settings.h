#pragma once
#include <windows.h>

#include <string>
#include <vector>

enum class DisplayMode {
    Always,      // 入力欄にいる間は常に表示
    OnChange,    // IME の切り替え時と、入力欄に移ったときだけ表示
    NativeOnly,  // 日本語入力のときだけ表示(「a」は出さない)
};

enum class Placement { Above, AboveRight, Below };

enum class SizeOption { Small, Medium, Large };

// %APPDATA%\ImeIndicator\settings.ini に保存する設定。
struct Settings {
    DisplayMode mode = DisplayMode::Always;
    Placement placement = Placement::Above;
    SizeOption size = SizeOption::Medium;
    bool accentKana = true;     // 「あ」をアクセントカラーで塗る
    bool mouseFallback = true;  // キャレットが取れないとき、IME の切り替え時にマウスの位置へ出す
    std::vector<std::wstring> exclude;  // 表示しないアプリの exe 名(小文字)

    void Load();
    void Save() const;

    bool IsExcluded(const std::wstring& exe) const;
    void ToggleExclude(const std::wstring& exe);
};

int SizeDip(SizeOption size);

// Windows の起動時に実行する(HKCU の Run キー)
bool IsAutostartEnabled();
void SetAutostart(bool enable);
