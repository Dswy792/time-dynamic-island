#include "rendering.h"

#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "animation.h"
#include "app_state.h"
#include "color_util.h"
#include "layout.h"
#include "notifications.h"
#include "theme.h"
#include "time_format.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

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
std::vector<Font*> s_fonts;

struct SubIslandContent
{
    std::wstring title;
    std::wstring body;
};
SubIslandContent s_subContent[kNotificationSourceCount];
int s_hoveredSub = -1;

struct SourceIconCache
{
    Bitmap* bitmap = nullptr;
    bool attempted = false;
};
SourceIconCache s_icons[kNotificationSourceCount];

struct RenderSignature
{
    RECT window = {};
    RECT capsule = {};
    RECT subIslands[kNotificationSourceCount] = {};
    RECT tooltip = {};
    int dpi = 0;
    COLORREF background = 0;
    COLORREF text = 0;
    COLORREF border = 0;
    int opacity = 0;
    int fontSize = 0;
    bool bold = false;
    int hoveredSub = -1;
    bool tooltipVisible = false;
    std::wstring content;
    std::wstring subTitle[kNotificationSourceCount];
    std::wstring subBody[kNotificationSourceCount];
};

RenderSignature s_lastSignature;
bool s_hasSignature = false;

bool RectsEqual(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

bool SignaturesEqual(const RenderSignature& a, const RenderSignature& b)
{
    if (!RectsEqual(a.window, b.window) || !RectsEqual(a.capsule, b.capsule) ||
        !RectsEqual(a.tooltip, b.tooltip))
        return false;
    for (int i = 0; i < kNotificationSourceCount; ++i)
    {
        if (!RectsEqual(a.subIslands[i], b.subIslands[i]) ||
            a.subTitle[i] != b.subTitle[i] || a.subBody[i] != b.subBody[i])
            return false;
    }
    return a.dpi == b.dpi && a.background == b.background && a.text == b.text &&
           a.border == b.border && a.opacity == b.opacity &&
           a.fontSize == b.fontSize && a.bold == b.bold &&
           a.hoveredSub == b.hoveredSub && a.tooltipVisible == b.tooltipVisible &&
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
        radius = std::min(rect.Width, rect.Height) / 2.0f;
    const REAL d = radius * 2.0f;

    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f,
                 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

// ---------------------------------------------------------------------------
// Source icons (real WeChat / QQ logos read from the installed applications)
// ---------------------------------------------------------------------------
const wchar_t* SourceDisplayName(int sourceIndex)
{
    return sourceIndex == 1 ? L"QQ" : L"微信";
}

COLORREF SourceAccent(int sourceIndex)
{
    return sourceIndex == 1 ? RGB(18, 183, 245) : RGB(7, 193, 96);
}

const wchar_t* SourceGlyph(int sourceIndex)
{
    return sourceIndex == 1 ? L"Q" : L"微";
}

bool FindAppPath(const wchar_t* exeName, std::wstring& path)
{
    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    const wchar_t* subKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths";
    for (HKEY root : roots)
    {
        std::wstring full = subKey;
        full += L"\\";
        full += exeName;
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, full.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        wchar_t buffer[MAX_PATH * 2] = {};
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        const LONG result = RegQueryValueExW(key, nullptr, nullptr, &type,
                                             reinterpret_cast<LPBYTE>(buffer), &size);
        RegCloseKey(key);
        if (result == ERROR_SUCCESS && type == REG_SZ && buffer[0] != L'\0')
        {
            path = buffer;
            return true;
        }
    }
    return false;
}

bool SourceExecutable(int sourceIndex, std::wstring& path)
{
    if (sourceIndex == 0)
    {
        if (FindAppPath(L"Weixin.exe", path) || FindAppPath(L"WeChat.exe", path))
            return true;
        const wchar_t* candidates[] = {
            L"C:\\Program Files\\Tencent\\Weixin\\Weixin.exe",
            L"C:\\Program Files (x86)\\Tencent\\Weixin\\Weixin.exe",
            L"C:\\Program Files\\Tencent\\WeChat\\WeChat.exe",
            L"C:\\Program Files (x86)\\Tencent\\WeChat\\WeChat.exe",
            L"D:\\Program Files\\Tencent\\Weixin\\Weixin.exe",
            L"D:\\Program Files\\Tencent\\WeChat\\WeChat.exe",
        };
        for (const wchar_t* candidate : candidates)
        {
            if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES)
            {
                path = candidate;
                return true;
            }
        }
        return false;
    }

    if (FindAppPath(L"QQ.exe", path))
        return true;
    const wchar_t* candidates[] = {
        L"C:\\Program Files\\Tencent\\QQNT\\QQ.exe",
        L"C:\\Program Files (x86)\\Tencent\\QQNT\\QQ.exe",
        L"C:\\Program Files (x86)\\Tencent\\QQ\\Bin\\QQ.exe",
        L"D:\\Program Files\\Tencent\\QQNT\\QQ.exe",
    };
    for (const wchar_t* candidate : candidates)
    {
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES)
        {
            path = candidate;
            return true;
        }
    }
    return false;
}

typedef UINT(WINAPI* PrivateExtractIconsFn)(LPCWSTR, int, int, int, HICON*, UINT*,
                                            UINT, UINT);

Bitmap* LoadSourceIcon(int sourceIndex)
{
    SourceIconCache& cache = s_icons[sourceIndex];
    if (cache.attempted)
        return cache.bitmap;
    cache.attempted = true;

    std::wstring exePath;
    if (!SourceExecutable(sourceIndex, exePath))
        return nullptr;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr)
        return nullptr;
    auto privateExtract = reinterpret_cast<PrivateExtractIconsFn>(
        GetProcAddress(user32, "PrivateExtractIconsW"));
    if (privateExtract == nullptr)
        return nullptr;

    HICON icon = nullptr;
    UINT iconId = 0;
    if (privateExtract(exePath.c_str(), 0, 256, 256, &icon, &iconId, 1, 0) == 0 ||
        icon == nullptr)
        return nullptr;

    cache.bitmap = Bitmap::FromHICON(icon);
    DestroyIcon(icon);
    if (cache.bitmap != nullptr && cache.bitmap->GetLastStatus() != Ok)
    {
        delete cache.bitmap;
        cache.bitmap = nullptr;
    }
    return cache.bitmap;
}

// ---------------------------------------------------------------------------
// Drawing primitives shared by the island and the settings preview
// ---------------------------------------------------------------------------
void DrawSoftShadow(Graphics& graphics, const RectF& shapeRect, REAL radius,
                    const ThemePalette& palette, REAL dpiScale)
{
    const BYTE shadowBase = palette.dark ? 11 : 8;
    for (int layer = 0; layer < kShadowLayers; ++layer)
    {
        const REAL k =
            static_cast<REAL>(layer) / static_cast<REAL>(kShadowLayers - 1);
        const REAL grow = (1.0f - k) * 8.0f * dpiScale;
        const REAL offset = (1.0f - k) * 5.0f * dpiScale;
        const BYTE alpha =
            static_cast<BYTE>(std::max(1.0f, shadowBase * (0.35f + 0.75f * k)));
        const RectF rect(shapeRect.X - grow, shapeRect.Y - grow + offset,
                         shapeRect.Width + grow * 2.0f,
                         shapeRect.Height + grow * 2.0f);
        GraphicsPath* path = CreateRoundedRectPath(rect, radius + grow);
        SolidBrush brush(Color(alpha, 0, 0, 0));
        graphics.FillPath(&brush, path);
        delete path;
    }
}

void FillCapsule(Graphics& graphics, const RectF& rect, REAL radius,
                 const ThemePalette& palette, int opacityPercent)
{
    const BYTE alpha = static_cast<BYTE>(std::clamp(opacityPercent, 0, 100) * 255 / 100);
    const COLORREF background = palette.islandBackground;
    const COLORREF topColor =
        palette.dark ? BlendColor(background, RGB(255, 255, 255), 26)
                     : BlendColor(background, RGB(255, 255, 255), 60);
    const COLORREF bottomColor =
        palette.dark ? BlendColor(background, RGB(0, 0, 0), 10)
                     : BlendColor(background, RGB(0, 0, 0), 8);
    LinearGradientBrush brush(
        RectF(rect.X, rect.Y, rect.Width, std::max(1.0f, rect.Height)),
        GdiColor(topColor, alpha), GdiColor(bottomColor, alpha),
        LinearGradientModeVertical);
    GraphicsPath* path = CreateRoundedRectPath(rect, radius);
    graphics.FillPath(&brush, path);
    delete path;
}

void StrokeCapsule(Graphics& graphics, const RectF& rect, REAL radius,
                   const ThemePalette& palette, REAL dpiScale)
{
    const BYTE borderAlpha = palette.dark ? 26 : 22;
    GraphicsPath* path = CreateRoundedRectPath(rect, radius);
    Pen pen(GdiColor(palette.islandBorder, borderAlpha), std::max(1.0f, dpiScale));
    graphics.DrawPath(&pen, path);
    delete path;
}

void DrawFittedText(Graphics& graphics, const std::wstring& text,
                    const RectF& textRect, const ThemePalette& palette,
                    REAL dpiScale, int baseFontSize, bool bold, BYTE alpha,
                    REAL verticalLimit)
{
    if (text.empty() || alpha == 0 || textRect.Width <= 1.0f)
        return;

    StringFormat format(StringFormat::GenericTypographic());
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingNone);
    format.SetFormatFlags(StringFormatFlagsNoWrap |
                          StringFormatFlagsMeasureTrailingSpaces);

    int pixelSize = std::max(6, static_cast<int>(std::lround(baseFontSize * dpiScale)));
    Font* font = GetFont(pixelSize, bold);

    const RectF measureRect(0.0f, 0.0f, 100000.0f, textRect.Height);
    RectF bounds;
    graphics.MeasureString(text.c_str(), static_cast<INT>(text.length()), font,
                           measureRect, &format, &bounds);
    while ((bounds.Width > textRect.Width || bounds.Height > verticalLimit) &&
           pixelSize > 6)
    {
        --pixelSize;
        font = GetFont(pixelSize, bold);
        graphics.MeasureString(text.c_str(), static_cast<INT>(text.length()), font,
                               measureRect, &format, &bounds);
    }

    SolidBrush brush(GdiColor(palette.islandText, alpha));
    graphics.DrawString(text.c_str(), static_cast<INT>(text.length()), font,
                        textRect, &format, &brush);
}

void DrawClockContent(Graphics& graphics, const RectF& capsuleRect,
                      const ThemePalette& palette, const AppSettings& settings,
                      REAL dpiScale, const std::wstring& text)
{
    if (text.empty())
        return;
    const REAL padding = 10.0f * dpiScale;
    RectF textRect(capsuleRect.X + padding, capsuleRect.Y,
                   std::max(1.0f, capsuleRect.Width - padding * 2.0f),
                   capsuleRect.Height);
    DrawFittedText(graphics, text, textRect, palette, dpiScale,
                   settings.fontSize, settings.boldFont, 255,
                   std::max(1.0f, capsuleRect.Height - 2.0f * dpiScale));
}

// ---------------------------------------------------------------------------
// Sub-island rendering
// ---------------------------------------------------------------------------
void DrawSubIsland(Graphics& graphics, const RectF& circleRect, int sourceIndex,
                   const ThemePalette& palette, int opacityPercent,
                   REAL dpiScale, bool hovered)
{
    if (circleRect.Width < 2.0f || circleRect.Height < 2.0f)
        return;

    const REAL radius = circleRect.Width / 2.0f;
    DrawSoftShadow(graphics, circleRect, radius, palette, dpiScale);

    const BYTE alpha =
        static_cast<BYTE>(std::clamp(opacityPercent, 0, 100) * 255 / 100);
    const COLORREF background = palette.islandBackground;
    const COLORREF topColor =
        palette.dark ? BlendColor(background, RGB(255, 255, 255), 26)
                     : BlendColor(background, RGB(255, 255, 255), 60);
    const COLORREF bottomColor =
        palette.dark ? BlendColor(background, RGB(0, 0, 0), 10)
                     : BlendColor(background, RGB(0, 0, 0), 8);
    {
        LinearGradientBrush brush(
            RectF(circleRect.X, circleRect.Y, circleRect.Width,
                  std::max(1.0f, circleRect.Height)),
            GdiColor(topColor, alpha), GdiColor(bottomColor, alpha),
            LinearGradientModeVertical);
        graphics.FillEllipse(&brush, circleRect);
    }

    // Circular app logo inset by ~1.5 DIP; fall back to a colored glyph.
    const REAL inset = std::max(1.5f, 1.5f * dpiScale);
    RectF logoRect(circleRect.X + inset, circleRect.Y + inset,
                   std::max(1.0f, circleRect.Width - inset * 2.0f),
                   std::max(1.0f, circleRect.Height - inset * 2.0f));
    Bitmap* icon = LoadSourceIcon(sourceIndex);
    if (icon != nullptr && icon->GetWidth() > 0)
    {
        GraphicsPath clip;
        clip.AddEllipse(logoRect);
        graphics.SetClip(&clip);
        graphics.DrawImage(icon, logoRect);
        graphics.ResetClip();
    }
    else
    {
        SolidBrush accent(GdiColor(SourceAccent(sourceIndex), alpha));
        graphics.FillEllipse(&accent, logoRect);

        const REAL glyphSize = std::max(1.0f, logoRect.Width);
        Font* glyphFont =
            GetFont(std::max(7, static_cast<int>(std::lround(glyphSize * 0.55f))), true);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush white(Color(alpha, 255, 255, 255));
        const std::wstring glyph = SourceGlyph(sourceIndex);
        graphics.DrawString(glyph.c_str(), static_cast<INT>(glyph.length()),
                            glyphFont, logoRect, &format, &white);
    }

    // Hairline border on the circle itself.
    {
        const BYTE borderAlpha = hovered ? (palette.dark ? 70 : 55)
                                         : (palette.dark ? 30 : 25);
        Pen pen(GdiColor(palette.islandBorder, borderAlpha),
                std::max(1.0f, dpiScale * (hovered ? 1.5f : 1.0f)));
        graphics.DrawEllipse(&pen, circleRect);
    }
}

std::wstring TooltipTextFor(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= kNotificationSourceCount)
        return std::wstring();

    const SubIslandContent& content = s_subContent[sourceIndex];
    if (!g_settings.notifyShowContent)
        return std::wstring(SourceDisplayName(sourceIndex)) + L" 有新消息";

    if (content.title.empty() && content.body.empty())
        return std::wstring(SourceDisplayName(sourceIndex)) + L" 有新消息";
    if (content.body.empty())
        return content.title;
    if (content.title.empty())
        return content.body;
    return content.title + L"：" + content.body;
}

void DrawTooltip(Graphics& graphics, const RectF& rect, const std::wstring& text,
                 const ThemePalette& palette, REAL dpiScale)
{
    if (rect.Width < 8.0f || rect.Height < 8.0f)
        return;
    const REAL radius = rect.Height / 2.0f;
    DrawSoftShadow(graphics, rect, radius, palette, dpiScale);
    FillCapsule(graphics, rect, radius, palette, g_settings.opacity);
    StrokeCapsule(graphics, rect, radius, palette, dpiScale);

    const REAL padding = 10.0f * dpiScale;
    RectF textRect(rect.X + padding, rect.Y,
                   std::max(1.0f, rect.Width - padding * 2.0f), rect.Height);
    Font* font = GetFont(std::max(7, static_cast<int>(std::lround(11.0f * dpiScale))),
                         false);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush brush(
        GdiColor(BlendColor(palette.islandText, palette.islandBackground, 20)));
    graphics.DrawString(text.c_str(), static_cast<INT>(text.length()), font,
                        textRect, &format, &brush);
}

// ---------------------------------------------------------------------------
// Surface management
// ---------------------------------------------------------------------------
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
    info.bmiHeader.biHeight = -height;
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

bool PresentFrame(const IslandClusterGeometry& geometry)
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

void PaintFrame(const ThemePalette& palette, const std::wstring& clockText,
                const IslandClusterGeometry& geometry)
{
    const REAL dpiScale = static_cast<REAL>(geometry.dpi) / 96.0f;
    s_graphics->Clear(Color(0, 0, 0, 0));

    const RectF capsuleRect(
        static_cast<REAL>(geometry.capsule.left - geometry.window.left),
        static_cast<REAL>(geometry.capsule.top - geometry.window.top),
        static_cast<REAL>(geometry.capsule.right - geometry.capsule.left),
        static_cast<REAL>(geometry.capsule.bottom - geometry.capsule.top));
    const REAL capsuleRadius = capsuleRect.Height / 2.0f;

    // Sub-islands first (they sit behind the clock capsule's shadow layer).
    for (int i = 0; i < kNotificationSourceCount; ++i)
    {
        if (geometry.subIslands[i].right <= geometry.subIslands[i].left)
            continue;
        const RectF circleRect(
            static_cast<REAL>(geometry.subIslands[i].left - geometry.window.left),
            static_cast<REAL>(geometry.subIslands[i].top - geometry.window.top),
            static_cast<REAL>(geometry.subIslands[i].right -
                              geometry.subIslands[i].left),
            static_cast<REAL>(geometry.subIslands[i].bottom -
                              geometry.subIslands[i].top));
        DrawSubIsland(*s_graphics, circleRect, i, palette, g_settings.opacity,
                      dpiScale, s_hoveredSub == i);
    }

    DrawSoftShadow(*s_graphics, capsuleRect, capsuleRadius, palette, dpiScale);
    FillCapsule(*s_graphics, capsuleRect, capsuleRadius, palette, g_settings.opacity);
    StrokeCapsule(*s_graphics, capsuleRect, capsuleRadius, palette, dpiScale);
    DrawClockContent(*s_graphics, capsuleRect, palette, g_settings, dpiScale,
                     clockText);

    if (s_hoveredSub >= 0 && s_hoveredSub < kNotificationSourceCount &&
        geometry.tooltip.right > geometry.tooltip.left)
    {
        const RectF tooltipRect(
            static_cast<REAL>(geometry.tooltip.left - geometry.window.left),
            static_cast<REAL>(geometry.tooltip.top - geometry.window.top),
            static_cast<REAL>(geometry.tooltip.right - geometry.tooltip.left),
            static_cast<REAL>(geometry.tooltip.bottom - geometry.tooltip.top));
        const std::wstring text = TooltipTextFor(s_hoveredSub);
        DrawTooltip(*s_graphics, tooltipRect, text, palette, dpiScale);
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void InitIslandRenderer(HWND hwnd)
{
    s_hwnd = hwnd;
    s_hasSignature = false;
}

void SetSubIslandContent(int sourceIndex, const std::wstring& title,
                         const std::wstring& body)
{
    if (sourceIndex < 0 || sourceIndex >= kNotificationSourceCount)
        return;
    s_subContent[sourceIndex].title = title;
    s_subContent[sourceIndex].body = body;
    s_hasSignature = false;
}

void SetHoveredSubIsland(int sourceIndex)
{
    if (sourceIndex < -1 || sourceIndex >= kNotificationSourceCount)
        return;
    if (s_hoveredSub == sourceIndex)
        return;
    s_hoveredSub = sourceIndex;
    s_hasSignature = false;
}

void RenderIslandPreview(Graphics& graphics, const RectF& bounds, UINT dpi,
                         const AppSettings& settings, const ThemePalette& palette)
{
    const REAL dpiScale = static_cast<REAL>(dpi == 0 ? 96u : dpi) / 96.0f;
    const int sizeIndex = std::clamp(settings.windowSize, 0, ISLAND_SIZE_COUNT - 1);
    constexpr int kBaseWidthDip[ISLAND_SIZE_COUNT] = {120, 150, 180};
    constexpr int kBaseHeightDip[ISLAND_SIZE_COUNT] = {24, 28, 32};

    const REAL width = static_cast<REAL>(kBaseWidthDip[sizeIndex]) * dpiScale;
    const REAL height = static_cast<REAL>(kBaseHeightDip[sizeIndex]) * dpiScale;
    const RectF capsuleRect(bounds.X + (bounds.Width - width) / 2.0f,
                            bounds.Y + (bounds.Height - height) / 2.0f, width,
                            height);
    const REAL radius = height / 2.0f;

    DrawSoftShadow(graphics, capsuleRect, radius, palette, dpiScale);
    FillCapsule(graphics, capsuleRect, radius, palette, settings.opacity);
    StrokeCapsule(graphics, capsuleRect, radius, palette, dpiScale);
    DrawClockContent(graphics, capsuleRect, palette, settings, dpiScale,
                     FormatDisplayText(settings));
}

void ShutdownIslandRenderer()
{
    for (Font* font : s_fonts)
        delete font;
    s_fonts.clear();

    for (SourceIconCache& cache : s_icons)
    {
        if (cache.bitmap != nullptr)
        {
            delete cache.bitmap;
            cache.bitmap = nullptr;
        }
        cache.attempted = false;
    }

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

    float subProgress[kNotificationSourceCount] = {
        GetSubIslandProgress(0), GetSubIslandProgress(1)};
    const int hoveredSub =
        (s_hoveredSub >= 0 && s_hoveredSub < kNotificationSourceCount &&
         subProgress[s_hoveredSub] > 0.5f)
            ? s_hoveredSub
            : -1;
    const bool tooltipVisible = hoveredSub >= 0;

    const IslandClusterGeometry geometry = ComputeIslandClusterGeometry(
        GetIslandHoverProgress(), GetIslandHideProgress(), subProgress,
        hoveredSub, tooltipVisible);
    const ThemePalette& palette = GetThemePalette();
    const std::wstring content = FormatDisplayText(g_settings);

    RenderSignature signature;
    signature.window = geometry.window;
    signature.capsule = geometry.capsule;
    for (int i = 0; i < kNotificationSourceCount; ++i)
    {
        signature.subIslands[i] = geometry.subIslands[i];
        signature.subTitle[i] = s_subContent[i].title;
        signature.subBody[i] = s_subContent[i].body;
    }
    signature.tooltip = geometry.tooltip;
    signature.dpi = geometry.dpi;
    signature.background = palette.islandBackground;
    signature.text = palette.islandText;
    signature.border = palette.islandBorder;
    signature.opacity = g_settings.opacity;
    signature.fontSize = g_settings.fontSize;
    signature.bold = g_settings.boldFont;
    signature.hoveredSub = hoveredSub;
    signature.tooltipVisible = tooltipVisible;
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
