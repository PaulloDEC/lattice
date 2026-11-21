/* state.h - whole-game-state save / restore (run-ahead; later quicksave).  state.c lists every writable
 * variable of the game modules (found with nm on the objects); modules with private state expose it through
 * a *_state(visit) function that calls visit(pointer, size) for each private variable. */
#ifndef HOCUS_STATE_H
#define HOCUS_STATE_H
#include <stddef.h>
typedef void (*state_visit_fn)(void *p, size_t n);
void   vga_state(state_visit_fn visit);
void   video_state(state_visit_fn visit);
void   rng_state(state_visit_fn visit);
void   loader_state(state_visit_fn visit);

size_t state_size(void);
void   state_save(void *buf);          /* buf of state_size() bytes */
void   state_restore(const void *buf);
#endif
