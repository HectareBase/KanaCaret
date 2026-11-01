#pragma once
#include <windows.h>

// 標準のコントロールとメニューをダークにする。Windows が公開していない uxtheme の関数を序数で呼ぶので、
// Windows 10 1903(ビルド 18362)以降で関数が見つかったときだけ使い、それ以外は何もしない(ライトのまま)。

// アプリのテーマに合わせてダークにできるようにする(起動時に 1 回)
void InitDarkMode();
// テーマの変更を読み直し、メニューに反映する
void RefreshDarkMode();
// ウィンドウのタイトルバーと、そのウィンドウ自身のダーク表示を切り替える
void SetWindowDark(HWND hwnd, bool dark);
// 標準のコントロールをダーク/ライトにする。darkClass はダークのときのテーマ名
// (ボタン・リストボックス・ツールチップは DarkMode_Explorer、コンボボックス・エディットは DarkMode_CFD)
void SetControlDark(HWND ctl, bool dark, const wchar_t* darkClass);
// ダークにできる環境か(関数が見つかったか)
bool DarkModeAvailable();
