#include "phase_wait.h"

#include <errno.h>
#include <sched.h>
#include <time.h>

#ifdef __linux__
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

void phase_wait_backoff(int* spin_count) {
    (*spin_count)++;
    if (*spin_count < 96) {
        return;
    }
    if (*spin_count < 256) {
        sched_yield();
        return;
    }

    struct timespec pause = {0, 5000};
    nanosleep(&pause, NULL);
}

void phase_wait_eq(atomic_int* value, int expected) {
#ifdef __linux__
    while (atomic_load_explicit(value, memory_order_acquire) == expected) {
        int rc = (int)syscall(SYS_futex, (int*)value, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
        if (rc == -1 && errno != EAGAIN && errno != EINTR) {
            break;
        }
    }
#else
    int spin_count = 0;
    while (atomic_load_explicit(value, memory_order_acquire) == expected) {
        phase_wait_backoff(&spin_count);
    }
#endif
}

void phase_wake_all(atomic_int* value) {
#ifdef __linux__
    (void)syscall(SYS_futex, (int*)value, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
#else
    (void)value;
#endif
}
