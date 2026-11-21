/*
 * timer.h - Hocus Pocus game clock and frame pacing.
 *
 * Replaces the Apogee task manager (code segment 1505, task_man.c) together with the
 * clock task at 1392:000A, which did `++clock` 140 times a second into DS:7908
 * (32-bit, DS:7908 low / DS:790A high).  Also replaces the frame-wait loop at
 * 0BA5:4D67.
 *
 * Original facts (from the disassembly):
 *   - TS_ScheduleTask(fn=1392:000A, rate=0x8C, priority=1, data=DS:7908) -> 140 Hz.
 *   - Main loop: clock=0, last=0; each frame wait until (int32)(clock-last) >= interval,
 *     then last=clock and run one frame.  interval = table[speed] at DS:14F8 = {8,7,6}
 *     (speed var at DS:790E).  No catch-up if a frame overruns.
 */
#ifndef HOCUS_TIMER_H
#define HOCUS_TIMER_H
#include <stdint.h>

#define TIMER_HZ 140
extern const uint16_t g_speed_interval[3];   /* DS:14F8 : 8, 7, 6 ticks */

typedef uint64_t (*timer_now_ns_fn)(void);   /* monotonic nanoseconds */
typedef void (*timer_idle_fn)(void);         /* called while waiting */

void     timer_init(timer_now_ns_fn now, timer_idle_fn idle); /* NULL = defaults */
void     timer_reset(void);                  /* clock = 0, last = 0 (loop start) */
uint32_t timer_clock(void);                  /* DS:7908 equivalent, 140 Hz */
void     timer_set_clock(uint32_t v);        /* the game writes the clock (0BA5:4D36) */
void     timer_frame_wait(int speed);        /* 0BA5:4D67 */
/* Enhanced display smoothing: with `steady` on, a frame wait advances the schedule by exactly one interval
 * (instead of restarting it from the moment the wait noticed the tick), so a present that blocks on vsync
 * cannot slow the game down; more than one interval behind, it resynchronises.  Off = the original. */
void     timer_set_steady(int on);
double   timer_clock_frac(void);              /* the 140 Hz clock with its fraction */
uint32_t timer_last(void);                    /* clock value of the last tick the frame wait accepted */
#endif
