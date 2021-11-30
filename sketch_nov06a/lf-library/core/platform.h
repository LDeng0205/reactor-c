/* Platform API support for the C target of Lingua Franca. */

/*************
Copyright (c) 2021, The University of California at Berkeley.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************/

/**
 * Platform API support for the C target of Lingua Franca.
 * This file detects the platform on which the C compiler is being run
 * (e.g. Windows, Linux, Mac) and conditionally includes platform-specific
 * files that define core datatypes and function signatures for Lingua Franca.
 * For example, the type instant_t represents a time value (long long on
 * most of the platforms). The conditionally included files define a type
 * _instant_t, and this file defines the type instant_t to be whatever
 * the included defines _instant_t to be. All platform-independent code
 * in Lingua Franca, therefore, should use the type instant_t for time
 * values.
 *  
 * @author{Soroush Bateni <soroush@utdallas.edu>}
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include "platform/lf_arduino_platforms.h"

#if defined(BOARD)
    #include "platform/lf_arduino_support.h"
#else
    #error "Platform not supported"
#endif

/**
 * Time instant. Both physical and logical times are represented
 * using this typedef.
 */
typedef _instant_t instant_t;

/**
 * Interval of time.
 */
typedef _interval_t interval_t;

/**
 * Microstep instant.
 */
typedef _microstep_t microstep_t;

#ifdef __cplusplus
extern "C" {
    /**
     * Initialize the LF clock. Must be called before using other clock-related APIs.
     */
    extern void lf_initialize_clock();

    /**
     * Fetch the value of an internal (and platform-specific) physical clock and 
     * store it in `t`.
     * 
     * Ideally, the underlying platform clock should be monotonic. However, the
     * core lib tries to enforce monotonicity at higher level APIs (see tag.h).
     * 
     * @return 0 for success, or -1 for failure
     */
    extern int lf_clock_gettime(instant_t* t);

    /**
     * Pause execution for a number of nanoseconds.
     * 
     * @return 0 for success, or -1 for failure.
     */
    extern int lf_nanosleep(instant_t requested_time);
    }
#endif

#endif // PLATFORM_H
