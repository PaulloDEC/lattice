/*
 * rng.h - Hocus Pocus random numbers (original: 1392:0617, 112 callers, plus two inlined
 * copies in 0BA5 at 2D02 and 2E47).
 *
 * The "generator" is a lookup table: 1000 signed 16-bit words loaded from HOCUS.DAT
 * entry 6 (2000 bytes, values 1..997) into DS:711A.  State is one index (DS:7118):
 *     index++ ; if (index > 999) index = 0 ; return table[index] % n   (signed, C-style)
 * The main loop sets index = 0 at the start of every level (0BA5:4D0D) and the game-start
 * code does the same (0548:02F1), so the sequence is the same every time a level begins.
 */
#ifndef HOCUS_RNG_H
#define HOCUS_RNG_H
#include <stdint.h>

#define RNG_TABLE_SIZE 1000
int  rng_load(const uint8_t *entry6, uint32_t size);  /* 0 on success, -1 if size != 2000 */
void rng_reset(void);                                  /* index = 0 */
int  rng_next(int n);                                  /* 1392:0617; n must be non-zero */
int  rng_index(void);                                  /* DS:7118 */
void rng_set_index(int i);
#endif
