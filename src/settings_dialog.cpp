// Dynamic Island - v5.2 custom-drawn settings window.
//
// A modeless, fully owner-drawn popup: WS_POPUP + WS_EX_LAYERED |
// WS_EX_TOOLWINDOW | WS_EX_TOPMOST.  The whole dialog is rendered with GDI+
// into a persistent 32bpp ARGB surface and presented with UpdateLayeredWindow
// (the same technique the island itself uses).  GDI+ is initialised once by
// main(); this file never calls GdiplusStartup/GdiplusShutdown.
//
// Painting is dirty-rectangle based to keep the window responsive:
//   * repaint requests are coalesced into one posted WM_APP+100 paint per
//     message-loop cycle,
//   * only the dirty rectangle is re-drawn (Graphics::SetClip) and only that
//     rectangle is premultiplied into the persistent DIB,
//   * title-bar dragging only moves the window (no repaint at all),
//   * the 1 s preview timer only repaints the preview card, and only when the
//     formatted preview text actually changed.
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

#include "actions.h"
#include "app_state.h"
#include "color_util.h"
#include "island.h"
#include "notifications.h"
#include "rendering.h"
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
constexpr float kShadowDip = 18.0f;

// v5.2: the notification settings card is enabled. The constant is kept so the
// card can still be hidden in one place if needed; the window height (and
// therefore the work-area fit logic) follows it.
constexpr bool kShowNotificationSettings = true;

constexpr float kBaseWindowHeightDip = 630.0f;
// 12 DIP gap below the columns + 112 DIP notification card + 4 DIP extra
// footer spacing = 128 DIP.
constexpr float kNotificationExtraHeightDip = 128.0f;
constexpr float kWindowHeightDip =
    kBaseWindowHeightDip +
    (kShowNotificationSettings ? kNotificationExtraHeightDip : 0.0f);

constexpr float kPanelWidthDip = kWindowWidthDip - kShadowDip * 2.0f; // 564
constexpr float kPanelHeightDip =
    kWindowHeightDip - kShadowDip * 2.0f; // 594 (hidden) / 722 (shown)
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

// v5.2: left column cards are taller so that both columns end at the same Y
// before the full-width notification card (user complaint):
//   left  = clock 150 + 12 + appearance 236 = 398
//   right = date  170 + 12 + behavior   216 = 398
// The extra left-column height is distributed between the rows (row heights and
// labels are unchanged) so the cards do not end with a large empty bottom.
constexpr float kClockCardHeightDip = 150.0f;
constexpr float kAppearanceCardHeightDip = 236.0f;
constexpr float kClockBottomPaddingDip = 11.0f;
constexpr float kAppearanceBottomPaddingDip = 13.0f;
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
constexpr float kFooterBandHeightDip = 46.0f;
constexpr float kFooterTopDip = kPanelHeightDip - kFooterBandHeightDip; // 548 / 676
constexpr float kResetRowHeightDip = 20.0f;
constexpr float kResetButtonWidthDip = 84.0f;
constexpr float kSystemTimeLinkWidthDip = 96.0f;

// Notification card (full width, appended below the two columns; only used
// when kShowNotificationSettings is true).
constexpr float kNotifTopDip = 560.0f; // contentTop(150) + columns(398) + 12
constexpr float kNotifCardHeightDip = 112.0f;
constexpr float kNotifRowHeightDip = 26.0f;
constexpr float kNotifRow1TopDip = 30.0f;
constexpr float kNotifRow2TopDip = 59.0f;
constexpr float kNotifRow3TopDip = 88.0f;
constexpr float kNotifRow3HeightDip = 18.0f;
constexpr float kNotifSegmentGapDip = 8.0f;
constexpr float kNotifRow2LabelWidthDip = 70.0f;
constexpr float kNotifHintGapDip = 12.0f;
constexpr float kNotifAccessLinkWidthDip = 220.0f;

constexpr float kDangerButtonWidthDip = 88.0f;
constexpr float kFooterHintGapDip = 12.0f;

constexpr float kCloseButtonSizeDip = 28.0f;
constexpr float kCloseButtonMarginDip = 14.0f;

constexpr UINT_PTR kPreviewTimerId = 1;
constexpr UINT kMsgPaintPending = WM_APP + 100; // posted by RequestPaint()
constexpr int kMaxWindowWidthPx = 12000;
constexpr int kMaxWindowHeightPx = 12000;

// A few extra device pixels around every dirty rectangle to cover the
// antialiased edges and the hover highlight that sticks out of a control.
constexpr int kDirtyMarginPx = 10;

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
    kIdSystemTimeSettings,
    kIdExitApp,

    kIdNotifyEnable,
    kIdNotifyShowContent,
    kIdNotifyWeChat,
    kIdNotifyQQ,
    kIdNotifyPeekMinus,
    kIdNotifyPeekPlus,
    kIdNotifyAccess,
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

// Fast round(v * a / 255) without an integer division.
BYTE PremultiplyChannel(BYTE value, BYTE alpha)
{
    const unsigned int scaled = static_cast<unsigned int>(value) * alpha + 128u;
    return static_cast<BYTE>((scaled + (scaled >> 8)) >> 8);
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
bool RectsIntersect(const RectF& a, const RectF& b)
{
    return a.X < b.X + b.Width && b.X < a.X + a.Width && a.Y < b.Y + b.Height &&
           b.Y < a.Y + a.Height;
}

class Painter
{
public:
    Painter(Graphics& graphics, FontCache& fonts, float scale,
            const ThemePalette& palette, int hoverId, int pressedId,
            const RectF& paintBounds, bool clipped)
        : m_graphics(graphics)
        , m_fonts(fonts)
        , m_scale(scale)
        , m_palette(palette)
        , m_hoverId(hoverId)
        , m_pressedId(pressedId)
        , m_paintBounds(paintBounds)
        , m_clipped(clipped)
    {
    }

    Graphics& G() { return m_graphics; }
    float Scale() const { return m_scale; }
    const ThemePalette& Palette() const { return m_palette; }
    bool Hovered(int id) const { return id != kIdNone && m_hoverId == id; }
    bool Pressed(int id) const { return id != kIdNone && m_pressedId == id; }

    // False when the given device-space rectangle lies completely outside the
    // region being repainted; used to skip whole widgets (and their text
    // layout) on dirty-rectangle repaints.
    bool Visible(const RectF& rect) const
    {
        if (!m_clipped)
            return true;
        return RectsIntersect(rect, m_paintBounds);
    }

    bool Clipped() const { return m_clipped; }
    const RectF& PaintBounds() const { return m_paintBounds; }

    void Text(const std::wstring& text, const RectF& layout, float sizeDip,
              bool bold, COLORREF color, BYTE alpha = 255,
              StringAlignment horizontal = StringAlignmentNear,
              StringAlignment vertical = StringAlignmentCenter,
              bool ellipsis = false)
    {
        if (text.empty() || !Visible(layout))
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
    RectF m_paintBounds;
    bool m_clipped;
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
    RectF systemTimeLink;

    // Notification card (v5.2: enabled).
    RectF notifCard;
    RectF notifRows[3];
    RectF notifStepper;
    RectF notifHint;
    RectF notifToggleSegments[4];
    RectF notifAccessLink;

    RectF hint;
    RectF cancelButton;
    RectF applyButton;
    RectF okButton;
    RectF exitButton;

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

RectF SystemTimeLinkRect(const DialogLayout& layout, float scale)
{
    return RectF(layout.resetRow.X, layout.resetRow.Y,
                 kSystemTimeLinkWidthDip * scale, layout.resetRow.Height);
}

// The four notification toggles share one row; each segment holds a label and
// a switch.
RectF NotifToggleSegment(const RectF& row, int index, float gapPx)
{
    const float width = (row.Width - gapPx * 3.0f) * 0.25f;
    return RectF(row.X + static_cast<float>(index) * (width + gapPx), row.Y,
                 width, row.Height);
}

// ---------------------------------------------------------------------------
// Widget drawing
// ---------------------------------------------------------------------------
void DrawCloseButton(Painter& painter, const RectF& rect, bool hovered,
                     bool pressed)
{
    if (!painter.Visible(rect))
        return;
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
    if (!painter.Visible(InsetRectF(row, -4.0f * painter.Scale())))
        return;
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
    if (!painter.Visible(row))
        return;
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
                    bool minusEnabled, bool plusEnabled, int minusId, int plusId,
                    bool rowEnabled = true)
{
    if (!painter.Visible(row))
        return;
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();
    const COLORREF textColor =
        rowEnabled ? palette.panelText : DisabledTextColor(palette);

    const RectF labelRect(row.X, row.Y, container.X - row.X - 8.0f * scale,
                          row.Height);
    painter.Text(label, labelRect, 12.5f, false, textColor);

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

    painter.Text(value, valueRect, 11.5f, false, textColor, 255,
                 StringAlignmentCenter, StringAlignmentCenter, true);
}

void DrawDateFormatPills(Painter& painter, const RectF& grid,
                         const std::vector<std::wstring>& labels, int selected,
                         const int* ids)
{
    if (!painter.Visible(grid))
        return;
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
    if (!painter.Visible(rect))
        return;
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
    if (!painter.Visible(rect))
        return;
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

// Destructive action button used by the footer ("退出程序").
void DrawDangerButton(Painter& painter, const RectF& rect,
                      const std::wstring& label, bool hovered, bool pressed)
{
    if (!painter.Visible(rect))
        return;
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    const COLORREF base = palette.dark ? RGB(216, 72, 72) : RGB(200, 52, 52);
    COLORREF fill = base;
    if (pressed)
        fill = BlendColor(base, RGB(0, 0, 0), 38);
    else if (hovered)
        fill = BlendColor(base, RGB(255, 255, 255), palette.dark ? 24 : 18);

    painter.FillRoundRect(rect, 10.0f * scale, fill);
    painter.Text(label, rect, 12.0f, true, RGB(255, 255, 255), 255,
                 StringAlignmentCenter, StringAlignmentCenter, false);
}

void DrawCard(Painter& painter, const RectF& card, const std::wstring& title)
{
    if (!painter.Visible(card))
        return;
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

// v5.2: full-width notification card (toggles, peek duration, privacy hint and
// the notification-access prompt).
void DrawNotificationCard(Painter& painter, const DialogLayout& layout,
                          const AppSettings& settings, NotificationAccess access)
{
    if (!painter.Visible(layout.notifCard))
        return;

    DrawCard(painter, layout.notifCard, L"通知");

    const ThemePalette& palette = painter.Palette();

    const wchar_t* labels[4] = {L"消息提醒", L"显示内容", L"微信", L"QQ"};
    const bool values[4] = {settings.notifyEnable, settings.notifyShowContent,
                            settings.notifyWeChat, settings.notifyQQ};
    const bool enabledFlags[4] = {true, settings.notifyEnable,
                                  settings.notifyEnable, settings.notifyEnable};
    const int ids[4] = {kIdNotifyEnable, kIdNotifyShowContent, kIdNotifyWeChat,
                        kIdNotifyQQ};
    for (int index = 0; index < 4; ++index)
    {
        DrawToggleRow(painter, layout.notifToggleSegments[index], labels[index],
                      values[index], enabledFlags[index], ids[index]);
    }

    // Row 2: peek duration stepper on the left, privacy hint on the right.
    const bool notifyOn = settings.notifyEnable;
    DrawStepperRow(painter, layout.notifRows[1], layout.notifStepper, L"停留时长",
                   ToWString(settings.notifyPeekSeconds) + L" s",
                   notifyOn &&
                       settings.notifyPeekSeconds > kNotifyPeekSecondsMin,
                   notifyOn &&
                       settings.notifyPeekSeconds < kNotifyPeekSecondsMax,
                   kIdNotifyPeekMinus, kIdNotifyPeekPlus, notifyOn);

    painter.Text(L"微信读取界面、QQ 走系统通知；不注入、不读内存", layout.notifHint,
                 9.0f, false, palette.panelTextSecondary, 255,
                 StringAlignmentNear, StringAlignmentCenter, true);

    // Row 3: notification-access prompt (only while access is not granted).
    if (access != NotificationAccess::Allowed)
    {
        DrawTextLink(painter, layout.notifAccessLink,
                     L"未授权通知访问 · 点击授权",
                     painter.Hovered(kIdNotifyAccess),
                     painter.Pressed(kIdNotifyAccess));
    }
}

void DrawPanelChrome(Painter& painter, const DialogLayout& layout,
                     const std::wstring& title, const std::wstring& subtitle)
{
    const ThemePalette& palette = painter.Palette();
    const float scale = painter.Scale();

    // The soft drop shadow only exists in the transparent window padding, so it
    // is painted for full repaints only and clipped to the padding ring: the
    // opaque panel would hide everything inside it anyway. This is visually
    // identical but roughly 10x cheaper (the shadow used to dominate a full
    // repaint: ~350 ms -> ~29 ms at 150% DPI).
    const int steps = std::max(4, RoundToInt(kShadowDip * scale));
    const float peakAlpha = palette.dark ? 88.0f : 46.0f;
    if (!painter.Clipped())
    {
        GraphicsPath panelPath;
        AddRoundedRectPath(panelPath, layout.panel, kPanelRadiusDip * scale);
        Region panelRegion(&panelPath);
        const GraphicsState shadowClipState = painter.G().Save();
        painter.G().SetClip(&panelRegion, CombineModeExclude);
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
            painter.FillRoundRect(shadow, kPanelRadiusDip * scale + grow,
                                  RGB(0, 0, 0), static_cast<BYTE>(alpha));
        }
        painter.G().Restore(shadowClipState);
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
    void DrawScene(Painter& painter);
    void RequestPaint();
    void RequestPaintRect(const RECT& rect);
    void RequestPaintCard(const RectF& card);
    void PostPaintRequest();
    void RepaintControl(int id);
    void RepaintAfterAction(int id);
    void FlushPaint();
    void PaintPending();
    void PaintRegion(bool requestedFull, const RECT& region);
    void ResetPaintState();
    void OnTimerTick();
    void ApplyPending();
    void PerformAction(int id);
    const Control* FindControl(int id) const;

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

    // Dirty-rectangle repaint state (device pixels, client coordinates).
    RECT m_dirtyRect = {};
    bool m_dirtyFull = false;
    bool m_paintPosted = false;

    // Content caches so the 1 s timer does not repaint unchanged content.
    std::wstring m_lastPreviewText;
    NotificationAccess m_notificationAccess = NotificationAccess::Unknown;

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
    ResetPaintState();
    m_lastPreviewText.clear();
    m_notificationAccess = NotificationAccess::Unknown;

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
    if (m_hwnd == nullptr)
        return;
    RequestPaint();
    // First frame / external refresh: present immediately instead of waiting
    // for the posted paint message.
    FlushPaint();
}

void SettingsWindow::ReloadPending()
{
    if (m_hwnd == nullptr)
        return;
    if (m_dragging || m_pressedId != kIdNone)
        return;
    m_pending = g_settings;
    RequestPaint();
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
    m_lastPreviewText = FormatDisplayText(m_pending);
    m_notificationAccess = NotificationAccess::Unknown;
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

    ResetPaintState();
    m_target.Destroy();
    m_fonts.Clear();
    m_layout.controls.clear();
    m_lastPreviewText.clear();

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
    RequestPaint();
    FlushPaint(); // the render target was recreated; present the new size now
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

    // The taller left-column cards distribute their extra height between the
    // rows (row heights/labels stay the same), keeping the old bottom padding.
    const float clockRowGap =
        (kClockCardHeightDip - kRowsTopDip - kClockBottomPaddingDip -
         3.0f * kRowHeightDip) /
        2.0f;
    const float appearanceRowGap =
        (kAppearanceCardHeightDip - kRowsTopDip - kAppearanceBottomPaddingDip -
         5.0f * kRowHeightDip) /
        4.0f;

    for (int index = 0; index < 3; ++index)
    {
        layout.clockRows[index] =
            CardRow(layout.clockCard,
                    kRowsTopDip + static_cast<float>(index) *
                                      (kRowHeightDip + clockRowGap),
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
                                      (kRowHeightDip + appearanceRowGap),
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
    layout.systemTimeLink = SystemTimeLinkRect(layout, scale);

    // Notification card: full width below both columns.
    if (kShowNotificationSettings)
    {
        layout.notifCard = PanelRect(kContentMarginDip, kNotifTopDip,
                                     kPanelWidthDip - kContentMarginDip * 2.0f,
                                     kNotifCardHeightDip);
        layout.notifRows[0] =
            CardRow(layout.notifCard, kNotifRow1TopDip, kNotifRowHeightDip);
        layout.notifRows[1] =
            CardRow(layout.notifCard, kNotifRow2TopDip, kNotifRowHeightDip);
        layout.notifRows[2] =
            CardRow(layout.notifCard, kNotifRow3TopDip, kNotifRow3HeightDip);
        for (int index = 0; index < 4; ++index)
        {
            layout.notifToggleSegments[index] =
                NotifToggleSegment(layout.notifRows[0], index,
                                   kNotifSegmentGapDip * scale);
        }

        // Row 2: label + stepper on the left, privacy hint on the right.
        const RectF& row2 = layout.notifRows[1];
        layout.notifStepper = RectF(
            row2.X + kNotifRow2LabelWidthDip * scale,
            row2.Y + (row2.Height - kStepperHeightDip * scale) * 0.5f,
            kStepperWidthDip * scale, kStepperHeightDip * scale);
        const float hintX =
            layout.notifStepper.X + layout.notifStepper.Width +
            kNotifHintGapDip * scale;
        layout.notifHint = RectF(hintX, row2.Y,
                                 row2.X + row2.Width - hintX, row2.Height);

        // Row 3: notification-access prompt (only shown while not granted).
        layout.notifAccessLink =
            RectF(layout.notifRows[2].X, layout.notifRows[2].Y,
                  kNotifAccessLinkWidthDip * scale,
                  layout.notifRows[2].Height);
    }

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
    layout.exitButton = PanelRect(kContentMarginDip, buttonY,
                                  kDangerButtonWidthDip, kButtonHeightDip);
    const float hintX =
        kContentMarginDip + kDangerButtonWidthDip + kFooterHintGapDip;
    layout.hint = PanelRect(hintX, buttonY, cancelX - hintX - kFooterHintGapDip,
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
    AddControl(kIdSystemTimeSettings, layout.systemTimeLink, true);

    // Notification card controls (v5.2).
    if (kShowNotificationSettings)
    {
        const int notifToggleIds[4] = {kIdNotifyEnable, kIdNotifyShowContent,
                                       kIdNotifyWeChat, kIdNotifyQQ};
        for (int index = 0; index < 4; ++index)
        {
            const bool enabled = (index == 0) || m_pending.notifyEnable;
            AddControl(notifToggleIds[index],
                       layout.notifToggleSegments[index], enabled);
        }
        const bool peekEnabled = m_pending.notifyEnable;
        AddControl(kIdNotifyPeekMinus,
                   StepperPartRect(layout.notifStepper, 0, scale),
                   peekEnabled &&
                       m_pending.notifyPeekSeconds > kNotifyPeekSecondsMin);
        AddControl(kIdNotifyPeekPlus,
                   StepperPartRect(layout.notifStepper, 2, scale),
                   peekEnabled &&
                       m_pending.notifyPeekSeconds < kNotifyPeekSecondsMax);
        AddControl(kIdNotifyAccess, layout.notifAccessLink,
                   m_notificationAccess != NotificationAccess::Allowed);
    }

    AddControl(kIdExitApp, layout.exitButton, true);
    AddControl(kIdCancel, layout.cancelButton, true);
    AddControl(kIdApply, layout.applyButton, true);
    AddControl(kIdOk, layout.okButton, true);
}

void SettingsWindow::DrawScene(Painter& painter)
{
    const DialogLayout& layout = m_layout;
    const ThemePalette& palette = painter.Palette();
    const float scale = m_scale;

    DrawPanelChrome(painter, layout, L"灵动岛设置",
                    std::wstring(L"Dynamic Island v") + APP_VERSION_STRING);

    // Live island preview: drawn by the shared renderer (rendering.h) so the
    // capsule is pixel-identical to the real one - same shadow, gradient,
    // border, opacity and fitted clock text, at its real DPI-scaled size.
    DrawCard(painter, layout.previewCard, L"预览");
    {
        const RectF& card = layout.previewCard;
        painter.Text(L"实际大小",
                     RectF(card.X + card.Width - 100.0f * scale,
                           card.Y + 7.0f * scale, 88.0f * scale, 18.0f * scale),
                     9.0f, false, palette.panelTextSecondary, 255,
                     StringAlignmentFar, StringAlignmentCenter, false);

        const RectF previewBounds(card.X + 12.0f * scale,
                                  card.Y + 22.0f * scale,
                                  card.Width - 24.0f * scale,
                                  card.Height - 26.0f * scale);
        if (painter.Visible(previewBounds))
        {
            // At normal DPI m_scale == dpi/96, so this is exactly m_dpi; on
            // fit-shrunk windows (very small screens) the preview shrinks with
            // the rest of the layout instead of overflowing the card.
            const UINT previewDpi =
                static_cast<UINT>(std::lround(96.0f * m_scale));
            const GraphicsState clipState = painter.G().Save();
            painter.G().SetClip(previewBounds, CombineModeReplace);
            RenderIslandPreview(painter.G(), previewBounds, previewDpi,
                                m_pending, palette);
            painter.G().Restore(clipState);
        }
    }

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

    DrawTextLink(painter, SystemTimeLinkRect(layout, scale), L"系统时间设置",
                 painter.Hovered(kIdSystemTimeSettings),
                 painter.Pressed(kIdSystemTimeSettings));
    DrawTextLink(painter, ResetButtonRect(layout, scale), L"重置位置",
                 painter.Hovered(kIdResetPosition),
                 painter.Pressed(kIdResetPosition));

    // v5.2 full-width notification card.
    if (kShowNotificationSettings)
        DrawNotificationCard(painter, layout, m_pending, m_notificationAccess);

    // Footer: danger exit button on the left, hint, then Cancel/Apply/OK.
    DrawDangerButton(painter, layout.exitButton, L"退出程序",
                     painter.Hovered(kIdExitApp), painter.Pressed(kIdExitApp));
    painter.Text(L"Enter 确定 · Esc 取消", layout.hint, 9.5f, false,
                 palette.panelTextSecondary, 255, StringAlignmentNear,
                 StringAlignmentCenter, true);
    DrawButton(painter, layout.cancelButton, L"取消", false,
               painter.Hovered(kIdCancel), painter.Pressed(kIdCancel));
    DrawButton(painter, layout.applyButton, L"应用", false,
               painter.Hovered(kIdApply), painter.Pressed(kIdApply));
    DrawButton(painter, layout.okButton, L"确定", true, painter.Hovered(kIdOk),
               painter.Pressed(kIdOk));
}

// ---------------------------------------------------------------------------
// Dirty-rectangle repaint scheduling
// ---------------------------------------------------------------------------
void SettingsWindow::RequestPaint()
{
    m_dirtyFull = true;
    PostPaintRequest();
}

void SettingsWindow::RequestPaintRect(const RECT& rect)
{
    if (m_dirtyFull)
        return;

    RECT clipped = rect;
    clipped.left = std::max<LONG>(0, std::min<LONG>(clipped.left, m_width));
    clipped.top = std::max<LONG>(0, std::min<LONG>(clipped.top, m_height));
    clipped.right = std::max<LONG>(0, std::min<LONG>(clipped.right, m_width));
    clipped.bottom = std::max<LONG>(0, std::min<LONG>(clipped.bottom, m_height));
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top)
        return;

    if (m_dirtyRect.right <= m_dirtyRect.left ||
        m_dirtyRect.bottom <= m_dirtyRect.top)
    {
        m_dirtyRect = clipped;
    }
    else
    {
        m_dirtyRect.left = std::min(m_dirtyRect.left, clipped.left);
        m_dirtyRect.top = std::min(m_dirtyRect.top, clipped.top);
        m_dirtyRect.right = std::max(m_dirtyRect.right, clipped.right);
        m_dirtyRect.bottom = std::max(m_dirtyRect.bottom, clipped.bottom);
    }

    PostPaintRequest();
}

void SettingsWindow::PostPaintRequest()
{
    if (m_hwnd == nullptr || m_paintPosted)
        return;
    m_paintPosted = true;
    PostMessageW(m_hwnd, kMsgPaintPending, 0, 0);
}

void SettingsWindow::RequestPaintCard(const RectF& card)
{
    RECT rect = ToPixelRect(card);
    InflateRect(&rect, kDirtyMarginPx, kDirtyMarginPx);
    RequestPaintRect(rect);
}

const Control* SettingsWindow::FindControl(int id) const
{
    for (const Control& control : m_layout.controls)
    {
        if (control.id == id)
            return &control;
    }
    return nullptr;
}

void SettingsWindow::RepaintControl(int id)
{
    const Control* control = FindControl(id);
    if (control == nullptr)
    {
        RequestPaint();
        return;
    }
    RECT rect = control->rect;
    InflateRect(&rect, kDirtyMarginPx, kDirtyMarginPx);
    RequestPaintRect(rect);
}

// Marks the card that owns the clicked control (plus the preview when the
// control affects it) instead of repainting the whole window.
void SettingsWindow::RepaintAfterAction(int id)
{
    const Control* control = FindControl(id);
    if (control == nullptr)
    {
        RequestPaint();
        return;
    }

    const POINT center = {(control->rect.left + control->rect.right) / 2,
                          (control->rect.top + control->rect.bottom) / 2};
    struct CardEntry
    {
        const RectF* card;
        bool affectsPreview;
    };
    const CardEntry cards[] = {
        {&m_layout.clockCard, true},
        {&m_layout.appearanceCard, true},
        {&m_layout.dateCard, true},
        {&m_layout.behaviorCard, false},
        {&m_layout.notifCard, false},
    };
    for (const CardEntry& entry : cards)
    {
        RECT cardRect = ToPixelRect(*entry.card);
        if (PtInRect(&cardRect, center) == FALSE)
            continue;
        InflateRect(&cardRect, kDirtyMarginPx, kDirtyMarginPx);
        RequestPaintRect(cardRect);
        if (entry.affectsPreview)
            RequestPaintCard(m_layout.previewCard);
        return;
    }

    RECT rect = control->rect;
    InflateRect(&rect, kDirtyMarginPx, kDirtyMarginPx);
    RequestPaintRect(rect);
}

void SettingsWindow::PaintPending()
{
    m_paintPosted = false;
    if (m_hwnd == nullptr)
    {
        ResetPaintState();
        return;
    }
    if (!m_dirtyFull &&
        (m_dirtyRect.right <= m_dirtyRect.left ||
         m_dirtyRect.bottom <= m_dirtyRect.top))
    {
        return;
    }

    const bool full = m_dirtyFull;
    RECT region = m_dirtyFull ? RECT{0, 0, m_width, m_height} : m_dirtyRect;
    m_dirtyFull = false;
    SetRectEmpty(&m_dirtyRect);
    PaintRegion(full, region);
}

void SettingsWindow::FlushPaint()
{
    if (m_hwnd == nullptr || m_width <= 0 || m_height <= 0)
        return;
    if (!m_dirtyFull &&
        (m_dirtyRect.right <= m_dirtyRect.left ||
         m_dirtyRect.bottom <= m_dirtyRect.top))
    {
        return;
    }
    PaintPending();
}

void SettingsWindow::ResetPaintState()
{
    m_dirtyFull = false;
    m_paintPosted = false;
    SetRectEmpty(&m_dirtyRect);
}

// Repaint modes (kept explicit so this stays auditable):
//   FULL  - the whole client rect is invalidated, drawn with NO clip, the whole
//           bitmap is cleared + redrawn and the whole DIB is premultiplied
//           before UpdateLayeredWindow. Used for: first paint, render-target
//           recreate / window resize, DPI or display change, work-area change,
//           theme switch, ReloadPending (external settings refresh), Apply and
//           WM_THEMECHANGED / ImmersiveColorSet.
//   PART  - exactly one dirty rectangle (hover/press feedback, the owning card
//           after a control action, the preview card when its text changes).
//           GDI+ is clipped to that rectangle and only that rectangle is
//           premultiplied into the persistent DIB; every other DIB pixel is
//           still valid from the previous frame, so bitmap and DIB stay
//           pixel-identical.
void SettingsWindow::PaintRegion(bool requestedFull, const RECT& requestedRegion)
{
    if (m_hwnd == nullptr || m_width <= 0 || m_height <= 0)
        return;

    const bool hadTarget = m_target.bitmap != nullptr &&
                           m_target.width == m_width &&
                           m_target.height == m_height;
    if (!m_target.Ensure(m_width, m_height))
        return;
    // A recreated render target has no valid pixels yet -> repaint everything.
    const bool full = requestedFull || !hadTarget;

    RECT region = full ? RECT{0, 0, m_width, m_height} : requestedRegion;
    region.left = std::max<LONG>(0, std::min<LONG>(region.left, m_width));
    region.top = std::max<LONG>(0, std::min<LONG>(region.top, m_height));
    region.right = std::max<LONG>(0, std::min<LONG>(region.right, m_width));
    region.bottom = std::max<LONG>(0, std::min<LONG>(region.bottom, m_height));
    if (region.right <= region.left || region.bottom <= region.top)
        return;

#ifdef _DEBUG
    OutputDebugStringW(full ? L"[settings] FULL repaint\n"
                            : L"[settings] PART repaint\n");
#endif

    Graphics& graphics = *m_target.graphics;
    const Rect clipRect(region.left, region.top, region.right - region.left,
                        region.bottom - region.top);
    if (full)
    {
        // Full repaint: no clip at all, clear the entire surface and redraw the
        // complete scene so no stale pixels can survive.
        graphics.ResetClip();
        graphics.Clear(Color(0, 0, 0, 0));
    }
    else
    {
        graphics.SetClip(clipRect, CombineModeReplace);
    }

    BuildLayout();

    const ThemePalette& palette = GetThemePaletteFor(m_pending.theme);
    const RectF paintBounds(static_cast<REAL>(clipRect.X),
                            static_cast<REAL>(clipRect.Y),
                            static_cast<REAL>(clipRect.Width),
                            static_cast<REAL>(clipRect.Height));
    Painter painter(graphics, m_fonts, m_scale, palette, m_hoverId, m_pressedId,
                    paintBounds, !full);
    DrawScene(painter);
    graphics.Flush();
    if (!full)
        graphics.ResetClip();

    // Premultiply the painted rectangle into the persistent DIB. The bitmap is
    // locked as a whole because GDI+ reports BitmapData::Scan0 relative to the
    // bitmap origin even for a sub-rectangle lock; the region offset is applied
    // manually here. Everything outside the region keeps the previous frame.
    BitmapData data = {};
    const Rect lockRect(0, 0, m_target.width, m_target.height);
    if (m_target.bitmap->LockBits(&lockRect, ImageLockModeRead,
                                  PixelFormat32bppARGB, &data) == Ok)
    {
        const BYTE* source = static_cast<const BYTE*>(data.Scan0);
        const int sourceStride = data.Stride;
        BYTE* destinationBase =
            static_cast<BYTE*>(m_target.dibPixels) +
            static_cast<size_t>(region.top) * static_cast<size_t>(m_target.stride) +
            static_cast<size_t>(region.left) * 4u;
        const int width = region.right - region.left;
        const int height = region.bottom - region.top;
        for (int y = 0; y < height; ++y)
        {
            const BYTE* sourceRow =
                source +
                static_cast<long long>(region.top + y) * sourceStride +
                static_cast<size_t>(region.left) * 4u;
            BYTE* destinationRow =
                destinationBase +
                static_cast<size_t>(y) * static_cast<size_t>(m_target.stride);
            for (int x = 0; x < width; ++x)
            {
                const size_t offset = static_cast<size_t>(x) * 4u;
                const BYTE alpha = sourceRow[offset + 3u];
                destinationRow[offset + 0u] =
                    PremultiplyChannel(sourceRow[offset + 0u], alpha);
                destinationRow[offset + 1u] =
                    PremultiplyChannel(sourceRow[offset + 1u], alpha);
                destinationRow[offset + 2u] =
                    PremultiplyChannel(sourceRow[offset + 2u], alpha);
                destinationRow[offset + 3u] = alpha;
            }
        }
        m_target.bitmap->UnlockBits(&data);
    }

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
    if (screenDc != nullptr)
    {
        UpdateLayeredWindow(m_hwnd, screenDc, &destinationPoint, &size,
                            m_target.memoryDc, &sourcePoint, 0, &blend,
                            ULW_ALPHA);
        ReleaseDC(nullptr, screenDc);
    }

    // Remember what the preview/timer state is now so the 1 s timer does not
    // repaint for unchanged content.
    m_lastPreviewText = FormatDisplayText(m_pending);
}

void SettingsWindow::OnTimerTick()
{
    if (m_hwnd == nullptr || m_dragging)
        return;

    // Preview clock: repaint the preview card only when the text changed.
    const std::wstring previewText = FormatDisplayText(m_pending);
    if (previewText != m_lastPreviewText)
    {
        m_lastPreviewText = previewText;
        if (m_layout.controls.empty())
            RequestPaint();
        else
            RequestPaintCard(m_layout.previewCard);
    }

    // Notification access can change while the dialog is open (the user grants
    // it in Windows Settings); refresh the status line when it does.
    if (kShowNotificationSettings)
    {
        const NotificationAccess access = GetNotificationAccessStatus();
        if (access != m_notificationAccess)
        {
            m_notificationAccess = access;
            if (m_layout.controls.empty())
                RequestPaint();
            else
                RequestPaintCard(m_layout.notifCard);
        }
    }
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
    if (hoverId == m_hoverId)
        return;

    // Only the previously and newly hovered controls need repainting.
    if (m_hoverId != kIdNone)
        RepaintControl(m_hoverId);
    m_hoverId = hoverId;
    if (hoverId != kIdNone)
        RepaintControl(hoverId);
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
        RepaintControl(m_hoverId);
        m_hoverId = kIdNone;
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
        RepaintControl(hit);
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
        RepaintControl(pressed);
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

    // Clear the hover highlight when the drag starts; the window is only moved
    // (never repainted) while dragging.
    if (m_hoverId != kIdNone)
    {
        RepaintControl(m_hoverId);
        m_hoverId = kIdNone;
    }

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
        RequestPaint();
        return;

    case kIdSystemTimeSettings:
        RepaintControl(kIdSystemTimeSettings); // drop the pressed state
        OpenDateAndTimeSettings();
        return;

    case kIdExitApp:
    {
        RepaintControl(kIdExitApp); // drop the pressed state before the dialog
        const int choice = MessageBoxW(
            m_hwnd,
            L"确定要退出灵动岛吗？\n\n退出后时间胶囊将从桌面消失，设置会保留。",
            L"退出程序", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (choice == IDYES)
            RequestApplicationExit();
        return;
    }

    case kIdUse24Hour:
        m_pending.use24Hour = !m_pending.use24Hour;
        break;
    case kIdShowSeconds:
        m_pending.showSeconds = !m_pending.showSeconds;
        break;
    case kIdShowAmPm:
        m_pending.showAmPm = !m_pending.showAmPm;
        break;

    // A theme change re-colours every pixel of the window.
    case kIdThemeDark:
        m_pending.theme = ISLAND_THEME_DARK;
        RequestPaint();
        return;
    case kIdThemeLight:
        m_pending.theme = ISLAND_THEME_LIGHT;
        RequestPaint();
        return;
    case kIdThemeSystem:
        m_pending.theme = ISLAND_THEME_SYSTEM;
        RequestPaint();
        return;

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

    // Notification card controls (v5.2).
    case kIdNotifyEnable:
        m_pending.notifyEnable = !m_pending.notifyEnable;
        break;
    case kIdNotifyShowContent:
        m_pending.notifyShowContent = !m_pending.notifyShowContent;
        break;
    case kIdNotifyWeChat:
        m_pending.notifyWeChat = !m_pending.notifyWeChat;
        break;
    case kIdNotifyQQ:
        m_pending.notifyQQ = !m_pending.notifyQQ;
        break;
    case kIdNotifyPeekMinus:
        m_pending.notifyPeekSeconds = ClampIntValue(
            m_pending.notifyPeekSeconds - 1, kNotifyPeekSecondsMin,
            kNotifyPeekSecondsMax);
        break;
    case kIdNotifyPeekPlus:
        m_pending.notifyPeekSeconds = ClampIntValue(
            m_pending.notifyPeekSeconds + 1, kNotifyPeekSecondsMin,
            kNotifyPeekSecondsMax);
        break;
    case kIdNotifyAccess:
        RepaintControl(kIdNotifyAccess); // drop the pressed state
        if (kShowNotificationSettings)
            RequestNotificationAccessFromUi();
        return;

    default:
        return;
    }

    // Repaint only the owning card (and the preview when relevant) instead of
    // the whole window.
    RepaintAfterAction(id);
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
        if (wParam == kPreviewTimerId)
            OnTimerTick();
        return 0;

    case kMsgPaintPending:
        // Coalesced paint request: one repaint per message-loop cycle.
        PaintPending();
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
        const int pressed = m_pressedId;
        const bool wasBusy = (pressed != kIdNone) || m_dragging;
        m_pressedId = kIdNone;
        m_dragging = false;
        if (wasBusy && m_hwnd != nullptr)
        {
            if (pressed != kIdNone)
                RepaintControl(pressed);
            else
                RequestPaint();
        }
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
            RequestPaint();
        }
        return 0;

    case WM_THEMECHANGED:
        RefreshSystemThemeCache();
        RequestPaint();
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
