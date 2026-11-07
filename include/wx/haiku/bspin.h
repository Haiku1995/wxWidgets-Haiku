#ifndef SPIN_BUTTON_H
#define SPIN_BUTTON_H

#include <Control.h>
#include <Rect.h>

class SpinButton : public BControl {
public:
							// Standard constructor
							SpinButton(BRect frame,
								const char* name,
								int32 value = 0,
								int32 min = 0,
								int32 max = 100,
								int32 step = 1,
								uint32 resizingMode = B_FOLLOW_LEFT | B_FOLLOW_TOP,
								uint32 flags = B_WILL_DRAW | B_NAVIGABLE);

							// Archive constructor for full Haiku compliance
							SpinButton(BMessage* archive);

	virtual					~SpinButton();

	// BArchivable
	static	BArchivable*	Instantiate(BMessage* data);
	virtual	status_t		Archive(BMessage* data, bool deep = true) const;

	// BView / BControl overrides
	virtual	void			AttachedToWindow();
	virtual	void			Draw(BRect updateRect);
	virtual	void			MouseDown(BPoint point);
	virtual	void			MouseUp(BPoint point);
	virtual	void			MouseMoved(BPoint point, uint32 transit, const BMessage* dragMessage);
	virtual	void			KeyDown(const char* bytes, int32 numBytes);
	virtual	void			MakeFocus(bool focused = true);
	virtual	void			SetEnabled(bool enabled);

	// Value handling
	virtual	void			SetValue(int32 value);	// override from BControl
			int32			Value() const { return fValue; }
			void			SetRange(int32 min, int32 max);
			void			SetStep(int32 step);
            void SetWrap(bool wrap) { fWrap = wrap; }
protected:
            virtual bool NotifyValueChanging(int32, bool) { return true; }


private:
			void			_DrawUpButton(bool pressed, bool enabled);
			void			_DrawDownButton(bool pressed, bool enabled);
			BRect			_UpButtonRect() const;
			BRect			_DownButtonRect() const;
			void			_SetValueInternal(int64 value, bool invoke = true);

			// State
			int32			fValue;
			int32			fMin;
			int32			fMax;
			int32			fStep;

            bool fWrap = false;
			bool			fUpPressed;
			bool			fDownPressed;
			bool			fUpHovered;
			bool			fDownHovered;
};

#endif // SPIN_BUTTON_H
