#include "bpen.h"

// Default Constructor
BPen::BPen() 
    : m_color({0, 0, 0, 255}), m_width(1.0), m_style(B_SOLID_HIGH), m_stipple(nullptr) {}

// Parameterized Constructor
BPen::BPen(rgb_color color, float width, int32 style) 
    : m_color(color), m_width(width), m_style(style), m_stipple(nullptr) {}

// Set Pen Color
void BPen::SetColor(rgb_color color) {
    m_color = color;
}

void BPen::SetColor(uchar r, uchar g, uchar b, uchar a) {
    m_color = {r, g, b, a};
}

// Set Pen Width
void BPen::SetWidth(float width) {
    m_width = width;
}

// Set Pen Style
void BPen::SetStyle(int32 style) {
    m_style = style;
}

// Set Stipple Pattern
void BPen::SetStipple(BBitmap* stipple) {
    m_stipple = stipple;
}

// Get Pen Color
rgb_color BPen::Color() const {
    return m_color;
}

// Get Pen Width
float BPen::Width() const {
    return m_width;
}

// Get Pen Style
int32 BPen::Style() const {
    return m_style;
}

// Get Stipple Pattern
BBitmap* BPen::Stipple() const {
    return m_stipple;
}

// Apply Pen Properties to a BView Before Drawing
void BPen::ApplyTo(BView* view) const {
    if (view) {
        view->SetHighColor(m_color);
        view->SetPenSize(m_width);
        // Haiku does not support native line styles, but we can simulate dashed lines with stipple
        if (m_stipple) {
            // Apply stipple pattern if available (implementation may require custom handling)
        }
    }
}
