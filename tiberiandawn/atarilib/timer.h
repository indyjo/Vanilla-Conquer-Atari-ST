/*
 * timer.h - Timer header stub for Atari ST/MiNT
 */

#ifndef TIMER_H
#define TIMER_H

/*=========================================================================*/
typedef enum BaseTimerEnum {
	BT_SYSTEM,			// System timer (60 / second).
	BT_USER				// User controllable timer (? / second).
} BaseTimerEnum;

// Minimal class definitions for types used in main source
class TimerClass {
public:
	TimerClass(BaseTimerEnum timer = BT_SYSTEM, int start = 0) {}
	long Set(long value, int start = 1) { return 0; }
	long Stop(void) { return 0; }
	long Start(void) { return 0; }
	long Reset(int start = 1) { return 0; }
	long Time(void) { return 0; }
};

class CountDownTimerClass : private TimerClass {
public:
	CountDownTimerClass(BaseTimerEnum timer = BT_SYSTEM, int start = 0) : TimerClass(timer, start) {}
	CountDownTimerClass(BaseTimerEnum timer, long set, int on = 0) : TimerClass(timer, on) {}
	long Set(long value, int start = 1) { return TimerClass::Set(value, start); }
	long Reset(int start = 1) { return TimerClass::Reset(start); }
	long Stop(void) { TimerClass::Stop(); return Time(); }
	long Start(void) { TimerClass::Start(); return Time(); }
	long Time(void) { return TimerClass::Time(); }
};

// Stub - needs implementation

#endif /* TIMER_H */

