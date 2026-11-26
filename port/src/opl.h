/* opl.h - YM3812 (OPL2) emulation for the AdLib music: register writes in, 16-bit mono samples out (49716 Hz native rate). */
#ifndef HOCUS_OPL_H
#define HOCUS_OPL_H
#include <stdint.h>
#define OPL_RATE 49716
#define OPL_CHIPS 2                 /* two chips: stereo FM (left / right) */
void opl_reset(void);               /* both chips */
void opl_write(int reg, int val);   /* chip 0 */
int  opl_sample(void);              /* chip 0: next sample at OPL_RATE, roughly -32768..32767 */
void opl_write_chip(int chip, int reg, int val);
int  opl_sample_chip(int chip);
#endif
