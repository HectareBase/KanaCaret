#pragma once
#include <windows.h>

#include <atomic>

// UI Automation でキャレット位置を問い合わせる専用スレッド。
// UIA の呼び出しは最大 1 秒止まることがあるため(docs/app-compatibility.md)、UI スレッドでは呼ばない。
class UiaWorker {
public:
    struct Result {
        HWND focus = nullptr;  // 要求したときのフォーカス
        DWORD pid = 0;         // UIA のフォーカス要素のプロセス
        bool ok = false;
        RECT caret{};
    };

    // 結果は notify に resultMsg で届く。lParam が Result* で、受け取った側が delete する
    bool Start(HWND notify, UINT resultMsg);
    void Stop();

    // 問い合わせ中でなければ要求を出して true を返す。問い合わせ中は何もしない
    bool Request(HWND focus);
    // 結果を受け取ったら呼ぶ
    void Done() { busy_ = false; }

private:
    static DWORD WINAPI ThreadProc(void* param);
    void Run();

    HANDLE thread_ = nullptr;
    HANDLE ready_ = nullptr;
    DWORD tid_ = 0;
    HWND notify_ = nullptr;
    UINT resultMsg_ = 0;
    std::atomic<bool> busy_{false};
};
