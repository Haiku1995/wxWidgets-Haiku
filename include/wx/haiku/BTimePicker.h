#ifndef BTIMEPICKER_H
#define BTIMEPICKER_H

#include <TextControl.h>
#include <String.h>
#include <Message.h>
#include <ctime>

class BTimePicker : public BTextControl {
public:
    BTimePicker(BRect frame, const char* name, const char* label,
                uint32 resizingMode = B_FOLLOW_LEFT | B_FOLLOW_TOP,
                uint32 flags = B_WILL_DRAW | B_NAVIGABLE);
    virtual ~BTimePicker();

    void SetTime(const char* time);
    const char* Time() const;

    void SetMinimumTime(const char* time);
    void SetMaximumTime(const char* time);
    void SetDisplayFormat(const char* format);

    virtual void MessageReceived(BMessage* message) override;

private:
    BString fTime;
    BString fDisplayFormat;
    BString fMinimumTime;
    BString fMaximumTime;

    void _UpdateTime();
    void _InitializeTime();
    bool _IsTimeValid(const char* time) const;
};
