// SPDX-License-Identifier: MIT
#ifndef NILAMP_CPU_H
#define NILAMP_CPU_H

#if ((defined(__i386__) || defined(__x86_64__)) && defined(__SSE__)) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif

static inline void nilamp_cpu_enable_realtime_float_mode(void)
{
#if ((defined(__i386__) || defined(__x86_64__)) && defined(__SSE__)) || defined(_M_IX86) || defined(_M_X64)
    enum {
        NILAMP_MXCSR_DAZ = 1u << 6,
        NILAMP_MXCSR_FTZ = 1u << 15,
    };
    _mm_setcsr(_mm_getcsr() | NILAMP_MXCSR_DAZ | NILAMP_MXCSR_FTZ);
#endif
}

#endif
