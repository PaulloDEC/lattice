#include "../src/timer.h"
#include <stdio.h>
static uint64_t fake_ns;
static uint64_t fnow(void){ return fake_ns; }
static void fidle(void){ fake_ns += 1000000; }          /* 1 ms per idle spin */
static int fails, checks;
#define CHECK(c) do{checks++; if(!(c)){fails++; printf("FAIL line %d: %s\n",__LINE__,#c);}}while(0)

int main(void){
    timer_init(fnow, fidle);
    /* 140 Hz: 1 second = 140 ticks */
    fake_ns = 0; timer_reset();
    CHECK(timer_clock()==0);
    fake_ns = 1000000000ull; CHECK(timer_clock()==140);
    fake_ns = 7142857ull;    CHECK(timer_clock()==0);   /* 1/140 s = 7.142857 ms */
    fake_ns = 7142858ull;    CHECK(timer_clock()==1);

    /* set_clock rebases */
    fake_ns = 5000000000ull; timer_set_clock(1000); CHECK(timer_clock()==1000);
    fake_ns += 1000000000ull; CHECK(timer_clock()==1140);

    /* frame pacing with zero work: 140 frames at interval i take 140/i ... check elapsed */
    for (int speed=0; speed<3; speed++){
        fake_ns = 0; timer_reset();
        for (int f=0; f<100; f++) timer_frame_wait(speed);
        double secs = fake_ns/1e9, want = 100.0*g_speed_interval[speed]/140.0;
        printf("speed %d: 100 frames in %.4f s (ideal %.4f s) = %.2f fps\n",
               speed, secs, want, 100.0/secs);
        CHECK(secs >= want - 0.0001 && secs <= want + 0.02);   /* within ~2 ticks */
    }

    /* overrun: a frame whose work takes longer than the interval is NOT caught up */
    fake_ns = 0; timer_reset(); timer_frame_wait(2);          /* interval 6 */
    uint64_t t_a = fake_ns;
    fake_ns += 100000000ull;                                   /* 100 ms of "work" = 14 ticks */
    timer_frame_wait(2);                                       /* already late: returns at once */
    CHECK(fake_ns == t_a + 100000000ull);
    uint64_t t_b = fake_ns;
    timer_frame_wait(2);                                       /* next frame waits a fresh 6 ticks */
    CHECK(fake_ns - t_b >= 6*7142857ull - 1000000ull && fake_ns - t_b <= 6*7142857ull + 2000000ull);

    printf("%d checks, %d failures\n", checks, fails);
    return fails!=0;
}
