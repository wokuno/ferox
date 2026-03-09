#ifndef FEROX_PHASE_WAIT_H
#define FEROX_PHASE_WAIT_H

#include <stdatomic.h>

void phase_wait_backoff(int* spin_count);
void phase_wait_eq(atomic_int* value, int expected);
void phase_wake_all(atomic_int* value);

#endif // FEROX_PHASE_WAIT_H
