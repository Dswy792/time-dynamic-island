#include "rendering.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "animation.h"
#include "app_state.h"
#include "color_util.h"
#include "layout.h"
#include "theme.h"
#include "time_format.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace
{
constexpr int kShadowLayers = 10;

HWND s_hwnd = nullptr;

Bitmap* s_bitmap = nullptr;
Graphics* s_graphics = nullptr;
HDC s_memoryDc = nullptr;
HBITMAP s_dib = nullptr;
HGDIOBJ s_previousDib = nullptr;
void* s_dibPixels = nullptr;
int s_surfaceWidth = 0;
int s_surfaceHeight = 0;

FontFamily* s_fontFamily = nullptr;
std::vector<Font*> s_fonts; // (size << 1 | bold) -> font

struct RenderSignature
{
    RECT window = {};
    RECT capsule = {};
    int dpi = 0;
    COLORREF background = 0;
    COLORREF text = 0;
    COLORREF border = 0;
    int opacity = 0;
    int fontSize = 0;
    bool bold = false;
    std::wstring content;
};

RenderSignature s_lastSignature;
bool s_hasSignature = false;

bool operator==(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

bool SignaturesEqual(const RenderSignature& a, const RenderSignature& b)
{
    return a.window == b.window && a.capsule == b.capsule && a.dpi == b.dpi &&
           a.background == b.background && a.text == b.text && a.border == b.border &&
           a.opacity == b.opacity && a.fontSize == b.fontSize && a.bold == b.bold &&
           a.content == b.content;
}

FontFamily* GetFontFamily()
{
    if (s_fontFamily != nullptr)
        return s_fontFamily;

    const wchar_t* candidates[] = {L"Microsoft YaHei UI", L"Segoe UI", L"Arial"};
    for (const wchar_t* candidate : candidates)
    {
        FontFamily* family = new FontFamily(candidate);
        if (family->GetLastStatus() == Ok && family->IsAvailable())
        {
            s_fontFamily = family;
            return s_fontFamily;
        }
        delete family;
    }

    s_fontFamily = new FontFamily(L"Arial");
    return s_fontFamily;
}

Font* GetFont(int pixelSize, bool bold)
{
    for (Font* font : s_fonts)
    {
        // Font::GetSize returns the em size in the unit used at construction.
        if (static_cast<int>(font->GetSize()) == pixelSize &&
            (font->GetStyle() & FontStyleBold) == (bold ? FontStyleBold : 0))
            return font;
    }

    FontFamily* family = GetFontFamily();
    Font* font = new Font(family, static_cast<REAL>(pixelSize),
                          bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    if (font->GetLastStatus() != Ok)
    {
        delete font;
        font = new Font(L"Arial", static_cast<REAL>(pixelSize),
                        bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    }
    s_fonts.push_back(font);
    return font;
}

GraphicsPath* CreateRoundedRectPath(const RectF& rect, REAL radius)
{
    GraphicsPath* path = new GraphicsPath();
    if (radius <= 0.0f)
    {
        path->AddRectangle(rect);
        return path;
    }

    const REAL diameter = radius * 2.0f;
    if (diameter >= rect.Width || diameter >= rect.Height)
    {
        radius = std::min(rect.Width, rect.Height) / 2.0f;
    }
    const REAL d = radius * 2.0f;

    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f,
                 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

bool EnsureSurface(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    if (s_bitmap != nullptr && s_surfaceWidth == width && s_surfaceHeight == height)
        return true;

    if (s_graphics != nullptr)
    {
        delete s_graphics;
        s_graphics = nullptr;
    }
    if (s_bitmap != nullptr)
    {
        delete s_bitmap;
        s_bitmap = nullptr;
    }
    if (s_previousDib != nullptr)
    {
        SelectObject(s_memoryDc, s_previousDib);
        s_previousDib = nullptr;
    }
    if (s_dib != nullptr)
    {
        DeleteObject(s_dib);
        s_dib = nullptr;
        s_dibPixels = nullptr;
    }
    if (s_memoryDc != nullptr)
    {
        DeleteDC(s_memoryDc);
        s_memoryDc = nullptr;
    }

    s_bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    if (s_bitmap->GetLastStatus() != Ok)
    {
        delete s_bitmap;
        s_bitmap = nullptr;
        return false;
    }

    s_graphics = new Graphics(s_bitmap);
    s_graphics->SetSmoothingMode(SmoothingModeAntiAlias);
    s_graphics->SetPixelOffsetMode(PixelOffsetModeHighQuality);
    s_graphics->SetCompositingMode(CompositingModeSourceOver);
    s_graphics->SetCompositingQuality(CompositingQualityHighQuality);
    s_graphics->SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    s_graphics->SetInterpolationMode(InterpolationModeHighQualityBicubic);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    s_memoryDc = CreateCompatibleDC(screenDc);
    ReleaseDC(nullptr, screenDc);
    if (s_memoryDc == nullptr)
        return false;

    s_dib = CreateDIBSection(s_memoryDc, &info, DIB_RGB_COLORS, &s_dibPixels,
                             nullptr, 0);
    if (s_dib == nullptr || s_dibPixels == nullptr)
        return false;
    s_previousDib = SelectObject(s_memoryDc, s_dib);

    s_surfaceWidth = width;
    s_surfaceHeight = height;
    return true;
}

void PaintFrame(const ThemePalette& palette, const std::wstring& content,
                const IslandGeometry& geometry)
{
    const int capsuleWidth = geometry.capsule.right - geometry.capsule.left;
    const int capsuleHeight = geometry.capsule.bottom - geometry.capsule.top;
    const REAL dpiScale = static_cast<REAL>(geometry.dpi) / 96.0f;

    s_graphics->Clear(Color(0, 0, 0, 0));

    const REAL capsuleX =
        static_cast<REAL>(geometry.capsule.left - geometry.window.left);
    const REAL capsuleY =
        static_cast<REAL>(geometry.capsule.top - geometry.window.top);
    const RectF capsuleRect(capsuleX, capsuleY, static_cast<REAL>(capsuleWidth),
                            static_cast<REAL>(capsuleHeight));
    const REAL radius = static_cast<REAL>(capsuleHeight) / 2.0f;

    // Soft drop shadow: a few concentric rounded rectangles with low alpha,
    // offset slightly downwards. The capsule is painted on top of it.
    const BYTE shadowBase = palette.dark ? 11 : 8;
    for (int layer = 0; layer < kShadowLayers; ++layer)
    {
        const REAL k = static_cast<REAL>(layer) / static_cast<REAL>(kShadowLayers - 1);
        const REAL grow = (1.0f - k) * 8.0f * dpiScale;
        const REAL offset = (1.0f - k) * 5.0f * dpiScale;
        const BYTE alpha = static_cast<BYTE>(std::max(1.0f, shadowBase * (0.35f + 0.75f * k)));
        const RectF shadowRect(capsuleRect.X - grow, capsuleRect.Y - grow + offset,
                               capsuleRect.Width + grow * 2.0f,
                               capsuleRect.Height + grow * 2.0f);
        GraphicsPath* path = CreateRoundedRectPath(shadowRect, radius + grow);
        SolidBrush brush(Color(alpha, 0, 0, 0));
        s_graphics->FillPath(&brush, path);
        delete path;
    }

    // Capsule background with a subtle vertical gradient for depth.
    const BYTE capsuleAlpha =
        static_cast<BYTE>(std::clamp(g_settings.opacity, 0, 100) * 255 / 100);
    const COLORREF background = palette.islandBackground;
    const COLORREF topColor = palette.dark ? BlendColor(background, RGB(255, 255, 255), 26)
                                          : BlendColor(background, RGB(255, 255, 255), 60);
    const COLORREF bottomColor = palette.dark ? BlendColor(background, RGB(0, 0, 0), 10)
                                             : BlendColor(background, RGB(0, 0, 0), 8);
    {
        LinearGradientBrush backgroundBrush(
            RectF(capsuleRect.X, capsuleRect.Y, capsuleRect.Width,
                  std::max(1.0f, capsuleRect.Height)),
            GdiColor(topColor, capsuleAlpha), GdiColor(bottomColor, capsuleAlpha),
            LinearGradientModeVertical);
        GraphicsPath* path = CreateRoundedRectPath(capsuleRect, radius);
        s_graphics->FillPath(&backgroundBrush, path);
        delete path;
    }

    // Hairline border for definition against light and dark wallpapers.
    {
        const BYTE borderAlpha = palette.dark ? 26 : 22;
        GraphicsPath* path = CreateRoundedRectPath(capsuleRect, radius);
        Pen pen(GdiColor(palette.islandBorder, borderAlpha),
                std::max(1.0f, dpiScale));
        s_graphics->DrawPath(&pen, path);
        delete path;
    }

    // Text: shrink the font until the content fits inside the capsule.
    if (!content.empty())
    {
        const REAL padding = 10.0f * dpiScale;
        RectF textRect(capsuleRect.X + padding, capsuleRect.Y,
                       std::max(1.0f, capsuleRect.Width - padding * 2.0f),
                       capsuleRect.Height);

        StringFormat format(StringFormat::GenericTypographic());
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        format.SetTrimming(StringTrimmingNone);
        format.SetFormatFlags(StringFormatFlagsNoWrap |
                              StringFormatFlagsMeasureTrailingSpaces);

        int pixelSize =
            std::max(6, static_cast<int>(std::lround(g_settings.fontSize * dpiScale)));
        Font* font = GetFont(pixelSize, g_settings.boldFont);

        // Measure against an effectively unbounded layout rect, otherwise GDI+
        // clips the measured width to the target rectangle and the shrink
        // loop below would never trigger.
        const RectF measureRect(0.0f, 0.0f, 100000.0f, textRect.Height);
        RectF bounds;
        s_graphics->MeasureString(content.c_str(), static_cast<INT>(content.length()),
                                  font, measureRect, &format, &bounds);
        const REAL verticalLimit = std::max(1.0f, capsuleRect.Height - 2.0f * dpiScale);
        while ((bounds.Width > textRect.Width || bounds.Height > verticalLimit) &&
               pixelSize > 6)
        {
            --pixelSize;
            font = GetFont(pixelSize, g_settings.boldFont);
            s_graphics->MeasureString(content.c_str(),
                                      static_cast<INT>(content.length()), font,
                                      measureRect, &format, &bounds);
        }

        SolidBrush textBrush(GdiColor(palette.islandText));
        s_graphics->DrawString(content.c_str(), static_cast<INT>(content.length()),
                               font, textRect, &format, &textBrush);
    }
}

bool PresentFrame(const IslandGeometry& geometry)
{
    const int width = geometry.window.right - geometry.window.left;
    const int height = geometry.window.bottom - geometry.window.top;

    BitmapData data = {};
    Rect lockRect(0, 0, s_surfaceWidth, s_surfaceHeight);
    if (s_bitmap->LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppARGB,
                           &data) != Ok)
        return false;

    const BYTE* source = static_cast<const BYTE*>(data.Scan0);
    BYTE* destination = static_cast<BYTE*>(s_dibPixels);
    const size_t sourceStride = static_cast<size_t>(data.Stride);
    const size_t rowBytes = static_cast<size_t>(s_surfaceWidth) * 4;
    for (int y = 0; y < s_surfaceHeight; ++y)
    {
        const BYTE* src = source + static_cast<size_t>(y) * sourceStride;
        BYTE* dst = destination + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < s_surfaceWidth; ++x)
        {
            const BYTE alpha = src[3];
            if (alpha == 255)
            {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
            }
            else if (alpha == 0)
            {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
            }
            else
            {
                dst[0] = static_cast<BYTE>(src[0] * alpha / 255);
                dst[1] = static_cast<BYTE>(src[1] * alpha / 255);
                dst[2] = static_cast<BYTE>(src[2] * alpha / 255);
            }
            dst[3] = alpha;
            src += 4;
            dst += 4;
        }
    }
    s_bitmap->UnlockBits(&data);

    POINT destinationPoint = {geometry.window.left, geometry.window.top};
    SIZE size = {width, height};
    POINT sourcePoint = {0, 0};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    return UpdateLayeredWindow(s_hwnd, nullptr, &destinationPoint, &size,
                               s_memoryDc, &sourcePoint, 0, &blend,
                               ULW_ALPHA) != FALSE;
}
} // namespace

void InitIslandRenderer(HWND hwnd)
{
    s_hwnd = hwnd;
    s_hasSignature = false;
}

void ShutdownIslandRenderer()
{
    for (Font* font : s_fonts)
        delete font;
    s_fonts.clear();

    if (s_fontFamily != nullptr)
    {
        delete s_fontFamily;
        s_fontFamily = nullptr;
    }
    if (s_graphics != nullptr)
    {
        delete s_graphics;
        s_graphics = nullptr;
    }
    if (s_bitmap != nullptr)
    {
        delete s_bitmap;
        s_bitmap = nullptr;
    }
    if (s_previousDib != nullptr)
    {
        SelectObject(s_memoryDc, s_previousDib);
        s_previousDib = nullptr;
    }
    if (s_dib != nullptr)
    {
        DeleteObject(s_dib);
        s_dib = nullptr;
        s_dibPixels = nullptr;
    }
    if (s_memoryDc != nullptr)
    {
        DeleteDC(s_memoryDc);
        s_memoryDc = nullptr;
    }
    s_surfaceWidth = 0;
    s_surfaceHeight = 0;
    s_hasSignature = false;
}

void InvalidateIslandContent()
{
    s_hasSignature = false;
}

void RenderIslandFrame(bool force)
{
    if (s_hwnd == nullptr)
        return;

    const IslandGeometry geometry =
        ComputeIslandGeometry(GetIslandHoverProgress(), GetIslandHideProgress());
    const ThemePalette& palette = GetThemePalette();
    const std::wstring content = FormatDisplayText(g_settings);

    RenderSignature signature;
    signature.window = geometry.window;
    signature.capsule = geometry.capsule;
    signature.dpi = geometry.dpi;
    signature.background = palette.islandBackground;
    signature.text = palette.islandText;
    signature.border = palette.islandBorder;
    signature.opacity = g_settings.opacity;
    signature.fontSize = g_settings.fontSize;
    signature.bold = g_settings.boldFont;
    signature.content = content;

    if (!force && s_hasSignature && SignaturesEqual(signature, s_lastSignature))
        return;

    const int width = geometry.window.right - geometry.window.left;
    const int height = geometry.window.bottom - geometry.window.top;
    if (!EnsureSurface(width, height))
        return;

    PaintFrame(palette, content, geometry);
    if (!PresentFrame(geometry))
        return;

    s_lastSignature = signature;
    s_hasSignature = true;
}
