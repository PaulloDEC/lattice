#define _POSIX_C_SOURCE 200809L
#include "timer.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

const uint16_t g_speed_interval[3] = { 8, 7, 6 };

static timer_now_ns_fn now_fn;
static timer_idle_fn   idle_fn;
static uint64_t t0_ns;          /* time at which clock == base */
static uint32_t base;
static uint32_t last;

#ifdef _WIN32
static uint64_t default_now(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    /* split to avoid 64-bit overflow of counter * 1e9 */
    return (uint64_t)(c.QuadPart / f.QuadPart) * 1000000000ull
         + (uint64_t)(c.QuadPart % f.QuadPart) * 1000000000ull / (uint64_t)f.QuadPart;
}
static void default_idle(void) { Sleep(0); }   /* yield; the main loop may SDL_Delay instead */
#else
static uint64_t default_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static void default_idle(void)
{
    struct timespec ts = { 0, 500000 };   /* 0.5 ms */
    nanosleep(&ts, 0);
}
#endif

void timer_init(timer_now_ns_fn now, timer_idle_fn idle)
{
    now_fn  = now  ? now  : default_now;
    idle_fn = idle ? idle : default_idle;
    timer_reset();
}

void timer_reset(void)
{
    t0_ns = now_fn();
    base = 0;
    last = 0;
}

uint32_t timer_clock(void)
{
    uint64_t dt = now_fn() - t0_ns;
    return base + (uint32_t)(dt * TIMER_HZ / 1000000000ull);
}

void timer_set_clock(uint32_t v)
{
    t0_ns = now_fn();
    base = v;
}

void timer_frame_wait(int speed)
{
    int32_t interval = g_speed_interval[(speed >= 0 && speed <= 2) ? speed : 0];
    while ((int32_t)(timer_clock() - last) < interval)   /* signed 32-bit, as the original */
        idle_fn();
    last = timer_clock();
}
