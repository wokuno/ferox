#ifndef FEROX_CACHELINE_H
#define FEROX_CACHELINE_H

#include <stddef.h>

#ifndef FEROX_CACHELINE_SIZE
#if defined(__APPLE__) && defined(__aarch64__)
#define FEROX_CACHELINE_SIZE 128
#else
#define FEROX_CACHELINE_SIZE 64
#endif
#endif

#define FEROX_CACHELINE_ALIGN _Alignas(FEROX_CACHELINE_SIZE)

#define FEROX_CACHELINE_ASSERT_MEMBER_ALIGNED(type, member) \
    _Static_assert((offsetof(type, member) % FEROX_CACHELINE_SIZE) == 0, \
                   #type "." #member " should start on a cacheline boundary")

#define FEROX_CACHELINE_ASSERT_SIZE_MULTIPLE(type) \
    _Static_assert((sizeof(type) % FEROX_CACHELINE_SIZE) == 0, \
                   #type " should occupy a whole number of cachelines")

#endif // FEROX_CACHELINE_H
