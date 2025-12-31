/*
 * timer.h - Timer header stub for Atari ST/MiNT
 */

#ifndef TIMER_H
#define TIMER_H

// Minimal class definitions for types used in main source
class TimerClass {
public:
	TimerClass(int timer = 0, int start = 0) {}
	long Set(long value, int start = 1) { return 0; }
	long Stop(void) { return 0; }
	long Start(void) { return 0; }
	long Reset(int start = 1) { return 0; }
	long Time(void) { return 0; }
};

class CountDownTimerClass : private TimerClass {
public:
	CountDownTimerClass(int timer = 0, int start = 0) : TimerClass(timer, start) {}
	long Set(long value, int start = 1) { return TimerClass::Set(value, start); }
	long Time(void) { return TimerClass::Time(); }
};

// Stub - needs implementation

#endif /* TIMER_H */

