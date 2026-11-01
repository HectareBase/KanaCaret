#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "ImeMonitor.h"
#include "Theme.h"

// 「あ」「a」のバッジを Direct2D/DirectWrite で描く。
// 描く先は 32bpp のプリマルチプライド DIB(UpdateLayeredWindow にそのまま渡せる形式)。
// 小さな画像なので GPU は使わずソフトウェア描画にする(D3D デバイスを作らない分、メモリが軽い)。
class Renderer {
public:
    bool Init();

    // dc に選択済みの width×height の DIB へ、(inset, inset) を左上として boxSize 四方のバッジを描く
    bool DrawBadge(HDC dc, int width, int height, int inset, int boxSize, UINT dpi, ImeState state,
                   const BadgeColors& colors);

    // 通知領域用のアイコン(size 四方)を作る。呼び出し側が DestroyIcon する
    HICON CreateIcon(int size, ImeState state, const BadgeColors& colors);

private:
    IDWriteTextFormat* Format(bool kana, float px);
    bool EnsureTarget();

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    const wchar_t* latinFamily_ = L"Segoe UI";

    // 直近に使った書式(DPI が変わったときだけ作り直す)
    struct CachedFormat {
        bool kana = false;
        float px = 0;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    } formats_[4];
    int nextFormat_ = 0;
};

// 32bpp トップダウンの DIB を作る。bits に先頭ポインタを返す
HBITMAP CreateDib(int width, int height, void** bits);
