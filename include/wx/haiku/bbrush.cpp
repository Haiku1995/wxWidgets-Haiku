#include "bbrush.h"

// Default Constructor (Transparent Brush)
BBrush::BBrush() : m_type(BRUSH_SOLID), m_color({0, 0, 0, 0}), m_pattern(nullptr), m_radialGradient(false) {}

// Solid Color Brush
BBrush::BBrush(rgb_color color) : m_type(BRUSH_SOLID), m_color(color), m_pattern(nullptr), m_radialGradient(false) {}

// Pattern Brush
BBrush::BBrush(BBitmap* pattern) : m_type(BRUSH_PATTERN), m_color({0, 0, 0, 255}), m_pattern(pattern), m_radialGradient(false) {}

// Gradient Brush
BBrush::BBrush(rgb_color startColor, rgb_color endColor, bool radial)
    : m_type(radial ? BRUSH_RADIAL_GRADIENT : BRUSH_LINEAR_GRADIENT), 
      m_startColor(startColor), m_endColor(endColor), m_pattern(nullptr), m_radialGradient(radial) {}

// Set Brush Color
void BBrush::SetColor(rgb_color color) {
    m_type = BRUSH_SOLID;
    m_color = color;
    m_pattern = nullptr;
}

// Set Brush Pattern
void BBrush::SetPattern(BBitmap* pattern) {
    m_type = BRUSH_PATTERN;
    m_pattern = pattern;
}

// Set Gradient Fill
void BBrush::SetGradient(rgb_color start, rgb_color end, bool radial) {
    m_type = radial ? BRUSH_RADIAL_GRADIENT : BRUSH_LINEAR_GRADIENT;
    m_startColor = start;
    m_endColor = end;
    m_radialGradient = radial;
}

// Apply Brush Properties to a BView Before Filling
void BBrush::ApplyTo(BView* view, BRect area) const {
    if (!view) return;

    switch (m_type) {
        case BRUSH_SOLID:
            view->SetHighColor(m_color);
            view->FillRect(area);
            break;
        case BRUSH_PATTERN:
            if (m_pattern) {
                for (float x = area.left; x < area.right; x += m_pattern->Bounds().Width())
                    for (float y = area.top; y < area.bottom; y += m_pattern->Bounds().Height())
                        view->DrawBitmap(m_pattern, BPoint(x, y));
            }
            break;
        case BRUSH_LINEAR_GRADIENT:
        case BRUSH_RADIAL_GRADIENT:
            DrawGradient(view, area);
            break;
        case BRUSH_HATCH:
            DrawHatchPattern(view, area);
            break;
    }
}

// Draw a Linear or Radial Gradient
void BBrush::DrawGradient(BView* view, BRect area) const {
    int steps = 50;
    float stepSize = area.Height() / steps;

    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / steps;
        rgb_color blendColor = {
            (uint8_t)(m_startColor.red * (1 - t) + m_endColor.red * t),
            (uint8_t)(m_startColor.green * (1 - t) + m_endColor.green * t),
            (uint8_t)(m_startColor.blue * (1 - t) + m_endColor.blue * t),
            255
        };
        view->SetHighColor(blendColor);
        view->FillRect(BRect(area.left, area.top + stepSize * i, area.right, area.top + stepSize * (i + 1)));
    }
}

// Draw a Hatch Pattern (Diagonal Lines)
void BBrush::DrawHatchPattern(BView* view, BRect area) const {
    view->SetHighColor({0, 0, 0, 255}); // Black hatch lines
    for (float x = area.left; x < area.right; x += 10) {
        view->StrokeLine(BPoint(x, area.top), BPoint(x + area.Height(), area.bottom));
    }
}
