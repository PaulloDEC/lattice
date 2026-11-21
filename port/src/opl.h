/* opl.h - YM3812 (OPL2) emulation for the AdLib music: register writes in, 16-bit mono samples out (49716 Hz native rate). */
#ifndef HOCUS_OPL_H
#define HOCUS_OPL_H
#include <stdint.h>
#define OPL_RATE 49716
void opl_reset(void);
void opl_write(int reg, int val);
int  opl_sample(void);              /* next sample at OPL_RATE, roughly -32768..32767 */
#endif
