// Dynamic Island - v5 custom-drawn settings window.
//
// A modeless, fully owner-drawn popup: WS_POPUP + WS_EX_LAYERED |
// WS_EX_TOOLWINDOW | WS_EX_TOPMOST.  The whole dialog is rendered with GDI+
// into a 32bpp ARGB surface, converted to premultiplied alpha and presented
// with UpdateLayeredWindow (the same technique the island itself uses).
// GDI+ is initialised once by main(); this file never calls
// GdiplusStartup/GdiplusShutdown.
//
// All edits are accumulated in a local AppSettings copy (m_pending) so the
// preview reflects them immediately while the application only sees them when
// "Apply"/"OK" is pressed.

#include "settings_dialog.h"

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "app_state.h"
#include "color_util.h"
#include "island.h"
#include "settings.h"
#include "theme.h"
#include "time_format.h"
#include "version.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace
{
// ---------------------------------------------------------------------------
// Geometry (device independent pixels, 96 DPI)
// ---------------------------------------------------------------------------
constexpr wchar_t kSettingsWindowClass[] = L"DynamicIslandSettingsClass";

constexpr float kWindowWidthDip = 600.0f;
constexpr float kWindowHeightDip = 630.0f;
constexpr float kShadowDip = 18.0f;
constexpr float kPanelWidthDip = kWindowWidthDip - kShadowDip * 2.0f;   // 564
constexpr float kPanelHeightDip = kWindowHeightDip - kShadowDip * 2.0f; // 594
constexpr float kPanelRadiusDip = 18.0f;
constexpr float kCardRadiusDip = 12.0f;
constexpr float kTitleBarHeightDip = 46.0f;
constexpr float kContentMarginDip = 18.0f;
constexpr float kColumnGapDip = 14.0f;
constexpr float kColumnWidthDip =
    (kPanelWidthDip - kContentMarginDip * 2.0f - kColumnGapDip) * 0.5f; // 257

constexpr float kPreviewTopDip = 54.0f;
constexpr float kPreviewHeightDip = 84.0f;
constexpr float kCardGapDip = 12.0f;
constexpr float kContentTopDip =
    kPreviewTopDip + kPreviewHeightDip + kCardGapDip; // 150

constexpr float kClockCardHeightDip = 126.0f;
constexpr float kAppearanceCardHeightDip = 186.0f;
constexpr float kDateCardHeightDip = 170.0f;
constexpr float kBehaviorCardHeightDip = 216.0f;

constexpr float kCardPaddingDip = 12.0f;
constexpr float kRowsTopDip = 31.0f;
constexpr float kRowHeightDip = 26.0f;
constexpr float kRowGapDip = 3.0f;
constexpr float kDateRowHeightDip = 24.0f;
constexpr float kDateRowGapDip = 2.0f;
constexpr float kDateFormatGapDip = 6.0f;
constexpr float kDateFormatPillGapDip = 4.0f;
constexpr float kDateFormatGridHeightDip = 46.0f;

constexpr float kSwitchWidthDip = 40.0f;
constexpr float kSwitchHeightDip = 22.0f;
constexpr float kStepperWidthDip = 108.0f;
constexpr float kStepperHeightDip = 24.0f;
constexpr float kStepperButtonWidthDip = 30.0f;
constexpr float kPillGroupWidthDip = 156.0f;
constexpr float kPillHeightDip = 24.0f;
constexpr float kSegmentGapDip = 2.0f;

constexpr float kButtonWidthDip = 88.0f;
constexpr float kButtonHeightDip = 30.0f;
constexpr float kButtonGapDip = 10.0f;
constexpr float kFooterTopDip = 548.0f;
constexpr float kResetRowHeightDip = 20.0f;
constexpr float kResetButtonWidthDip = 84.0f;

constexpr float kCloseButtonSizeDip = 28.0f;
constexpr float kCloseButtonMarginDip = 14.0f;

constexpr UINT_PTR kPreviewTimerId = 1;
constexpr int kMaxWindowWidthPx = 12000;
constexpr int kMaxWindowHeightPx = 12000;

// ---------------------------------------------------------------------------
// Control identifiers
// ---------------------------------------------------------------------------
enum ControlId
{
    kIdNone = 0,

    kIdClose,
    kIdCancel,
    kIdApply,
    kIdOk,

    kIdUse24Hour,
    kIdShowSeconds,
    kIdShowAmPm,

    kIdThemeDark,
    kIdThemeLight,
    kIdThemeSystem,
    kIdSizeSmall,
    kIdSizeMedium,
    kIdSizeLarge,
    kIdFontMinus,
    kIdFontPlus,
    kIdBold,
    kIdOpacityMinus,
    kIdOpacityPlus,

    kIdShowDate,
    kIdShowYear,
    kIdShowWeekday,
    kIdDateMmdd,
    kIdDateDdmm,
    kIdDateYmd,
    kIdDateCn,

    kIdAutoHide,
    kIdDelayMinus,
    kIdDelayPlus,
    kIdHideFullscreen,
    kIdAutoStart,
    kIdPosLeft,
    kIdPosCenter,
    kIdPosRight,
    kIdResetPosition,
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
int RoundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

int ClampIntValue(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

std::wstring ToWString(int value)
{
    return std::to_wstring(value);
}

RECT ToPixelRect(const RectF& rect)
{
    RECT result = {};
    result.left = static_cast<LONG>(std::lround(rect.X));
    result.top = static_cast<LONG>(std::lround(rect.Y));
    result.right = static_cast<LONG>(std::lround(rect.X + rect.Width));
    result.bottom = static_cast<LONG>(std::lround(rect.Y + rect.Height));
    return result;
}

RectF InsetRectF(const RectF& rect, float inset)
{
    return RectF(rect.X + inset, rect.Y + inset, rect.Width - inset * 2.0f,
                 rect.Height - inset * 2.0f);
}

void AddRoundedRectPath(GraphicsPath& path, const RectF& rect, float radius)
{
    const float maximumRadius = std::min(rect.Width, rect.Height) * 0.5f;
    const float clamped = std::clamp(radius, 0.0f, maximumRadius);
    if (clamped <= 0.5f)
    {
        path.AddRectangle(rect);
        return;
    }

    const float diameter = clamped * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0f,
                90.0f);
    path.AddArc(rect.X + rect.Width - diameter,
                rect.Y + rect.Height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0f,
                90.0f);
    path.CloseFigure();
}

// Colors derived from the resolved theme palette.
COLORREF DisabledTextColor(const ThemePalette& palette)
{
    return BlendColor(palette.panelText, palette.panelSurface, 145);
}

// Background of "sunken" widgets (segmented pill containers, steppers). In
// light mode panelSurface (white) and panelBackground are almost identical, so
// fall back to the hover shade to keep the control clearly visible.
COLORREF RecessedColor(const ThemePalette& palette)
{
    if (palette.dark)
        return BlendColor(palette.panelSurface, palette.panelBackground, 150);
    return palette.panelSurfaceHover;
}

// Off-state toggle track: must read clearly against the card surface in both
// themes (a mid grey on dark cards, a clearly darker grey on white cards).
COLORREF SwitchOffColor(const ThemePalette& palette)
{
    return palette.dark ? RGB(74, 74, 84) : RGB(196, 196, 206);
}

COLORREF SwitchOffBorderColor(const ThemePalette& palette)
{
    return BlendColor(SwitchOffColor(palette),
                      palette.dark ? RGB(255, 255, 255) : RGB(0, 0, 0), 30);
}

COLORREF AccentHoverColor(const ThemePalette& palette)
{
    return BlendColor(palette.accent, palette.dark ? RGB(255, 255, 255) : RGB(0, 0, 0),
                      24);
}

COLORREF AccentPressedColor(const ThemePalette& palette)
{
    return BlendColor(palette.accent, RGB(0, 0, 0), 32);
}

// Font family used by the whole window. Prefers a family with CJK coverage so
// the Chinese labels render even when the Latin UI font is "Segoe UI".
FontFamily* GetUiFontFamily()
{
    static FontFamily* family = nullptr;
    if (family != nullptr)
        return family;

    const wchar_t* candidates[] = {L"Microsoft YaHei UI", L"Microsoft YaHei",
                                   L"Segoe UI", L"Arial"};
    for (const wchar_t* candidate : candidates)
    {
        FontFamily* candidateFamily = new FontFamily(candidate);
        if (candidateFamily->GetLastStatus() == Ok && candidateFamily->IsAvailable())
        {
            family = candidateFamily;
            return family;
        }
        delete candidateFamily;
    }

    family = new FontFamily(L"Arial");
    return family;
}

class FontCache
{
public:
    FontCache() = default;
    ~FontCache() { Clear(); }

    FontCache(const FontCache&) = delete;
    FontCache& operator=(const FontCache&) = delete;

    void Clear()
    {
        for (Font* font : m_fonts)
            delete font;
        m_fonts.clear();
    }

    Font* Get(int pixelSize, bool bold)
    {
        pixelSize = std::max(4, pixelSize);
        const bool wantBold = bold;
        for (Font* font : m_fonts)
        {
            const bool fontBold = (font->GetStyle() & FontStyleBold) != 0;
            if (static_cast<int>(std::lround(font->GetSize())) == pixelSize &&
                fontBold == wantBold)
                return font;
        }

        Font* font = new Font(GetUiFontFamily(), static_cast<REAL>(pixelSize),
                              wantBold ? FontStyleBold : FontStyleRegular, UnitPixel);
        if (font->GetLastStatus() != Ok)
        {
            delete font;
            font = new Font(L"Arial", static_cast<REAL>(pixelSize),
                            wantBold ? FontStyleBold : FontStyleRegular, UnitPixel);
        }
        m_fonts.push_back(font);
        return font;
    }

private:
    std::vector<Font*> m_fonts;
};

// ---------------------------------------------------------------------------
// Painter: thin wrapper around GDI+ with DIP-aware helpers
// ---------------------------------------------------------------------------
class Painter
{
public:
    Painter(Graphics& graphics, FontCache& fonts, float scale,
            const ThemePalette& palette, int hoverId, int pressedId)
        : m_graphics(graphics)
        , m_fonts(fonts)
        , m_scale(scale)
        , m_palette(palette)
        , m_hoverId(hoverId)
        , m_pressedId(pressedId)
    {
    }

    Graphics& G() { return m_graphics; }
    float Scale() const { return m_scale; }
    const ThemePalette& Palette() const { return m_palette; }
    bool Hovered(int id) const { return id != kIdNone && m_hoverId == id; }
    bool Pressed(int id) const { return id != kIdNone && m_pressedId == id; }

    void Text(const std::wstring& text, const RectF& layout, float sizeDip,
              bool bold, COLORREF color, BYTE alpha = 255,
              StringAlignment horizontal = StringAlignmentNear,
              StringAlignment vertical = StringAlignmentCenter,
              bool ellipsis = false)
    {
        if (text.empty())
            return;

        Font* font = m_fonts.Get(PixelSize(sizeDip), bold);
        if (font == nullptr)
            return;

        SolidBrush brush(GdiColor(color, alpha));
        StringFormat format;
        format.SetAlignment(horizontal);
        format.SetLineAlignment(vertical);
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        if (ellipsis)
            format.SetTrimming(StringTrimmingEllipsisCharacter);
        m_graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), font,
                              layout, &format, &brush);
    }

    RectF MeasureText(const std::wstring& text, float sizeDip, bool bold)
    {
        RectF bounds;
        Font* font = m_fonts.Get(PixelSize(sizeDip), bold);
        if (font == nullptr || text.empty())
            return bounds;

        const StringFormat* typographic = StringFormat::GenericTypographic();
        StringFormat format(typographic);
        format.SetFormatFlags(static_cast<INT>(StringFormatFlagsNoWrap) |
                              static_cast<INT>(StringFormatFlagsMeasureTrailingSpaces));
        const RectF infinite(0.0f, 0.0f, 10000.0f, 1000.0f);
        m_graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), font,
                                 infinite, &format, &bounds);
        return bounds;
    }

    void FillRoundRect(const RectF& rect, float radiusPx, COLORREF color,
                       BYTE alpha = 255)
    {
        if (rect.Width <= 0.0f || rect.Height <= 0.0f)
            return;
        GraphicsPath path;
        AddRoundedRectPath(path, rect, radiusPx);
        SolidBrush brush(GdiColor(color, alpha));
        m_graphics.FillPath(&brush, &path);
    }

    void StrokeRoundRect(const RectF& rect, float radiusPx, COLORREF color,
                         BYTE alpha = 255, float widthPx = 1.0f)
    {
        if (rect.Width <= 0.0f || rect.Height <= 0.0f)
            return;
        GraphicsPath path;
        AddRoundedRectPath(path, rect, radiusPx);
        Pen pen(GdiColor(color, alpha), std::max(1.0f, widthPx));
        m_graphics.DrawPath(&pen, &path);
    }

    void FillEllipse(const RectF& rect, COLORREF color, BYTE alpha = 255)
    {
        if (rect.Width <= 0.0f || rect.Height <= 0.0f)
            return;
        SolidBrush brush(GdiColor(color, alpha));
        m_graphics.FillEllipse(&brush, rect);
    }

    void Line(const PointF& from, const PointF& to, COLORREF color, float widthDip)
    {
        Pen pen(GdiColor(color), std::max(1.0f, widthDip * m_scale));
        m_graphics.DrawLine(&pen, from, to);
    }

private:
    int PixelSize(float sizeDip) const
    {
        return std::max(4, RoundToInt(sizeDip * m_scale));
    }

    Graphics& m_graphics;
    FontCache& m_fonts;
    float m_scale;
    const ThemePalette& m_palette;
    int m_hoverId;
    int m_pressedId;
};

// ---------------------------------------------------------------------------
// Layout primitives (shared by hit testing and painting)
// ---------------------------------------------------------------------------
struct Control
{
    int id = kIdNone;
    RECT rect = {};
    bool enabled = true;
};

struct DialogLayout
{
    float scale = 1.0f;
    int width = 0;
    int height = 0;

    RectF panel;
    RectF titleBar;
    RectF closeButton;
    RectF previewCard;
    RectF clockCard;
    RectF appearanceCard;
    RectF dateCard;
    RectF behaviorCard;

    RectF clockRows[3];
    RectF appearanceRows[5];
    RectF dateRows[3];
    RectF behaviorRows[5];

    RectF themePills;
    RectF sizePills;
    RectF fontStepper;
    RectF opacityStepper;
    RectF positionPills;
    RectF delayStepper;
    RectF dateGrid;
    RectF resetRow;

    RectF hint;
    RectF cancelButton;
    RectF applyButton;
    RectF okButton;

    std::vector<Control> controls;
};

RectF SegmentRectOf(const RectF& container, int index, int count, float gapPx)
{
    const float width = (container.Width - gapPx * static_cast<float>(count - 1)) /
                        static_cast<float>(count);
    return RectF(container.X + static_cast<float>(index) * (width + gapPx),
                 container.Y, width, container.Height);
}

RectF GridPillRect(const RectF& grid, int index, float gapPx)
{
    const float width = (grid.Width - gapPx) * 0.5f;
    const float height = (grid.Height - gapPx) * 0.5f;
    const int column = index % 2;
    const int row = index / 2;
    return RectF(grid.X + static_cast<float>(column) * (width + gapPx),
                 grid.Y + static_cast<float>(row) * (height + gapPx), width, height);
}

RectF StepperPartRect(const RectF& container, int index, float scale)
{
    const float buttonWidth = kStepperButtonWidthDip * scale;
    if (index == 0)
        return RectF(container.X, container.Y, buttonWidth, container.Height);
    if (index == 2)
        return RectF(container.X + container.Width - buttonWidth, container.Y,
                     buttonWidth, container.Height);
    return RectF(container.X + buttonWidth, container.Y,
                 container.Width - buttonWidth * 2.0f, container.Height);
}

RectF SwitchRectForRow(const RectF& row, float scale)
{
    const float width = kSwitchWidthDip * scale;
    const float height = kSwitchHeightDip * scale;
    return RectF(row.X + row.Width - width, row.Y + (row.Height - height) * 0.5f,
                 width, height);
}

RectF ResetButtonRect(const DialogLayout& layout, float scale)
{
    const float width = kResetButtonWidthDip * scale;
    return RectF(layout.resetRow.X + layout.resetRow.Width - width,
                 layout.resetRow.Y, width, layout.resetRow.Height);
}

// ---------------------------------------------------------------------------
// Widget drawing
// ---------------------------------------------------------------------------
void DrawCloseButton(Painter& painter, const RectF& rect, bool hovered,
                     bool pressed)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    const RectF circle = InsetRectF(rect, 1.0f * scale);
    if (hovered || pressed)
    {
        painter.FillEllipse(circle,
                            pressed ? palette.panelSurface : palette.panelSurfaceHover);
    }

    const float centerX = circle.X + circle.Width * 0.5f;
    const float centerY = circle.Y + circle.Height * 0.5f;
    const float arm = circle.Width * 0.16f;
    const COLORREF color = (hovered || pressed) ? palette.panelText
                                                : palette.panelTextSecondary;
    painter.Line(PointF(centerX - arm, centerY - arm),
                 PointF(centerX + arm, centerY + arm), color, 1.5f);
    painter.Line(PointF(centerX - arm, centerY + arm),
                 PointF(centerX + arm, centerY - arm), color, 1.5f);
}

void DrawSwitch(Painter& painter, const RectF& rect, bool on, bool enabled)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    const BYTE alpha = enabled ? 255 : 120;

    COLORREF track = SwitchOffColor(palette);
    if (on)
    {
        track = enabled ? palette.accent
                        : BlendColor(palette.accent, palette.panelSurface, 130);
    }

    painter.FillRoundRect(rect, rect.Height * 0.5f, track, alpha);
    if (!on)
    {
        painter.StrokeRoundRect(rect, rect.Height * 0.5f,
                                SwitchOffBorderColor(palette), alpha, 1.0f);
    }

    const float pad = 2.0f * scale;
    const float diameter = rect.Height - pad * 2.0f;
    const float knobX = on ? (rect.X + rect.Width - pad - diameter) : (rect.X + pad);
    const RectF knob(knobX, rect.Y + pad, diameter, diameter);
    const COLORREF knobColor =
        enabled ? (palette.dark ? RGB(246, 246, 250) : RGB(255, 255, 255))
                : RGB(226, 226, 232);
    painter.FillEllipse(knob, knobColor, alpha);
}

void DrawToggleRow(Painter& painter, const RectF& row, const std::wstring& label,
                   bool on, bool enabled, int id)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    if (enabled && painter.Hovered(id))
    {
        painter.FillRoundRect(InsetRectF(row, -3.0f * scale), 8.0f * scale,
                              palette.panelSurfaceHover);
    }

    const COLORREF textColor = enabled ? palette.panelText
                                       : DisabledTextColor(palette);
    const RectF labelRect(row.X, row.Y, row.Width - 54.0f * scale, row.Height);
    painter.Text(label, labelRect, 12.5f, false, textColor);
    DrawSwitch(painter, SwitchRectForRow(row, scale), on, enabled);
}

void DrawSegmentedRow(Painter& painter, const RectF& row,
                      const std::wstring& label, const RectF& container,
                      const std::vector<std::wstring>& labels, int selected,
                      const int* ids)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    const int count = static_cast<int>(labels.size());
    if (count <= 0)
        return;

    const RectF labelRect(row.X, row.Y, container.X - row.X - 8.0f * scale,
                          row.Height);
    painter.Text(label, labelRect, 12.5f, false, palette.panelText);

    painter.FillRoundRect(container, 8.0f * scale, RecessedColor(palette));

    for (int index = 0; index < count; ++index)
    {
        const RectF segment =
            SegmentRectOf(container, index, count, kSegmentGapDip * scale);
        const RectF pill = InsetRectF(segment, 2.0f * scale);
        const bool isSelected = (index == selected);
        const bool isHovered = painter.Hovered(ids[index]);

        if (isSelected)
            painter.FillRoundRect(pill, 6.0f * scale, palette.accent);
        else if (isHovered)
            painter.FillRoundRect(pill, 6.0f * scale, palette.panelSurfaceHover);

        COLORREF color = palette.panelTextSecondary;
        if (isSelected)
            color = palette.accentText;
        else if (isHovered)
            color = palette.panelText;

        painter.Text(labels[static_cast<size_t>(index)], segment, 11.5f,
                     isSelected, color, 255, StringAlignmentCenter,
                     StringAlignmentCenter, true);
    }
}

void DrawStepperGlyph(Painter& painter, const RectF& part, bool plus, bool enabled,
                      bool hovered, bool pressed)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    COLORREF color = palette.panelText;
    if (!enabled)
        color = DisabledTextColor(palette);
    else if (pressed)
        color = AccentPressedColor(palette);
    else if (hovered)
        color = palette.accent;

    const float centerX = part.X + part.Width * 0.5f;
    const float centerY = part.Y + part.Height * 0.5f;
    const float arm = 5.0f * scale;
    painter.Line(PointF(centerX - arm, centerY), PointF(centerX + arm, centerY),
                 color, 1.5f);
    if (plus)
    {
        painter.Line(PointF(centerX, centerY - arm), PointF(centerX, centerY + arm),
                     color, 1.5f);
    }
}

void DrawStepperRow(Painter& painter, const RectF& row, const RectF& container,
                    const std::wstring& label, const std::wstring& value,
                    bool minusEnabled, bool plusEnabled, int minusId, int plusId)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    const RectF labelRect(row.X, row.Y, container.X - row.X - 8.0f * scale,
                          row.Height);
    painter.Text(label, labelRect, 12.5f, false, palette.panelText);

    painter.FillRoundRect(container, 8.0f * scale, RecessedColor(palette));

    const RectF minus = StepperPartRect(container, 0, scale);
    const RectF plus = StepperPartRect(container, 2, scale);
    const RectF valueRect = StepperPartRect(container, 1, scale);

    if (minusEnabled && painter.Hovered(minusId))
    {
        painter.FillRoundRect(InsetRectF(minus, 2.0f * scale), 6.0f * scale,
                              palette.panelSurfaceHover);
    }
    if (plusEnabled && painter.Hovered(plusId))
    {
        painter.FillRoundRect(InsetRectF(plus, 2.0f * scale), 6.0f * scale,
                              palette.panelSurfaceHover);
    }

    DrawStepperGlyph(painter, minus, false, minusEnabled, painter.Hovered(minusId),
                     painter.Pressed(minusId));
    DrawStepperGlyph(painter, plus, true, plusEnabled, painter.Hovered(plusId),
                     painter.Pressed(plusId));

    painter.Text(value, valueRect, 11.5f, false, palette.panelText, 255,
                 StringAlignmentCenter, StringAlignmentCenter, true);
}

void DrawDateFormatPills(Painter& painter, const RectF& grid,
                         const std::vector<std::wstring>& labels, int selected,
                         const int* ids)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    const int count = std::min(static_cast<int>(labels.size()), 4);
    for (int index = 0; index < count; ++index)
    {
        const RectF cell =
            GridPillRect(grid, index, kDateFormatPillGapDip * scale);
        const bool isSelected = (index == selected);
        const bool isHovered = painter.Hovered(ids[index]);

        if (isSelected)
            painter.FillRoundRect(cell, 7.0f * scale, palette.accent);
        else
            painter.FillRoundRect(cell, 7.0f * scale, RecessedColor(palette));

        if (isHovered && !isSelected)
        {
            painter.FillRoundRect(cell, 7.0f * scale,
                                  palette.panelSurfaceHover);
        }

        COLORREF color = palette.panelTextSecondary;
        if (isSelected)
            color = palette.accentText;
        else if (isHovered)
            color = palette.panelText;

        painter.Text(labels[static_cast<size_t>(index)], cell, 11.5f, isSelected,
                     color, 255, StringAlignmentCenter, StringAlignmentCenter,
                     true);
    }
}

void DrawTextLink(Painter& painter, const RectF& rect, const std::wstring& label,
                  bool hovered, bool pressed)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    if (hovered || pressed)
    {
        painter.FillRoundRect(rect, 6.0f * scale,
                              pressed ? palette.panelSurface : palette.panelSurfaceHover);
    }

    const COLORREF color =
        (hovered || pressed) ? palette.accent : palette.panelTextSecondary;
    painter.Text(label, rect, 11.5f, hovered, color, 255, StringAlignmentCenter,
                 StringAlignmentCenter, false);
}

void DrawButton(Painter& painter, const RectF& rect, const std::wstring& label,
                bool accent, bool hovered, bool pressed)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    COLORREF fill = palette.panelSurface;
    COLORREF textColor = palette.panelText;
    if (accent)
    {
        fill = palette.accent;
        textColor = palette.accentText;
        if (pressed)
            fill = AccentPressedColor(palette);
        else if (hovered)
            fill = AccentHoverColor(palette);
    }
    else if (pressed)
    {
        fill = palette.panelSurface;
    }
    else if (hovered)
    {
        fill = palette.panelSurfaceHover;
    }

    painter.FillRoundRect(rect, 10.0f * scale, fill);
    if (!accent)
    {
        painter.StrokeRoundRect(rect, 10.0f * scale, palette.panelBorder, 255,
                                1.0f);
    }

    painter.Text(label, rect, 12.0f, true, textColor, 255,
                 StringAlignmentCenter, StringAlignmentCenter, false);
}

void DrawCard(Painter& painter, const RectF& card, const std::wstring& title)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    painter.FillRoundRect(card, kCardRadiusDip * scale, palette.panelSurface);
    if (!palette.dark)
    {
        // White cards on the very light panel background need a hairline to
        // separate the surfaces in light mode.
        painter.StrokeRoundRect(card, kCardRadiusDip * scale,
                                palette.panelBorder, 255, 1.0f);
    }

    const RectF dot(card.X + 12.0f * scale, card.Y + 14.0f * scale,
                    4.0f * scale, 4.0f * scale);
    painter.FillRoundRect(dot, 2.0f * scale, palette.accent);

    const RectF titleRect(card.X + 21.0f * scale, card.Y + 7.0f * scale,
                          card.Width - 30.0f * scale, 18.0f * scale);
    painter.Text(title, titleRect, 10.0f, true, palette.panelTextSecondary);
}

void DrawPanelChrome(Painter& painter, const DialogLayout& layout,
                     const std::wstring& title, const std::wstring& subtitle)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    // Soft drop shadow: concentric rounded rectangles, darkest close to the
    // panel. The transparent 18 DIP window padding holds all of it.
    const int steps = std::max(4, RoundToInt(kShadowDip * scale));
    const float peakAlpha = palette.dark ? 88.0f : 46.0f;
    for (int layer = steps; layer >= 1; --layer)
    {
        const float grow = static_cast<float>(layer);
        const float t = grow / static_cast<float>(steps + 1);
        const int alpha = RoundToInt(peakAlpha * (1.0f - t) * (1.0f - t));
        if (alpha <= 0)
            continue;

        const RectF shadow(layout.panel.X - grow,
                           layout.panel.Y - grow + 2.0f * scale,
                           layout.panel.Width + grow * 2.0f,
                           layout.panel.Height + grow * 2.0f);
        painter.FillRoundRect(shadow, kPanelRadiusDip * scale + grow, RGB(0, 0, 0),
                              static_cast<BYTE>(alpha));
    }

    painter.FillRoundRect(layout.panel, kPanelRadiusDip * scale,
                          palette.panelBackground);
    painter.StrokeRoundRect(layout.panel, kPanelRadiusDip * scale,
                            palette.panelBorder, 255, 1.0f);

    const RectF titleRect(layout.panel.X + 20.0f * scale,
                          layout.panel.Y + 6.0f * scale,
                          layout.panel.Width - 130.0f * scale, 22.0f * scale);
    painter.Text(title, titleRect, 15.0f, true, palette.panelText, 255,
                 StringAlignmentNear, StringAlignmentCenter, false);

    const RectF subtitleRect(layout.panel.X + 20.0f * scale,
                             layout.panel.Y + 28.0f * scale,
                             layout.panel.Width - 130.0f * scale, 14.0f * scale);
    painter.Text(subtitle, subtitleRect, 9.5f, false, palette.panelTextSecondary,
                 255, StringAlignmentNear, StringAlignmentCenter, true);

    DrawCloseButton(painter, layout.closeButton, painter.Hovered(kIdClose),
                    painter.Pressed(kIdClose));
}

void DrawPreviewCapsule(Painter& painter, const RectF& card,
                        const AppSettings& settings)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    std::wstring content = FormatDisplayText(settings);
    if (content.empty())
        content = L"00:00";

    const RectF area(card.X + 12.0f * scale, card.Y + 26.0f * scale,
                     card.Width - 24.0f * scale, card.Height - 36.0f * scale);
    if (area.Width <= 0.0f || area.Height <= 0.0f)
        return;

    const float maximumWidth = area.Width - 8.0f * scale;
    float fontDip = std::min(8.0f + static_cast<float>(settings.fontSize) * 0.42f,
                             21.0f);
    RectF textBounds = painter.MeasureText(content, fontDip, settings.boldFont);
    for (int attempt = 0;
         attempt < 12 && textBounds.Width > maximumWidth - 36.0f * scale &&
         fontDip > 7.0f;
         ++attempt)
    {
        fontDip -= 0.75f;
        textBounds = painter.MeasureText(content, fontDip, settings.boldFont);
    }

    const float capsuleHeight =
        std::min(std::max(36.0f * scale, fontDip * scale + 20.0f * scale),
                 area.Height);
    float capsuleWidth = std::min(textBounds.Width + 36.0f * scale, maximumWidth);
    capsuleWidth = std::max(capsuleWidth, 96.0f * scale);

    const float centerX = area.X + area.Width * 0.5f;
    const float centerY = area.Y + area.Height * 0.5f;
    const RectF capsule(centerX - capsuleWidth * 0.5f,
                        centerY - capsuleHeight * 0.5f, capsuleWidth,
                        capsuleHeight);

    const int opacity = ClampIntValue(settings.opacity, 0, 100);
    const BYTE alpha = static_cast<BYTE>(opacity * 255 / 100);

    const RectF shadow(capsule.X, capsule.Y + 2.0f * scale, capsule.Width,
                       capsule.Height);
    painter.FillRoundRect(shadow, capsule.Height * 0.5f, RGB(0, 0, 0),
                          palette.dark ? 46 : 34);

    painter.FillRoundRect(capsule, capsule.Height * 0.5f,
                          palette.islandBackground, alpha);
    // Match the real island's hairline border (dark 26 / light 22) and scale it
    // with the pending opacity; light mode uses a slightly stronger value so
    // the white capsule still reads against the white preview card.
    const int baseBorderAlpha = palette.dark ? 26 : 34;
    const BYTE borderAlpha = static_cast<BYTE>(
        std::max(6, baseBorderAlpha * static_cast<int>(alpha) / 255));
    painter.StrokeRoundRect(capsule, capsule.Height * 0.5f, palette.islandBorder,
                            borderAlpha, std::max(1.0f, scale));

    GraphicsPath clipPath;
    AddRoundedRectPath(clipPath, capsule, capsule.Height * 0.5f);
    painter.G().SetClip(&clipPath, CombineModeReplace);
    painter.Text(content, capsule, fontDip, settings.boldFont, palette.islandText,
                 alpha, StringAlignmentCenter, StringAlignmentCenter, false);
    painter.G().ResetClip();
}

// ---------------------------------------------------------------------------
// DPI / monitor helpers
// ---------------------------------------------------------------------------
UINT QueryWindowDpi(HWND hwnd)
{
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn getDpiForWindow =
        reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow != nullptr && hwnd != nullptr)
    {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi != 0)
            return dpi;
    }

    UINT dpi = 96;
    HDC dc = GetDC(nullptr);
    if (dc != nullptr)
    {
        const int value = GetDeviceCaps(dc, LOGPIXELSX);
        if (value > 0)
            dpi = static_cast<UINT>(value);
        ReleaseDC(nullptr, dc);
    }
    return dpi == 0 ? 96u : dpi;
}

RECT WorkAreaForRect(const RECT& reference)
{
    HMONITOR monitor = MonitorFromRect(&reference, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &info) != FALSE)
        return info.rcWork;

    RECT fallback = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0) != FALSE &&
        fallback.right > fallback.left && fallback.bottom > fallback.top)
    {
        return fallback;
    }

    fallback.left = 0;
    fallback.top = 0;
    fallback.right = GetSystemMetrics(SM_CXSCREEN);
    fallback.bottom = GetSystemMetrics(SM_CYSCREEN);
    if (fallback.right <= 0)
        fallback.right = 1920;
    if (fallback.bottom <= 0)
        fallback.bottom = 1080;
    return fallback;
}

float ComputeDialogScale(UINT dpi, const RECT& workArea)
{
    const float dpiScale = static_cast<float>(dpi) / 96.0f;
    const float workWidth = static_cast<float>(workArea.right - workArea.left);
    const float workHeight = static_cast<float>(workArea.bottom - workArea.top);

    // Shrink uniformly when the whole layered window (visible panel plus the
    // transparent 18 DIP shadow padding) does not fit the work area. Keeping
    // the window fully inside the work area also keeps the non click-through
    // shadow off the taskbar. Example: 1280x720 at 125% has a ~672 px work
    // height, so the dialog uses ~1.04x and the body text stays ~13 physical
    // pixels - still legible.
    const float usableWidth = workWidth - 12.0f;
    const float usableHeight = workHeight - 12.0f;

    float scale = dpiScale;
    if (usableWidth > 64.0f)
        scale = std::min(scale, usableWidth / kWindowWidthDip);
    if (usableHeight > 64.0f)
        scale = std::min(scale, usableHeight / kWindowHeightDip);

    return std::clamp(scale, 0.4f, 4.0f);
}

// ---------------------------------------------------------------------------
// Render target: GDI+ bitmap -> premultiplied 32bpp DIB -> UpdateLayeredWindow
// ---------------------------------------------------------------------------
struct RenderTarget
{
    HDC memoryDc = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ previousDib = nullptr;
    void* dibPixels = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    Bitmap* bitmap = nullptr;
    Graphics* graphics = nullptr;

    RenderTarget() = default;
    ~RenderTarget() { Destroy(); }

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool Ensure(int newWidth, int newHeight)
    {
        if (newWidth <= 0 || newHeight <= 0 || newWidth > kMaxWindowWidthPx ||
            newHeight > kMaxWindowHeightPx)
        {
            return false;
        }

        if (bitmap != nullptr && graphics != nullptr && width == newWidth &&
            height == newHeight)
        {
            return true;
        }

        Destroy();

        bitmap = new Bitmap(newWidth, newHeight, PixelFormat32bppARGB);
        if (bitmap->GetLastStatus() != Ok)
        {
            Destroy();
            return false;
        }

        graphics = new Graphics(bitmap);
        if (graphics->GetLastStatus() != Ok)
        {
            Destroy();
            return false;
        }
        graphics->SetSmoothingMode(SmoothingModeAntiAlias);
        graphics->SetPixelOffsetMode(PixelOffsetModeHighQuality);
        graphics->SetCompositingMode(CompositingModeSourceOver);
        graphics->SetCompositingQuality(CompositingQualityHighQuality);
        graphics->SetTextRenderingHint(TextRenderingHintAntiAlias);
        graphics->SetInterpolationMode(InterpolationModeHighQualityBicubic);

        HDC screenDc = GetDC(nullptr);
        memoryDc = CreateCompatibleDC(screenDc);
        if (screenDc != nullptr)
            ReleaseDC(nullptr, screenDc);
        if (memoryDc == nullptr)
        {
            Destroy();
            return false;
        }

        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = newWidth;
        info.bmiHeader.biHeight = -newHeight; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        dib = CreateDIBSection(memoryDc, &info, DIB_RGB_COLORS, &dibPixels,
                               nullptr, 0);
        if (dib == nullptr || dibPixels == nullptr)
        {
            Destroy();
            return false;
        }
        previousDib = SelectObject(memoryDc, dib);

        width = newWidth;
        height = newHeight;
        stride = newWidth * 4;
        return true;
    }

    void Destroy()
    {
        if (graphics != nullptr)
        {
            delete graphics;
            graphics = nullptr;
        }
        if (bitmap != nullptr)
        {
            delete bitmap;
            bitmap = nullptr;
        }
        if (memoryDc != nullptr && previousDib != nullptr)
        {
            SelectObject(memoryDc, previousDib);
            previousDib = nullptr;
        }
        if (dib != nullptr)
        {
            DeleteObject(dib);
            dib = nullptr;
        }
        if (memoryDc != nullptr)
        {
            DeleteDC(memoryDc);
            memoryDc = nullptr;
        }
        dibPixels = nullptr;
        width = 0;
        height = 0;
        stride = 0;
    }
};

// ---------------------------------------------------------------------------
// The settings window
// ---------------------------------------------------------------------------
class SettingsWindow
{
public:
    SettingsWindow() = default;
    ~SettingsWindow() { Close(false); }

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    HWND Handle() const { return m_hwnd; }

    bool Create(HWND owner);
    void Close(bool applyChanges);
    void Repaint();
    void ReloadPending();

private:
    static bool EnsureClass();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                       LPARAM lParam);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnDestroy();
    void UpdateMetrics(UINT dpi, const RECT& workArea);
    void RelayoutForDpi(UINT dpi, const RECT& proposedRect);
    void SnapWindowToWorkArea(int windowX, int windowY);
    void ClampToWorkArea();
    void CenterOnWorkArea();

    void BuildLayout();
    void Paint();
    void Present();
    void ApplyPending();
    void PerformAction(int id);

    void OnMouseMove(POINT point);
    void OnMouseLeave();
    void OnLButtonDown(POINT point);
    void OnLButtonUp(POINT point);
    void UpdateHover(POINT point);
    void UpdateCursor();
    void UpdateCursorForPoint(POINT point);
    void BeginTitleDrag(POINT screenPoint);
    void UpdateTitleDrag(POINT screenPoint);

    int HitTestControl(POINT clientPoint) const;
    bool IsControlEnabled(int id) const;
    bool IsPointOnTitleBar(POINT clientPoint) const;
    RECT CurrentWindowRect() const;
    RECT WorkAreaForWindow() const;

    HWND m_hwnd = nullptr;
    HWND m_owner = nullptr;
    UINT m_dpi = 96;
    float m_scale = 1.0f;
    int m_width = 0;
    int m_height = 0;

    AppSettings m_pending;
    DialogLayout m_layout;
    RenderTarget m_target;
    FontCache m_fonts;

    int m_hoverId = kIdNone;
    int m_pressedId = kIdNone;
    bool m_trackingMouse = false;
    bool m_dragging = false;
    POINT m_dragCursorStart = {};
    POINT m_dragWindowStart = {};
};

bool SettingsWindow::EnsureClass()
{
    static bool registered = false;
    if (registered)
        return true;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = 0;
    windowClass.lpfnWndProc = &SettingsWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kSettingsWindowClass;

    registered = RegisterClassExW(&windowClass) != 0;
    if (!registered && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        registered = true;
    return registered;
}

bool SettingsWindow::Create(HWND owner)
{
    if (m_hwnd != nullptr)
        return true;
    if (!EnsureClass())
        return false;

    m_owner = owner;
    m_pending = g_settings;
    m_hoverId = kIdNone;
    m_pressedId = kIdNone;
    m_trackingMouse = false;
    m_dragging = false;
    m_target.Destroy();
    m_fonts.Clear();

    const UINT dpi = QueryWindowDpi(owner);
    const RECT workArea = WorkAreaForWindow();
    m_scale = ComputeDialogScale(dpi, workArea);
    m_width = std::max(160, RoundToInt(kWindowWidthDip * m_scale));
    m_height = std::max(160, RoundToInt(kWindowHeightDip * m_scale));

    const int x = workArea.left + ((workArea.right - workArea.left) - m_width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - m_height) / 2;

    const HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSettingsWindowClass,
        L"Dynamic Island Settings", WS_POPUP, x, y, m_width, m_height, owner,
        nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd == nullptr)
    {
        m_hwnd = nullptr;
        m_target.Destroy();
        return false;
    }
    return true;
}

void SettingsWindow::Close(bool applyChanges)
{
    if (m_hwnd == nullptr)
        return;
    if (applyChanges)
        ApplyPending();
    DestroyWindow(m_hwnd);
}

void SettingsWindow::Repaint()
{
    if (m_hwnd != nullptr)
        Paint();
}

void SettingsWindow::ReloadPending()
{
    if (m_hwnd == nullptr)
        return;
    if (m_dragging || m_pressedId != kIdNone)
        return;
    m_pending = g_settings;
    Paint();
}

void SettingsWindow::ApplyPending()
{
    g_settings = m_pending;
    ValidateSettings(g_settings);
    SaveSettings();
    ApplySettingsAndRefresh();
    m_pending = g_settings;
}

void SettingsWindow::OnCreate()
{
    const RECT workArea = WorkAreaForWindow();
    UpdateMetrics(QueryWindowDpi(m_hwnd), workArea);
    CenterOnWorkArea();
    SetTimer(m_hwnd, kPreviewTimerId, 1000, nullptr);
}

void SettingsWindow::OnDestroy()
{
    if (m_hwnd != nullptr)
        KillTimer(m_hwnd, kPreviewTimerId);

    // Clear the interaction state first so that the WM_CAPTURECHANGED sent by
    // ReleaseCapture does not try to repaint the dying window.
    m_dragging = false;
    m_pressedId = kIdNone;
    m_hoverId = kIdNone;
    m_trackingMouse = false;
    if (m_hwnd != nullptr && GetCapture() == m_hwnd)
        ReleaseCapture();

    m_target.Destroy();
    m_fonts.Clear();
    m_layout.controls.clear();

    m_hwnd = nullptr; // owned window: never PostQuitMessage from here
}

void SettingsWindow::UpdateMetrics(UINT dpi, const RECT& workArea)
{
    m_dpi = dpi == 0 ? 96u : dpi;
    m_scale = ComputeDialogScale(m_dpi, workArea);
    m_width = std::max(160, RoundToInt(kWindowWidthDip * m_scale));
    m_height = std::max(160, RoundToInt(kWindowHeightDip * m_scale));
    m_target.Destroy();
}

void SettingsWindow::RelayoutForDpi(UINT dpi, const RECT& proposedRect)
{
    if (m_hwnd == nullptr)
        return;

    const RECT workArea = WorkAreaForRect(proposedRect);
    UpdateMetrics(dpi, workArea);

    SetWindowPos(m_hwnd, HWND_TOPMOST, proposedRect.left, proposedRect.top,
                 m_width, m_height, SWP_NOACTIVATE);
    SnapWindowToWorkArea(proposedRect.left, proposedRect.top);
    Paint();
}

// Keep the window on screen. Normally the whole layered window is clamped into
// the monitor work area. Only if the window is physically larger than the work
// area (extremely small screens) fall back to keeping the visible panel inside
// it, so the content can never be pushed off screen.
void SettingsWindow::SnapWindowToWorkArea(int windowX, int windowY)
{
    if (m_hwnd == nullptr)
        return;

    const RECT proposed = {windowX, windowY, windowX + m_width,
                           windowY + m_height};
    const RECT workArea = WorkAreaForRect(proposed);
    const int workWidth = workArea.right - workArea.left;
    const int workHeight = workArea.bottom - workArea.top;

    int x = windowX;
    if (m_width <= workWidth)
    {
        if (x + m_width > workArea.right)
            x = workArea.right - m_width;
        if (x < workArea.left)
            x = workArea.left;
    }
    else
    {
        const int panelWidth = RoundToInt(kPanelWidthDip * m_scale);
        const int paddingX = (m_width - panelWidth) / 2;
        int panelX = x + paddingX;
        if (panelX + panelWidth > workArea.right)
            panelX = workArea.right - panelWidth;
        if (panelX < workArea.left)
            panelX = workArea.left;
        x = panelX - paddingX;
    }

    int y = windowY;
    if (m_height <= workHeight)
    {
        if (y + m_height > workArea.bottom)
            y = workArea.bottom - m_height;
        if (y < workArea.top)
            y = workArea.top;
    }
    else
    {
        const int panelHeight = RoundToInt(kPanelHeightDip * m_scale);
        const int paddingY = (m_height - panelHeight) / 2;
        int panelY = y + paddingY;
        if (panelY + panelHeight > workArea.bottom)
            panelY = workArea.bottom - panelHeight;
        if (panelY < workArea.top)
            panelY = workArea.top;
        y = panelY - paddingY;
    }

    const RECT current = CurrentWindowRect();
    if (x == current.left && y == current.top)
        return;
    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::ClampToWorkArea()
{
    if (m_hwnd == nullptr)
        return;

    const RECT current = CurrentWindowRect();
    if (current.right - current.left <= 0 || current.bottom - current.top <= 0)
        return;

    SnapWindowToWorkArea(current.left, current.top);
}

void SettingsWindow::CenterOnWorkArea()
{
    if (m_hwnd == nullptr)
        return;

    const RECT workArea = WorkAreaForWindow();
    const int x =
        workArea.left + ((workArea.right - workArea.left) - m_width) / 2;
    const int y =
        workArea.top + ((workArea.bottom - workArea.top) - m_height) / 2;

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_width, m_height, SWP_NOACTIVATE);
    SnapWindowToWorkArea(x, y);
}

RECT SettingsWindow::CurrentWindowRect() const
{
    RECT rect = {};
    if (m_hwnd != nullptr)
        GetWindowRect(m_hwnd, &rect);
    return rect;
}

RECT SettingsWindow::WorkAreaForWindow() const
{
    if (m_hwnd != nullptr)
        return WorkAreaForRect(CurrentWindowRect());
    if (m_owner != nullptr)
    {
        RECT ownerRect = {};
        if (GetWindowRect(m_owner, &ownerRect) != FALSE)
            return WorkAreaForRect(ownerRect);
    }

    POINT cursor = {};
    GetCursorPos(&cursor);
    RECT cursorRect = {cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    return WorkAreaForRect(cursorRect);
}

void SettingsWindow::BuildLayout()
{
    DialogLayout& layout = m_layout;
    const float scale = m_scale;

    layout.scale = scale;
    layout.width = m_width;
    layout.height = m_height;
    layout.controls.clear();
    layout.controls.reserve(40);

    const float panelWidth = kPanelWidthDip * scale;
    const float panelHeight = kPanelHeightDip * scale;
    const float originX = (static_cast<float>(m_width) - panelWidth) * 0.5f;
    const float originY = (static_cast<float>(m_height) - panelHeight) * 0.5f;

    const auto PointX = [&](float dip) { return originX + dip * scale; };
    const auto PointY = [&](float dip) { return originY + dip * scale; };
    const auto PanelRect = [&](float x, float y, float width, float height) {
        return RectF(PointX(x), PointY(y), width * scale, height * scale);
    };
    const auto CardRow = [&](const RectF& card, float topDip, float heightDip) {
        return RectF(card.X + kCardPaddingDip * scale, card.Y + topDip * scale,
                     card.Width - kCardPaddingDip * 2.0f * scale,
                     heightDip * scale);
    };
    const auto RightAligned = [&](const RectF& row, float widthDip,
                                  float heightDip) {
        const float width = widthDip * scale;
        const float height = heightDip * scale;
        return RectF(row.X + row.Width - width,
                     row.Y + (row.Height - height) * 0.5f, width, height);
    };
    const auto AddControl = [&](int id, const RectF& rect, bool enabled) {
        Control control;
        control.id = id;
        control.rect = ToPixelRect(rect);
        control.enabled = enabled;
        layout.controls.push_back(control);
    };

    layout.panel = PanelRect(0.0f, 0.0f, kPanelWidthDip, kPanelHeightDip);
    layout.titleBar = PanelRect(0.0f, 0.0f, kPanelWidthDip, kTitleBarHeightDip);
    layout.closeButton = PanelRect(
        kPanelWidthDip - kCloseButtonMarginDip - kCloseButtonSizeDip,
        (kTitleBarHeightDip - kCloseButtonSizeDip) * 0.5f, kCloseButtonSizeDip,
        kCloseButtonSizeDip);

    const float leftX = kContentMarginDip;
    const float rightX = kContentMarginDip + kColumnWidthDip + kColumnGapDip;

    layout.previewCard = PanelRect(kContentMarginDip, kPreviewTopDip,
                                   kPanelWidthDip - kContentMarginDip * 2.0f,
                                   kPreviewHeightDip);
    layout.clockCard = PanelRect(leftX, kContentTopDip, kColumnWidthDip,
                                 kClockCardHeightDip);
    layout.appearanceCard =
        PanelRect(leftX, kContentTopDip + kClockCardHeightDip + kCardGapDip,
                  kColumnWidthDip, kAppearanceCardHeightDip);
    layout.dateCard = PanelRect(rightX, kContentTopDip, kColumnWidthDip,
                                kDateCardHeightDip);
    layout.behaviorCard =
        PanelRect(rightX, kContentTopDip + kDateCardHeightDip + kCardGapDip,
                  kColumnWidthDip, kBehaviorCardHeightDip);

    for (int index = 0; index < 3; ++index)
    {
        layout.clockRows[index] =
            CardRow(layout.clockCard,
                    kRowsTopDip + static_cast<float>(index) *
                                      (kRowHeightDip + kRowGapDip),
                    kRowHeightDip);
        layout.dateRows[index] =
            CardRow(layout.dateCard,
                    kRowsTopDip + static_cast<float>(index) *
                                      (kDateRowHeightDip + kDateRowGapDip),
                    kDateRowHeightDip);
    }
    for (int index = 0; index < 5; ++index)
    {
        layout.appearanceRows[index] =
            CardRow(layout.appearanceCard,
                    kRowsTopDip + static_cast<float>(index) *
                                      (kRowHeightDip + kRowGapDip),
                    kRowHeightDip);
        layout.behaviorRows[index] =
            CardRow(layout.behaviorCard,
                    kRowsTopDip + static_cast<float>(index) *
                                      (kRowHeightDip + kRowGapDip),
                    kRowHeightDip);
    }

    layout.themePills =
        RightAligned(layout.appearanceRows[0], kPillGroupWidthDip, kPillHeightDip);
    layout.sizePills =
        RightAligned(layout.appearanceRows[1], kPillGroupWidthDip, kPillHeightDip);
    layout.fontStepper = RightAligned(layout.appearanceRows[2], kStepperWidthDip,
                                      kStepperHeightDip);
    layout.opacityStepper = RightAligned(layout.appearanceRows[4],
                                         kStepperWidthDip, kStepperHeightDip);
    layout.delayStepper = RightAligned(layout.behaviorRows[1], kStepperWidthDip,
                                       kStepperHeightDip);
    layout.positionPills =
        RightAligned(layout.behaviorRows[4], kPillGroupWidthDip, kPillHeightDip);

    const float dateGridTop =
        kRowsTopDip + 3.0f * kDateRowHeightDip + 2.0f * kDateRowGapDip +
        kDateFormatGapDip;
    layout.dateGrid = CardRow(layout.dateCard, dateGridTop,
                              kDateFormatGridHeightDip);

    const float resetTop =
        kRowsTopDip + 5.0f * kRowHeightDip + 4.0f * kRowGapDip + 6.0f;
    layout.resetRow =
        CardRow(layout.behaviorCard, resetTop, kResetRowHeightDip);

    const float buttonY =
        kFooterTopDip +
        (kPanelHeightDip - kFooterTopDip - kButtonHeightDip) * 0.5f;
    const float rightEdge = kPanelWidthDip - kContentMarginDip;
    const float okX = rightEdge - kButtonWidthDip;
    const float applyX = okX - kButtonGapDip - kButtonWidthDip;
    const float cancelX = applyX - kButtonGapDip - kButtonWidthDip;
    layout.okButton = PanelRect(okX, buttonY, kButtonWidthDip, kButtonHeightDip);
    layout.applyButton =
        PanelRect(applyX, buttonY, kButtonWidthDip, kButtonHeightDip);
    layout.cancelButton =
        PanelRect(cancelX, buttonY, kButtonWidthDip, kButtonHeightDip);
    layout.hint =
        PanelRect(kContentMarginDip, buttonY,
                  cancelX - kContentMarginDip - kContentMarginDip,
                  kButtonHeightDip);

    // -- controls -----------------------------------------------------------
    AddControl(kIdClose, layout.closeButton, true);

    AddControl(kIdUse24Hour, layout.clockRows[0], true);
    AddControl(kIdShowSeconds, layout.clockRows[1], true);
    AddControl(kIdShowAmPm, layout.clockRows[2], !m_pending.use24Hour);

    const int themeIds[3] = {kIdThemeDark, kIdThemeLight, kIdThemeSystem};
    for (int index = 0; index < 3; ++index)
    {
        AddControl(themeIds[index],
                   SegmentRectOf(layout.themePills, index, 3,
                                 kSegmentGapDip * scale),
                   true);
    }

    const int sizeIds[3] = {kIdSizeSmall, kIdSizeMedium, kIdSizeLarge};
    for (int index = 0; index < 3; ++index)
    {
        AddControl(sizeIds[index],
                   SegmentRectOf(layout.sizePills, index, 3,
                                 kSegmentGapDip * scale),
                   true);
    }

    AddControl(kIdFontMinus, StepperPartRect(layout.fontStepper, 0, scale),
               m_pending.fontSize > kFontSizeMin);
    AddControl(kIdFontPlus, StepperPartRect(layout.fontStepper, 2, scale),
               m_pending.fontSize < kFontSizeMax);
    AddControl(kIdBold, layout.appearanceRows[3], true);
    AddControl(kIdOpacityMinus, StepperPartRect(layout.opacityStepper, 0, scale),
               m_pending.opacity > kOpacityMin);
    AddControl(kIdOpacityPlus, StepperPartRect(layout.opacityStepper, 2, scale),
               m_pending.opacity < kOpacityMax);

    AddControl(kIdShowDate, layout.dateRows[0], true);
    AddControl(kIdShowYear, layout.dateRows[1], true);
    AddControl(kIdShowWeekday, layout.dateRows[2], true);

    const int dateIds[4] = {kIdDateMmdd, kIdDateDdmm, kIdDateYmd, kIdDateCn};
    for (int index = 0; index < 4; ++index)
    {
        AddControl(dateIds[index],
                   GridPillRect(layout.dateGrid, index,
                                kDateFormatPillGapDip * scale),
                   true);
    }

    AddControl(kIdAutoHide, layout.behaviorRows[0], true);
    AddControl(kIdDelayMinus, StepperPartRect(layout.delayStepper, 0, scale),
               m_pending.hideDelaySeconds > kHideDelayMinSeconds);
    AddControl(kIdDelayPlus, StepperPartRect(layout.delayStepper, 2, scale),
               m_pending.hideDelaySeconds < kHideDelayMaxSeconds);
    AddControl(kIdHideFullscreen, layout.behaviorRows[2], true);
    AddControl(kIdAutoStart, layout.behaviorRows[3], true);

    const int positionIds[3] = {kIdPosLeft, kIdPosCenter, kIdPosRight};
    for (int index = 0; index < 3; ++index)
    {
        AddControl(positionIds[index],
                   SegmentRectOf(layout.positionPills, index, 3,
                                 kSegmentGapDip * scale),
                   true);
    }

    AddControl(kIdResetPosition, ResetButtonRect(layout, scale), true);

    AddControl(kIdCancel, layout.cancelButton, true);
    AddControl(kIdApply, layout.applyButton, true);
    AddControl(kIdOk, layout.okButton, true);
}

void SettingsWindow::Paint()
{
    if (m_hwnd == nullptr || m_width <= 0 || m_height <= 0)
        return;
    if (!m_target.Ensure(m_width, m_height))
        return;

    m_target.graphics->Clear(Color(0, 0, 0, 0));

    BuildLayout();

    const ThemePalette& palette = GetThemePaletteFor(m_pending.theme);
    Painter painter(*m_target.graphics, m_fonts, m_scale, palette, m_hoverId,
                    m_pressedId);
    const DialogLayout& layout = m_layout;

    DrawPanelChrome(painter, layout, L"灵动岛设置",
                    std::wstring(L"Dynamic Island v") + APP_VERSION_STRING);

    // Live mini island preview.
    DrawCard(painter, layout.previewCard, L"预览");
    DrawPreviewCapsule(painter, layout.previewCard, m_pending);

    // Left column: clock.
    DrawCard(painter, layout.clockCard, L"时钟");
    DrawToggleRow(painter, layout.clockRows[0], L"24 小时制", m_pending.use24Hour,
                  true, kIdUse24Hour);
    DrawToggleRow(painter, layout.clockRows[1], L"显示秒数",
                  m_pending.showSeconds, true, kIdShowSeconds);
    DrawToggleRow(painter, layout.clockRows[2], L"显示上/下午", m_pending.showAmPm,
                  !m_pending.use24Hour, kIdShowAmPm);

    // Left column: appearance.
    DrawCard(painter, layout.appearanceCard, L"外观");
    const std::vector<std::wstring> themeLabels = {L"深色", L"浅色", L"系统"};
    const int themeIds[3] = {kIdThemeDark, kIdThemeLight, kIdThemeSystem};
    DrawSegmentedRow(painter, layout.appearanceRows[0], L"主题",
                     layout.themePills, themeLabels, m_pending.theme, themeIds);

    const std::vector<std::wstring> sizeLabels = {L"小", L"中", L"大"};
    const int sizeIds[3] = {kIdSizeSmall, kIdSizeMedium, kIdSizeLarge};
    DrawSegmentedRow(painter, layout.appearanceRows[1], L"尺寸",
                     layout.sizePills, sizeLabels, m_pending.windowSize,
                     sizeIds);

    DrawStepperRow(painter, layout.appearanceRows[2], layout.fontStepper, L"字号",
                   ToWString(m_pending.fontSize),
                   m_pending.fontSize > kFontSizeMin,
                   m_pending.fontSize < kFontSizeMax, kIdFontMinus, kIdFontPlus);
    DrawToggleRow(painter, layout.appearanceRows[3], L"粗体",
                  m_pending.boldFont, true, kIdBold);
    DrawStepperRow(painter, layout.appearanceRows[4], layout.opacityStepper,
                   L"不透明度", ToWString(m_pending.opacity) + L"%",
                   m_pending.opacity > kOpacityMin,
                   m_pending.opacity < kOpacityMax, kIdOpacityMinus,
                   kIdOpacityPlus);

    // Right column: date.
    DrawCard(painter, layout.dateCard, L"日期");
    DrawToggleRow(painter, layout.dateRows[0], L"显示日期", m_pending.showDate,
                  true, kIdShowDate);
    DrawToggleRow(painter, layout.dateRows[1], L"显示年份", m_pending.showYear,
                  true, kIdShowYear);
    DrawToggleRow(painter, layout.dateRows[2], L"显示星期",
                  m_pending.showWeekday, true, kIdShowWeekday);

    std::vector<std::wstring> dateLabels;
    dateLabels.reserve(ISLAND_DATE_COUNT);
    for (int format = 0; format < ISLAND_DATE_COUNT; ++format)
    {
        AppSettings sample = m_pending;
        sample.showDate = true;
        sample.showWeekday = false;
        sample.dateFormat = format;
        dateLabels.push_back(FormatDateText(sample));
    }
    const int dateIds[4] = {kIdDateMmdd, kIdDateDdmm, kIdDateYmd, kIdDateCn};
    DrawDateFormatPills(painter, layout.dateGrid, dateLabels,
                        m_pending.dateFormat, dateIds);

    // Right column: behavior.
    DrawCard(painter, layout.behaviorCard, L"行为");
    DrawToggleRow(painter, layout.behaviorRows[0], L"自动隐藏",
                  m_pending.autoHide, true, kIdAutoHide);
    DrawStepperRow(painter, layout.behaviorRows[1], layout.delayStepper,
                   L"隐藏延迟",
                   ToWString(m_pending.hideDelaySeconds) + L" s",
                   m_pending.hideDelaySeconds > kHideDelayMinSeconds,
                   m_pending.hideDelaySeconds < kHideDelayMaxSeconds,
                   kIdDelayMinus, kIdDelayPlus);
    DrawToggleRow(painter, layout.behaviorRows[2], L"全屏时隐藏",
                  m_pending.hideOnFullscreen, true, kIdHideFullscreen);
    DrawToggleRow(painter, layout.behaviorRows[3], L"开机自启",
                  m_pending.autoStart, true, kIdAutoStart);

    const std::vector<std::wstring> positionLabels = {L"左", L"中", L"右"};
    const int positionIds[3] = {kIdPosLeft, kIdPosCenter, kIdPosRight};
    int positionIndex = -1;
    if (m_pending.position >= ISLAND_POS_LEFT &&
        m_pending.position <= ISLAND_POS_RIGHT)
    {
        positionIndex = m_pending.position;
    }
    DrawSegmentedRow(painter, layout.behaviorRows[4], L"位置",
                     layout.positionPills, positionLabels, positionIndex,
                     positionIds);

    DrawTextLink(painter, ResetButtonRect(layout, m_scale), L"重置位置",
                 painter.Hovered(kIdResetPosition),
                 painter.Pressed(kIdResetPosition));

    // Footer.
    painter.Text(L"Enter 确定 · Esc 取消", layout.hint, 9.5f, false,
                 palette.panelTextSecondary, 255, StringAlignmentNear,
                 StringAlignmentCenter, true);
    DrawButton(painter, layout.cancelButton, L"取消", false,
               painter.Hovered(kIdCancel), painter.Pressed(kIdCancel));
    DrawButton(painter, layout.applyButton, L"应用", false,
               painter.Hovered(kIdApply), painter.Pressed(kIdApply));
    DrawButton(painter, layout.okButton, L"确定", true, painter.Hovered(kIdOk),
               painter.Pressed(kIdOk));

    m_target.graphics->Flush();
    Present();
}

void SettingsWindow::Present()
{
    if (m_hwnd == nullptr || m_target.bitmap == nullptr ||
        m_target.dibPixels == nullptr)
    {
        return;
    }

    BitmapData data = {};
    const Rect lockRect(0, 0, m_target.width, m_target.height);
    if (m_target.bitmap->LockBits(&lockRect, ImageLockModeRead,
                                  PixelFormat32bppARGB, &data) != Ok)
    {
        return;
    }

    const BYTE* source = static_cast<const BYTE*>(data.Scan0);
    BYTE* destination = static_cast<BYTE*>(m_target.dibPixels);
    const int sourceStride = data.Stride;
    for (int y = 0; y < m_target.height; ++y)
    {
        const BYTE* sourceRow =
            source + static_cast<size_t>(y) * static_cast<size_t>(sourceStride);
        BYTE* destinationRow =
            destination + static_cast<size_t>(y) * static_cast<size_t>(m_target.stride);
        for (int x = 0; x < m_target.width; ++x)
        {
            const size_t offset = static_cast<size_t>(x) * 4u;
            const BYTE alpha = sourceRow[offset + 3u];
            destinationRow[offset + 0u] =
                static_cast<BYTE>(sourceRow[offset + 0u] * alpha / 255);
            destinationRow[offset + 1u] =
                static_cast<BYTE>(sourceRow[offset + 1u] * alpha / 255);
            destinationRow[offset + 2u] =
                static_cast<BYTE>(sourceRow[offset + 2u] * alpha / 255);
            destinationRow[offset + 3u] = alpha;
        }
    }
    m_target.bitmap->UnlockBits(&data);

    RECT windowRect = CurrentWindowRect();
    POINT destinationPoint = {windowRect.left, windowRect.top};
    SIZE size = {m_target.width, m_target.height};
    POINT sourcePoint = {0, 0};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr)
        return;
    UpdateLayeredWindow(m_hwnd, screenDc, &destinationPoint, &size,
                        m_target.memoryDc, &sourcePoint, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);
}

int SettingsWindow::HitTestControl(POINT clientPoint) const
{
    for (const Control& control : m_layout.controls)
    {
        if (PtInRect(&control.rect, clientPoint) != FALSE)
            return control.id;
    }
    return kIdNone;
}

bool SettingsWindow::IsControlEnabled(int id) const
{
    for (const Control& control : m_layout.controls)
    {
        if (control.id == id)
            return control.enabled;
    }
    return false;
}

bool SettingsWindow::IsPointOnTitleBar(POINT clientPoint) const
{
    const RECT titleBar = ToPixelRect(m_layout.titleBar);
    return PtInRect(&titleBar, clientPoint) != FALSE;
}

void SettingsWindow::UpdateHover(POINT point)
{
    const int hit = HitTestControl(point);
    const int hoverId =
        (hit != kIdNone && IsControlEnabled(hit)) ? hit : kIdNone;
    if (hoverId != m_hoverId)
    {
        m_hoverId = hoverId;
        Paint();
    }
}

void SettingsWindow::UpdateCursor()
{
    POINT point = {};
    GetCursorPos(&point);
    ScreenToClient(m_hwnd, &point);
    UpdateCursorForPoint(point);
}

void SettingsWindow::UpdateCursorForPoint(POINT point)
{
    if (m_hwnd == nullptr)
        return;

    if (m_dragging)
    {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return;
    }

    const int hit = HitTestControl(point);
    if (hit != kIdNone && IsControlEnabled(hit))
    {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return;
    }
    if (IsPointOnTitleBar(point))
    {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return;
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void SettingsWindow::OnMouseMove(POINT point)
{
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(TRACKMOUSEEVENT);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = m_hwnd;
        TrackMouseEvent(&track);
        m_trackingMouse = true;
    }

    if (m_dragging)
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        UpdateTitleDrag(cursor);
        return;
    }

    UpdateHover(point);
    UpdateCursorForPoint(point);
}

void SettingsWindow::OnMouseLeave()
{
    m_trackingMouse = false;
    if (m_dragging || m_pressedId != kIdNone)
        return;
    if (m_hoverId != kIdNone)
    {
        m_hoverId = kIdNone;
        Paint();
    }
}

void SettingsWindow::OnLButtonDown(POINT point)
{
    SetFocus(m_hwnd);

    const int hit = HitTestControl(point);
    if (hit != kIdNone)
    {
        if (!IsControlEnabled(hit))
            return;
        m_pressedId = hit;
        SetCapture(m_hwnd);
        Paint();
        return;
    }

    if (IsPointOnTitleBar(point))
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        BeginTitleDrag(cursor);
    }
}

void SettingsWindow::OnLButtonUp(POINT point)
{
    if (m_dragging)
    {
        m_dragging = false;
        if (GetCapture() == m_hwnd)
            ReleaseCapture();
        return;
    }

    const int pressed = m_pressedId;
    m_pressedId = kIdNone;
    if (m_hwnd != nullptr && GetCapture() == m_hwnd)
        ReleaseCapture();
    if (pressed == kIdNone)
        return;

    const int hit = HitTestControl(point);
    if (hit == pressed && IsControlEnabled(pressed))
    {
        PerformAction(pressed);
    }
    else
    {
        Paint();
    }
}

void SettingsWindow::BeginTitleDrag(POINT screenPoint)
{
    if (m_hwnd == nullptr)
        return;
    RECT rect = {};
    if (GetWindowRect(m_hwnd, &rect) == FALSE)
        return;

    m_dragging = true;
    m_dragCursorStart = screenPoint;
    m_dragWindowStart.x = rect.left;
    m_dragWindowStart.y = rect.top;
    SetCapture(m_hwnd);
    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
}

void SettingsWindow::UpdateTitleDrag(POINT screenPoint)
{
    if (m_hwnd == nullptr || !m_dragging)
        return;

    const int x = m_dragWindowStart.x + (screenPoint.x - m_dragCursorStart.x);
    const int y = m_dragWindowStart.y + (screenPoint.y - m_dragCursorStart.y);

    SnapWindowToWorkArea(x, y);
}

void SettingsWindow::PerformAction(int id)
{
    switch (id)
    {
    case kIdClose:
    case kIdCancel:
        Close(false);
        return;

    case kIdOk:
        ApplyPending();
        Close(false);
        return;

    case kIdApply:
        ApplyPending();
        Paint();
        return;

    case kIdUse24Hour:
        m_pending.use24Hour = !m_pending.use24Hour;
        break;
    case kIdShowSeconds:
        m_pending.showSeconds = !m_pending.showSeconds;
        break;
    case kIdShowAmPm:
        m_pending.showAmPm = !m_pending.showAmPm;
        break;

    case kIdThemeDark:
        m_pending.theme = ISLAND_THEME_DARK;
        break;
    case kIdThemeLight:
        m_pending.theme = ISLAND_THEME_LIGHT;
        break;
    case kIdThemeSystem:
        m_pending.theme = ISLAND_THEME_SYSTEM;
        break;

    case kIdSizeSmall:
        m_pending.windowSize = ISLAND_SIZE_SMALL;
        break;
    case kIdSizeMedium:
        m_pending.windowSize = ISLAND_SIZE_MEDIUM;
        break;
    case kIdSizeLarge:
        m_pending.windowSize = ISLAND_SIZE_LARGE;
        break;

    case kIdFontMinus:
        m_pending.fontSize =
            ClampIntValue(m_pending.fontSize - 1, kFontSizeMin, kFontSizeMax);
        break;
    case kIdFontPlus:
        m_pending.fontSize =
            ClampIntValue(m_pending.fontSize + 1, kFontSizeMin, kFontSizeMax);
        break;
    case kIdBold:
        m_pending.boldFont = !m_pending.boldFont;
        break;

    case kIdOpacityMinus:
        m_pending.opacity =
            ClampIntValue(m_pending.opacity - 5, kOpacityMin, kOpacityMax);
        break;
    case kIdOpacityPlus:
        m_pending.opacity =
            ClampIntValue(m_pending.opacity + 5, kOpacityMin, kOpacityMax);
        break;

    case kIdShowDate:
        m_pending.showDate = !m_pending.showDate;
        break;
    case kIdShowYear:
        m_pending.showYear = !m_pending.showYear;
        break;
    case kIdShowWeekday:
        m_pending.showWeekday = !m_pending.showWeekday;
        break;

    case kIdDateMmdd:
        m_pending.dateFormat = ISLAND_DATE_MMDD;
        break;
    case kIdDateDdmm:
        m_pending.dateFormat = ISLAND_DATE_DDMM;
        break;
    case kIdDateYmd:
        m_pending.dateFormat = ISLAND_DATE_YMD;
        break;
    case kIdDateCn:
        m_pending.dateFormat = ISLAND_DATE_CN;
        break;

    case kIdAutoHide:
        m_pending.autoHide = !m_pending.autoHide;
        break;
    case kIdDelayMinus:
        m_pending.hideDelaySeconds = ClampIntValue(
            m_pending.hideDelaySeconds - 5, kHideDelayMinSeconds,
            kHideDelayMaxSeconds);
        break;
    case kIdDelayPlus:
        m_pending.hideDelaySeconds = ClampIntValue(
            m_pending.hideDelaySeconds + 5, kHideDelayMinSeconds,
            kHideDelayMaxSeconds);
        break;
    case kIdHideFullscreen:
        m_pending.hideOnFullscreen = !m_pending.hideOnFullscreen;
        break;
    case kIdAutoStart:
        m_pending.autoStart = !m_pending.autoStart;
        break;

    case kIdPosLeft:
    case kIdPosCenter:
    case kIdPosRight:
        m_pending.position = (id == kIdPosLeft)    ? ISLAND_POS_LEFT
                             : (id == kIdPosCenter) ? ISLAND_POS_CENTER
                                                    : ISLAND_POS_RIGHT;
        m_pending.customX = -100000;
        m_pending.customY = -100000;
        break;

    case kIdResetPosition:
        // Pending-only edit (applied with the rest of the changes): forget the
        // dragged custom position and return to the centered preset.
        m_pending.position = ISLAND_POS_CENTER;
        m_pending.customX = -100000;
        m_pending.customY = -100000;
        break;

    default:
        return;
    }

    Paint();
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND hwnd, UINT message,
                                            WPARAM wParam, LPARAM lParam)
{
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        if (self != nullptr)
        {
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (message == WM_NCDESTROY)
    {
        const LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return result;
    }

    if (self == nullptr || self->m_hwnd == nullptr)
        return DefWindowProcW(hwnd, message, wParam, lParam);

    return self->HandleMessage(message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    const auto ClientPoint = [lParam]() {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return point;
    };

    switch (message)
    {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_PAINT:
        // Painting happens through UpdateLayeredWindow; only validate.
        ValidateRect(m_hwnd, nullptr);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;

    case WM_TIMER:
        if (wParam == kPreviewTimerId && !m_dragging)
            Paint();
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(ClientPoint());
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(ClientPoint());
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(ClientPoint());
        return 0;

    case WM_CAPTURECHANGED:
    {
        const bool wasBusy = (m_pressedId != kIdNone) || m_dragging;
        m_pressedId = kIdNone;
        m_dragging = false;
        if (wasBusy && m_hwnd != nullptr)
            Paint();
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            UpdateCursor();
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            Close(false);
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            Close(true);
            return 0;
        }
        break;

    case WM_DPICHANGED:
    {
        const UINT dpi = (LOWORD(wParam) != 0) ? static_cast<UINT>(LOWORD(wParam))
                                               : QueryWindowDpi(m_hwnd);
        RECT suggested = CurrentWindowRect();
        if (lParam != 0)
            suggested = *reinterpret_cast<const RECT*>(lParam);
        RelayoutForDpi(dpi, suggested);
        return 0;
    }

    case WM_DISPLAYCHANGE:
        RelayoutForDpi(QueryWindowDpi(m_hwnd), CurrentWindowRect());
        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA)
        {
            RelayoutForDpi(QueryWindowDpi(m_hwnd), CurrentWindowRect());
        }
        else if (lParam != 0 &&
                 lstrcmpW(reinterpret_cast<LPCWSTR>(lParam),
                          L"ImmersiveColorSet") == 0)
        {
            RefreshSystemThemeCache();
            Paint();
        }
        return 0;

    case WM_THEMECHANGED:
        RefreshSystemThemeCache();
        Paint();
        return 0;

    case WM_CLOSE:
        Close(false);
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Singleton + public API
// ---------------------------------------------------------------------------
// Deliberately allocated once and never destroyed: main() shuts GDI+ down
// before process exit, and a static destructor must not touch GDI+ objects
// afterwards. The window itself is always destroyed through WM_DESTROY.
SettingsWindow& SettingsWindowInstance()
{
    static SettingsWindow* instance = new SettingsWindow();
    return *instance;
}
} // namespace

void ShowSettingsDialog(HWND owner)
{
    if (!SettingsWindowInstance().Create(owner))
        return;

    const HWND hwnd = SettingsWindowInstance().Handle();
    if (hwnd == nullptr)
        return;

    if (IsWindowVisible(hwnd) == FALSE)
        ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SettingsWindowInstance().Repaint();
}

void CloseSettingsDialog(bool applyChanges)
{
    SettingsWindowInstance().Close(applyChanges);
}

bool IsSettingsDialogOpen()
{
    return SettingsWindowInstance().Handle() != nullptr;
}

void RefreshSettingsDialog()
{
    SettingsWindowInstance().ReloadPending();
}
