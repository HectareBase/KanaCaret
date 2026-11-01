// badge-preview: インジケーターの見た目を、ライト/ダーク × あ/a × DPI(100/150/200%)で
// 1 枚の PNG に並べて保存する。Windows の設定を変えずに見た目を確認するためのツール。
//   badge-preview.exe out.png
//   badge-preview.exe --ico app.ico   … exe のアイコン(既定のアクセントカラーの「あ」)を書き出す
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

#include "../src/Renderer.h"
#include "../src/Theme.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kCell = 90;
constexpr UINT kDpis[] = {96, 144, 192};

// 出力先のフォルダーが無ければ作る(既定の置き場 local\checks\ はビルド直後には無い)
void EnsureParentDir(const wchar_t* path) {
    wchar_t full[MAX_PATH];
    if (!GetFullPathNameW(path, MAX_PATH, full, nullptr)) return;
    if (wchar_t* slash = wcsrchr(full, L'\\')) {
        *slash = L'\0';
        SHCreateDirectoryExW(nullptr, full, nullptr);
    }
}

bool SavePng(const wchar_t* path, const void* bits, int width, int height) {
    EnsureParentDir(path);
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetSize(width, height);
    frame->SetPixelFormat(&format);
    frame->WritePixels(height, width * 4, width * height * 4, (BYTE*)bits);
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

// PNG をメモリ上に作る(ICO の中身に使う)
bool EncodePng(const void* bits, int width, int height, std::vector<BYTE>& out) {
    wchar_t tmp[MAX_PATH], path[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    GetTempFileNameW(tmp, L"ico", 0, path);
    bool ok = SavePng(path, bits, width, height);
    if (ok) {
        HANDLE h = CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        DWORD size = GetFileSize(h, nullptr), read = 0;
        out.resize(size);
        ok = ReadFile(h, out.data(), size, &read, nullptr) && read == size;
        CloseHandle(h);
    }
    DeleteFileW(path);
    return ok;
}

// Windows 既定のアクセントカラー(ライトテーマの塗り)の「あ」を、各サイズの PNG を収めた ICO にする
int WriteIco(Renderer& renderer, const wchar_t* path) {
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 256};
    BadgeColors colors{D2D1::ColorF(0x005FB8), D2D1::ColorF(0x005FB8), D2D1::ColorF(D2D1::ColorF::White), 0};
    std::vector<std::vector<BYTE>> images;
    for (int size : sizes) {
        void* bits = nullptr;
        HBITMAP dib = CreateDib(size, size, &bits);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, dib);
        renderer.DrawBadge(dc, size, size, 0, size, 96 * size / 16, ImeState::Native, colors);
        SelectObject(dc, old);
        DeleteDC(dc);
        // PNG はストレートアルファなので戻す
        auto* px = static_cast<BYTE*>(bits);
        for (int i = 0; i < size * size; ++i, px += 4) {
            if (BYTE a = px[3]; a != 0 && a != 255)
                for (int c = 0; c < 3; ++c) px[c] = (BYTE)std::min(255, px[c] * 255 / a);
        }
        images.emplace_back();
        bool ok = EncodePng(bits, size, size, images.back());
        DeleteObject(dib);
        if (!ok) return 1;
    }

    EnsureParentDir(path);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return 1;
    auto put16 = [&](WORD v) { fwrite(&v, 2, 1, f); };
    auto put32 = [&](DWORD v) { fwrite(&v, 4, 1, f); };
    put16(0);  // ICONDIR
    put16(1);
    put16((WORD)images.size());
    DWORD offset = 6 + 16 * (DWORD)images.size();
    for (size_t i = 0; i < images.size(); ++i) {  // ICONDIRENTRY
        BYTE dim = sizes[i] >= 256 ? 0 : (BYTE)sizes[i];
        fputc(dim, f);
        fputc(dim, f);
        fputc(0, f);
        fputc(0, f);
        put16(1);
        put16(32);
        put32((DWORD)images[i].size());
        put32(offset);
        offset += (DWORD)images[i].size();
    }
    for (const auto& img : images) fwrite(img.data(), 1, img.size(), f);
    fclose(f);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: badge-preview out.png | badge-preview --ico app.ico\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Renderer renderer;
    if (!renderer.Init()) return 1;
    if (!wcscmp(argv[1], L"--ico") && argc >= 3) {
        int rc = WriteIco(renderer, argv[2]);
        CoUninitialize();
        return rc;
    }
    Theme theme = LoadTheme();

    // 行: ライト a / ライト あ / ダーク a / ダーク あ、列: DPI
    const int cols = (int)std::size(kDpis), rows = 4;
    const int width = kCell * cols, height = kCell * rows;
    void* canvasBits = nullptr;
    HBITMAP canvas = CreateDib(width, height, &canvasBits);
    HDC canvasDC = CreateCompatibleDC(nullptr);
    SelectObject(canvasDC, canvas);

    for (int row = 0; row < rows; ++row) {
        bool dark = row >= 2;
        ImeState state = (row % 2) ? ImeState::Native : ImeState::Direct;
        // Windows 11 のウィンドウ背景(Mica の基本色)に近い色を敷く
        RECT band{0, row * kCell, width, (row + 1) * kCell};
        HBRUSH bg = CreateSolidBrush(dark ? RGB(32, 32, 32) : RGB(243, 243, 243));
        FillRect(canvasDC, &band, bg);
        DeleteObject(bg);

        for (int col = 0; col < cols; ++col) {
            UINT dpi = kDpis[col];
            int box = MulDiv(22, dpi, 96), margin = MulDiv(6, dpi, 96), size = box + margin * 2;
            void* bits = nullptr;
            HBITMAP dib = CreateDib(size, size, &bits);
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = SelectObject(dc, dib);
            renderer.DrawBadge(dc, size, size, margin, box, dpi, state, ColorsFor(theme, dark, state == ImeState::Native));

            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            int x = col * kCell + (kCell - size) / 2, y = row * kCell + (kCell - size) / 2;
            GdiAlphaBlend(canvasDC, x, y, size, size, dc, 0, 0, size, size, blend);
            SelectObject(dc, old);
            DeleteDC(dc);
            DeleteObject(dib);
        }
    }

    GdiFlush();
    auto* px = static_cast<BYTE*>(canvasBits);
    for (int i = 0; i < width * height; ++i) px[i * 4 + 3] = 255;
    bool ok = SavePng(argv[1], canvasBits, width, height);
    DeleteDC(canvasDC);
    DeleteObject(canvas);
    CoUninitialize();
    return ok ? 0 : 1;
}
