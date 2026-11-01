#include "UiaWorker.h"

#include <UIAutomation.h>

#include "CaretLocator.h"

namespace {

constexpr UINT kLocateMsg = WM_APP + 1;

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

IUIAutomationTextRange* CaretRange(IUIAutomationElement* el) {
    IUIAutomationTextRange* caret = nullptr;
    IUIAutomationTextPattern2* tp2 = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&tp2))) && tp2) {
        BOOL active = FALSE;
        tp2->GetCaretRange(&active, &caret);
        tp2->Release();
    }
    if (caret) return caret;

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
    return caret;
}

// アプリによって返し方が違うので、検証で有効だった 3 通りを順に試す(docs/app-compatibility.md)
//   1. 範囲そのもの(スタートメニューの検索)
//   2. 直後の 1 文字の左端(Windows Terminal)
//   3. 直前の 1 文字の右端(エクスプローラーのアドレスバー・検索欄)
bool CaretRect(IUIAutomationTextRange* caret, RECT& out) {
    if (RangeRect(caret, out) && PlausibleCaret(out)) return true;

    // 選択範囲や文書全体が返った場合は、先頭側に縮めて幅のない範囲にする
    caret->MoveEndpointByRange(TextPatternRangeEndpoint_End, caret, TextPatternRangeEndpoint_Start);

    bool ok = false;
    IUIAutomationTextRange* r = nullptr;
    if (SUCCEEDED(caret->Clone(&r)) && r) {
        if (SUCCEEDED(r->ExpandToEnclosingUnit(TextUnit_Character)) && RangeRect(r, out)) {
            out.right = out.left + 1;
            ok = PlausibleCaret(out);
        }
        r->Release();
    }
    if (ok) return true;

    r = nullptr;
    if (SUCCEEDED(caret->Clone(&r)) && r) {
        int moved = 0;
        r->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved);
        if (moved != 0 && RangeRect(r, out)) {
            out.left = out.right - 1;
            ok = PlausibleCaret(out);
        }
        r->Release();
    }
    return ok;
}

void Locate(IUIAutomation* uia, UiaWorker::Result& result) {
    IUIAutomationElement* el = nullptr;
    if (FAILED(uia->GetFocusedElement(&el)) || !el) return;
    int pid = 0;
    el->get_CurrentProcessId(&pid);
    result.pid = (DWORD)pid;
    if (IUIAutomationTextRange* caret = CaretRange(el)) {
        result.ok = CaretRect(caret, result.caret);
        caret->Release();
    }
    el->Release();
}

}  // namespace

bool UiaWorker::Start(HWND notify, UINT resultMsg) {
    notify_ = notify;
    resultMsg_ = resultMsg;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, &tid_);
    if (!thread_) return false;
    WaitForSingleObject(ready_, 5000);  // スレッドのメッセージキューができるまで待つ
    return true;
}

void UiaWorker::Stop() {
    if (!thread_) return;
    PostThreadMessageW(tid_, WM_QUIT, 0, 0);
    WaitForSingleObject(thread_, 2000);  // UIA の呼び出し中なら最大 1 秒待つ
    CloseHandle(thread_);
    CloseHandle(ready_);
    thread_ = ready_ = nullptr;
}

bool UiaWorker::Request(HWND focus) {
    if (!thread_ || busy_.exchange(true)) return false;
    if (!PostThreadMessageW(tid_, kLocateMsg, reinterpret_cast<WPARAM>(focus), 0)) {
        busy_ = false;
        return false;
    }
    return true;
}

DWORD WINAPI UiaWorker::ThreadProc(void* param) {
    static_cast<UiaWorker*>(param)->Run();
    return 0;
}

void UiaWorker::Run() {
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))))
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    IUIAutomation2* uia2 = nullptr;
    if (uia && SUCCEEDED(uia->QueryInterface(IID_PPV_ARGS(&uia2)))) {
        uia2->put_ConnectionTimeout(1000);
        uia2->put_TransactionTimeout(1000);
        uia2->Release();
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message != kLocateMsg) continue;
        auto* result = new UiaWorker::Result;
        result->focus = reinterpret_cast<HWND>(msg.wParam);
        if (uia) Locate(uia, *result);
        if (!PostMessageW(notify_, resultMsg_, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
            busy_ = false;
        }
    }

    if (uia) uia->Release();
    CoUninitialize();
}
