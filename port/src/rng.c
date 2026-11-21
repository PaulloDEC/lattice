#include "rng.h"
#include "state.h"

static int16_t table[RNG_TABLE_SIZE];   /* DS:711A */
static uint16_t index_;                 /* DS:7118 */

int rng_load(const uint8_t *e, uint32_t size)
{
    if (size != 2 * RNG_TABLE_SIZE) return -1;
    for (int i = 0; i < RNG_TABLE_SIZE; i++)
        table[i] = (int16_t)(e[2 * i] | (e[2 * i + 1] << 8));
    return 0;
}
void rng_reset(void)        { index_ = 0; }
int  rng_index(void)        { return index_; }
void rng_set_index(int i)   { index_ = (uint16_t)i; }

int rng_next(int n)
{
    index_++;
    if ((int16_t)index_ > 0x3E7) index_ = 0;          /* original: cmp ..., 3E7 ; jle */
    return (int16_t)(table[index_] % (int16_t)n);     /* cwd + idiv: signed remainder */
}

void rng_state(state_visit_fn visit) { visit(table, sizeof table); visit(&index_, sizeof index_); }
