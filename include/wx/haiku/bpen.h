#ifndef _B_PEN_H_
#define _B_PEN_H_

#include <InterfaceDefs.h>
#include <View.h>
#include <GraphicsDefs.h>
#include <Bitmap.h>

class BPen {
public:
    // Constructors
    BPen();
    BPen(rgb_color color, float width = 1.0, int32 style = B_SOLID_HIGH);

    // Setters
    void SetColor(rgb_color color);
    void SetColor(uchar r, uchar g, uchar b, uchar a = 255);
    void SetWidth(float width);
    void SetStyle(int32 style);
    void SetStipple(BBitmap* stipple);

    // Getters
    rgb_color Color() const;
    float Width() const;
    int32 Style() const;
    BBitmap* Stipple() const;

    // Apply the pen settings to a BView before drawing
    void ApplyTo(BView* view) const;

private:
    rgb_color m_color;
    float m_width;
    int32 m_style;
    BBitmap* m_stipple;
};

#endif // _B_PEN_H_
