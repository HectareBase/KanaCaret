#include "ImeMonitor.h"

#include <imm.h>

#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif

ImeState QueryImeState(HWND focus) {
    HWND ime = ImmGetDefaultIMEWnd(focus);
    if (!ime) return ImeState::Unknown;

    DWORD_PTR open = 0, conv = 0;
    if (!SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0, SMTO_ABORTIFHUNG, 50, &open))
        return ImeState::Unknown;
    // IME をオフにしても変換モードの NATIVE ビットは立ったままなので、ON/OFF を先に見る(docs/app-compatibility.md)
    if (!open) return ImeState::Direct;
    if (!SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG, 50, &conv))
        return ImeState::Unknown;
    return (conv & IME_CMODE_NATIVE) ? ImeState::Native : ImeState::Direct;
}
