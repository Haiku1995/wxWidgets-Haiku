#include "wx/haiku/bspin.h"

#include <Window.h>
#include <View.h>
#include <Message.h>
//#include <ControlPrivate.h> // optional (for native look)
#include <stdio.h>

const float kButtonHeightRatio = 0.5f;

SpinButton::SpinButton(BRect frame, const char* name,
	int32 value, int32 min, int32 max, int32 step,
	uint32 resizingMode, uint32 flags)
	:
	BControl(frame, name, "", new BMessage('spnb'), resizingMode, flags),
	fValue(value),
	fMin(min),
	fMax(max),
	fStep(step),
	fUpPressed(false),
	fDownPressed(false),
	fUpHovered(false),
	fDownHovered(false)
{
	if (fValue < fMin) fValue = fMin;
	if (fValue > fMax) fValue = fMax;
	SetValue(fValue);
}


SpinButton::SpinButton(BMessage* archive)
	:
	BControl(archive)
{
	archive->FindInt32("value", &fValue);
	archive->FindInt32("min", &fMin);
	archive->FindInt32("max", &fMax);
	archive->FindInt32("step", &fStep);

	fUpPressed = fDownPressed = fUpHovered = fDownHovered = false;
}


SpinButton::~SpinButton()
{
}


void
SpinButton::AttachedToWindow()
{
	BControl::AttachedToWindow();
	if (Parent())
		SetViewColor(Parent()->ViewColor());
}


void
SpinButton::Draw(BRect updateRect)
{
	SetHighColor(ViewColor());
	FillRect(Bounds());

	bool enabled = IsEnabled();

	_DrawUpButton(fUpPressed, enabled);
	_DrawDownButton(fDownPressed, enabled);

	SetHighColor(tint_color(ViewColor(), B_DARKEN_2_TINT));
	StrokeRect(Bounds());
}


BRect
SpinButton::_UpButtonRect() const
{
	BRect bounds = Bounds();
	bounds.bottom = bounds.top + (bounds.Height() * kButtonHeightRatio);
	return bounds;
}


BRect
SpinButton::_DownButtonRect() const
{
	BRect bounds = Bounds();
	bounds.top = bounds.top + (bounds.Height() * kButtonHeightRatio) + 1;
	return bounds;
}


void
SpinButton::_DrawUpButton(bool pressed, bool enabled)
{
	BRect rect = _UpButtonRect();
	rgb_color base = enabled
		? ui_color(B_PANEL_BACKGROUND_COLOR)
		: tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DISABLED_LABEL_TINT);

	if (pressed || fUpHovered)
		base = tint_color(base, B_DARKEN_1_TINT);

	SetHighColor(base);
	FillRect(rect);

	SetHighColor(enabled
		? ui_color(B_CONTROL_TEXT_COLOR)
		: tint_color(ui_color(B_CONTROL_TEXT_COLOR), B_DISABLED_LABEL_TINT));

	float centerX = rect.left + rect.Width() / 2;
	float top = rect.top + 4;
	float bottom = rect.bottom - 2;
	float width = 6;

	BeginLineArray(3);
	AddLine(BPoint(centerX, top), BPoint(centerX - width, bottom), HighColor());
	AddLine(BPoint(centerX, top), BPoint(centerX + width, bottom), HighColor());
	AddLine(BPoint(centerX - width, bottom), BPoint(centerX + width, bottom), HighColor());
	EndLineArray();
}


void
SpinButton::_DrawDownButton(bool pressed, bool enabled)
{
	BRect rect = _DownButtonRect();
	rgb_color base = enabled
		? ui_color(B_PANEL_BACKGROUND_COLOR)
		: tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DISABLED_LABEL_TINT);

	if (pressed || fDownHovered)
		base = tint_color(base, B_DARKEN_1_TINT);

	SetHighColor(base);
	FillRect(rect);

	SetHighColor(enabled
		? ui_color(B_CONTROL_TEXT_COLOR)
		: tint_color(ui_color(B_CONTROL_TEXT_COLOR), B_DISABLED_LABEL_TINT));

	float centerX = rect.left + rect.Width() / 2;
	float top = rect.top + 2;
	float bottom = rect.bottom - 4;
	float width = 6;

	BeginLineArray(3);
	AddLine(BPoint(centerX, bottom), BPoint(centerX - width, top), HighColor());
	AddLine(BPoint(centerX, bottom), BPoint(centerX + width, top), HighColor());
	AddLine(BPoint(centerX - width, top), BPoint(centerX + width, top), HighColor());
	EndLineArray();
}


void
SpinButton::MouseDown(BPoint point)
{
	if (!IsEnabled())
		return;

	BRect upRect = _UpButtonRect();
	BRect downRect = _DownButtonRect();

	if (upRect.Contains(point)) {
		fUpPressed = true;
		Invalidate(upRect);
		_SetValueInternal(int64(fValue) + fStep);
	} else if (downRect.Contains(point)) {
		fDownPressed = true;
		Invalidate(downRect);
		_SetValueInternal(int64(fValue) - fStep);
	}

	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}


void
SpinButton::MouseUp(BPoint point)
{
	if (fUpPressed)
		Invalidate(_UpButtonRect());
	if (fDownPressed)
		Invalidate(_DownButtonRect());

	fUpPressed = fDownPressed = false;
}


void
SpinButton::MouseMoved(BPoint point, uint32 transit, const BMessage* dragMessage)
{
	BRect upRect = _UpButtonRect();
	BRect downRect = _DownButtonRect();

	bool newUpHover = upRect.Contains(point);
	bool newDownHover = downRect.Contains(point);

	if (newUpHover != fUpHovered || newDownHover != fDownHovered) {
		fUpHovered = newUpHover;
		fDownHovered = newDownHover;
		Invalidate();
	}
}


void
SpinButton::KeyDown(const char* bytes, int32 numBytes)
{
	if (!IsEnabled() || numBytes != 1)
		return BControl::KeyDown(bytes, numBytes);

	switch (bytes[0]) {
		case B_UP_ARROW:
			_SetValueInternal(int64(fValue) + fStep);
			break;
		case B_DOWN_ARROW:
			_SetValueInternal(int64(fValue) - fStep);
			break;
		default:
			BControl::KeyDown(bytes, numBytes);
			return;
	}

	Invalidate();
}


void
SpinButton::MakeFocus(bool focused)
{
	BControl::MakeFocus(focused);
	Invalidate();
}


void
SpinButton::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);
	Invalidate();
}


void
SpinButton::SetValue(int32 value)
{
	_SetValueInternal(value, false);
}


void
SpinButton::SetRange(int32 min, int32 max)
{
	fMin = min;
	fMax = max;
	if (fValue < fMin)
		SetValue(fMin);
	else if (fValue > fMax)
		SetValue(fMax);
}


void
SpinButton::SetStep(int32 step)
{
	fStep = (step > 0) ? step : 1;
}


void
SpinButton::_SetValueInternal(int64 value, bool invoke)
{
	int32 oldValue = fValue;

    const bool up = value > oldValue;
    if (invoke && fWrap) {
        if (value < fMin) value = fMax;
        else if (value > fMax) value = fMin;
    }
    if (value < fMin) value = fMin;
    if (value > fMax) value = fMax;
    if (value == fValue) return;
    if (invoke && !NotifyValueChanging(value, up)) return;
    fValue = value;
    BControl::SetValue(value);
    if (invoke) Invoke();

	Invalidate();
}


status_t
SpinButton::Archive(BMessage* data, bool deep) const
{
	status_t status = BControl::Archive(data, deep);
	if (status != B_OK)
		return status;

	data->AddString("class", "SpinButton");
	data->AddInt32("value", fValue);
	data->AddInt32("min", fMin);
	data->AddInt32("max", fMax);
	data->AddInt32("step", fStep);

	return B_OK;
}


BArchivable*
SpinButton::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "SpinButton"))
		return NULL;
	return new SpinButton(data);
}
