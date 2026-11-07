#ifndef _B_BRUSH_H_
#define _B_BRUSH_H_

#include <InterfaceDefs.h>
#include <GraphicsDefs.h>
#include <View.h>
#include <Bitmap.h>

enum BBrushType {
    BRUSH_SOLID,
    BRUSH_PATTERN,
    BRUSH_LINEAR_GRADIENT,
    BRUSH_RADIAL_GRADIENT,
    BRUSH_HATCH
};

class BBrush {
public:
    // Constructors
    BBrush();
    BBrush(rgb_color color);
    BBrush(BBitmap* pattern);
    BBrush(rgb_color startColor, rgb_color endColor, bool radial = false); // Gradient

    // Setters
    void SetColor(rgb_color color);
    void SetPattern(BBitmap* pattern);
    void SetGradient(rgb_color start, rgb_color end, bool radial = false);
    void SetHatchPattern(int type);

    // Getters
    rgb_color Color() const;
    BBitmap* Pattern() const;
    bool IsRadialGradient() const;
    void GetGradientColors(rgb_color &start, rgb_color &end) const;

    // Apply brush settings to a BView
    void ApplyTo(BView* view, BRect area) const;

private:
    BBrushType m_type;
    rgb_color m_color;
    BBitmap* m_pattern;
    rgb_color m_startColor, m_endColor;
    bool m_radialGradient;

    void DrawGradient(BView* view, BRect area) const;
    void DrawHatchPattern(BView* view, BRect area) const;
};

#endif // _B_BRUSH_H_
