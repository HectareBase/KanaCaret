#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct RunningApp {
    std::wstring exe;    // exe 名(設定に使う)
    std::wstring label;  // 表示名(exe のバージョン情報の説明。無ければ exe 名)
};

// タスクバーや Alt+Tab に出るウィンドウを持つアプリを、表示名の順に返す(自分自身は除く)。
// 対象のアプリを選ぶメニューに使う
std::vector<RunningApp> ListRunningApps();

// exe 名の大文字小文字を無視して比べる
bool SameExe(const std::wstring& a, const std::wstring& b);
