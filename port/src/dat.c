#include "dat.h"
#include <stdio.h>
#include <stdlib.h>

static FILE *s_f;

int dat_open(const char *path)
{
    if (s_f) fclose(s_f);
    s_f = fopen(path, "rb");
    return s_f ? 0 : -1;
}
void dat_close(void) { if (s_f) fclose(s_f); s_f = NULL; }
uint32_t dat_size(int entry) { return (entry >= 0 && entry < DAT_ENTRIES) ? g_dat_fat[entry][1] : 0; }

uint32_t dat_read(int entry, void *dest, uint32_t cap)
{
    uint32_t n = dat_size(entry);
    if (!s_f || n == 0 || n > cap) return 0;
    if (fseek(s_f, (long)g_dat_fat[entry][0], SEEK_SET) != 0) return 0;
    return fread(dest, 1, n, s_f) == n ? n : 0;
}

uint8_t *dat_load(int entry, uint32_t *size)
{
    uint32_t n = dat_size(entry);
    if (!n) return NULL;
    uint8_t *b = malloc(n);
    if (!b) return NULL;
    if (dat_read(entry, b, n) != n) { free(b); return NULL; }
    if (size) *size = n;
    return b;
}
