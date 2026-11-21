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

/* The FAT is compiled in from the Registered v1.1 HOCUS.EXE, so any other HOCUS.DAT (shareware, other
 * versions) would be read at the wrong offsets.  Returns NULL when the open file fits, else a message. */
const char *dat_check(void)
{
    if (!s_f) return "HOCUS.DAT is not open.";
    uint32_t need = 0;
    for (int i = 0; i < DAT_ENTRIES; i++) { uint32_t e = g_dat_fat[i][0] + g_dat_fat[i][1]; if (e > need) need = e; }
    if (fseek(s_f, 0, SEEK_END) != 0) return "Cannot read HOCUS.DAT.";
    long have = ftell(s_f);
    if (have < 0 || (uint32_t)have < need)
        return "This HOCUS.DAT is too small for the port - it is probably the shareware version.\n"
               "The port needs the HOCUS.DAT from the registered Hocus Pocus v1.1.";
    uint8_t b[2];                                     /* spot check: the first start-up picture (entry 0x11) is a PCX */
    if (fseek(s_f, (long)g_dat_fat[0x11][0], SEEK_SET) != 0 || fread(b, 1, 2, s_f) != 2 || b[0] != 0x0A || b[1] > 5)
        return "This HOCUS.DAT does not match the registered Hocus Pocus v1.1 that the port was built from.";
    return NULL;
}
