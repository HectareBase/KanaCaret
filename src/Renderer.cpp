#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {

constexpr float kBoxDip = 22.0f;  // IndicatorWindow の箱の大きさ。角の丸みと文字の大きさはこれとの比で決める
constexpr float kRadiusDip = 5.0f;
constexpr float kFontDip = 13.0f;
constexpr float kShadowDip = 4.0f;        // 影の広がり
constexpr float kShadowOffsetDip = 1.0f;  // 影を下にずらす量
constexpr int kShadowLayers = 6;

}  // namespace

HBITMAP CreateDib(int width, int height, void** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // トップダウン
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
}

bool Renderer::Init() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;

    // Windows 11 の UI フォント。無ければ Segoe UI を使う
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts)) &&
        SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
        latinFamily_ = L"Segoe UI Variable Text";
    return EnsureTarget();
}

bool Renderer::EnsureTarget() {
    if (target_) return true;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(d2d_->CreateDCRenderTarget(&props, target_.GetAddressOf()))) return false;
    // 背景が透明なので ClearType は使えない
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

IDWriteTextFormat* Renderer::Format(bool kana, float px) {
    for (const CachedFormat& f : formats_)
        if (f.format && f.kana == kana && f.px == px) return f.format.Get();

    CachedFormat& slot = formats_[nextFormat_];
    nextFormat_ = (nextFormat_ + 1) % static_cast<int>(std::size(formats_));
    slot = {};
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(kana ? L"Yu Gothic UI" : latinFamily_, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"ja-JP",
                                         &format)))
        return nullptr;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    slot = {kana, px, format};
    return format.Get();
}

bool Renderer::DrawBadge(HDC dc, int width, int height, int inset, int boxSize, UINT dpi, ImeState state,
                         const BadgeColors& colors) {
    if (!EnsureTarget()) return false;
    RECT bounds{0, 0, width, height};
    if (FAILED(target_->BindDC(dc, &bounds))) return false;

    const float s = dpi / 96.0f;
    const bool native = state == ImeState::Native;
    const D2D1_RECT_F box = D2D1::RectF((float)inset, (float)inset, (float)(inset + boxSize), (float)(inset + boxSize));
    const float radius = kRadiusDip / kBoxDip * boxSize;  // 箱の大きさに比例(通知領域のアイコンでも同じ形にする)

    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0.0f));

    ComPtr<ID2D1SolidColorBrush> brush;
    target_->CreateSolidColorBrush(colors.fill, &brush);

    // 影: 少しずつ大きくした角丸を薄く重ねて、ぼかしの代わりにする(エフェクトを使わず軽く済ませる)
    if (colors.shadowAlpha > 0) {
        brush->SetColor(D2D1::ColorF(0, 0, 0, colors.shadowAlpha / kShadowLayers));
        for (int i = kShadowLayers; i >= 1; --i) {
            float e = kShadowDip * s * i / kShadowLayers;
            D2D1_RECT_F r = D2D1::RectF(box.left - e, box.top - e + kShadowOffsetDip * s, box.right + e,
                                        box.bottom + e + kShadowOffsetDip * s);
            target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius + e, radius + e), brush.Get());
        }
    }

    brush->SetColor(colors.fill);
    target_->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brush.Get());

    const float stroke = std::max(1.0f, std::round(s));
    D2D1_RECT_F edge = D2D1::RectF(box.left + stroke / 2, box.top + stroke / 2, box.right - stroke / 2,
                                   box.bottom - stroke / 2);
    brush->SetColor(colors.border);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(edge, radius - stroke / 2, radius - stroke / 2), brush.Get(),
                                  stroke);

    float px = std::round(kFontDip / kBoxDip * boxSize);
    if (IDWriteTextFormat* format = Format(native, px)) {
        brush->SetColor(colors.text);
        D2D1_RECT_F layout = box;
        if (!native) {  // 小文字の「a」は行の中央だと低く見えるので少し上げる
            layout.top -= px * 0.06f;
            layout.bottom -= px * 0.06f;
        }
        target_->DrawText(native ? L"あ" : L"a", 1, format, layout, brush.Get());
    }

    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) target_.Reset();
    return SUCCEEDED(hr);
}

HICON Renderer::CreateIcon(int size, ImeState state, const BadgeColors& colors) {
    void* bits = nullptr;
    HBITMAP color = CreateDib(size, size, &bits);
    if (!color) return nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, color);
    BadgeColors flat = colors;
    flat.shadowAlpha = 0;  // 小さいので影は付けない
    bool ok = DrawBadge(dc, size, size, 0, size, GetDpiForSystem(), state, flat);
    SelectObject(dc, old);
    DeleteDC(dc);

    HICON icon = nullptr;
    if (ok) {
        // アイコンはプリマルチプライドではない(ストレートアルファ)ので戻す
        auto* px = static_cast<BYTE*>(bits);
        for (int i = 0; i < size * size; ++i, px += 4) {
            BYTE a = px[3];
            if (a != 0 && a != 255) {
                px[0] = (BYTE)std::min(255, px[0] * 255 / a);
                px[1] = (BYTE)std::min(255, px[1] * 255 / a);
                px[2] = (BYTE)std::min(255, px[2] * 255 / a);
            }
        }
        HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
        ICONINFO ii{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&ii);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}
