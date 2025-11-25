/*
Copyright (c) 2003-2010, Mark Borgerding

All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.
    * Neither the author nor the names of any contributors may be used to
endorse or promote products derived from this software without specific prior
written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef KISS_FFT_H
#define KISS_FFT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Define memory allocation functions */
#ifndef KISS_FFT_MALLOC
#define KISS_FFT_MALLOC(nbytes) malloc(nbytes)
#endif

#ifndef KISS_FFT_FREE
#define KISS_FFT_FREE(ptr) free(ptr)
#endif

/* Define trig functions if not already defined */
#ifndef KISS_FFT_COS
#define KISS_FFT_COS(phase) (kiss_fft_scalar) cos(phase)
#endif

#ifndef KISS_FFT_SIN
#define KISS_FFT_SIN(phase) (kiss_fft_scalar) sin(phase)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * If you would like a :
 * - a utility that will handle the caching of fft objects
 * - real-only (no imaginary time component ) FFT
 * - a multi-dimensional FFT
 * - a command-line utility to perform ffts
 * - a command-line utility to perform fast-convolution filtering

 * Then see kfc.h kissfft.h tools/
 */

#ifdef FIXED_POINT
#include <sys/types.h>
#if (FIXED_POINT == 32)
#define kiss_fft_scalar int32_t
#else
#define kiss_fft_scalar int16_t
#endif
#else
#ifndef kiss_fft_scalar
/*  default is float */
#define kiss_fft_scalar float
#endif
#endif

/* Maximum number of factors in the factorization of n */
#define MAXFACTORS 32

typedef struct {
  kiss_fft_scalar r;
  kiss_fft_scalar i;
} kiss_fft_cpx;

struct kiss_fft_state {
  int nfft;
  int inverse;
  int factors[2 * MAXFACTORS];
  kiss_fft_cpx twiddles[1];
};

typedef struct kiss_fft_state *kiss_fft_cfg;

/* Define the macro used for cexp, which is called in kiss_fftr.c */
#ifndef kf_cexp
#define kf_cexp(x, phase)                                                      \
  do {                                                                         \
    (x)->r = KISS_FFT_COS(phase);                                              \
    (x)->i = KISS_FFT_SIN(phase);                                              \
  } while (0)
#endif

/*
 *  kiss_fft_alloc
 *
 *  Initialize a FFT (or IFFT) algorithm's cfg/state buffer.
 *
 *  typical usage:      kiss_fft_cfg mycfg=kiss_fft_alloc(1024,0,NULL,NULL);
 *
 *  The return value from fft_alloc is a cfg buffer used internally
 *  by the fft routine or NULL.
 *
 *  If lenmem is NULL, then kiss_fft_alloc will allocate a cfg buffer using
 * malloc. The returned value should be free'd when done with kiss_fft_free().
 *
 *  If lenmem is not NULL, then the user must supply a buffer of length *lenmem.
 *  If the buffer is too small, then *lenmem is set to the required length and
 * the call fails. In this case, the user should allocate a buffer of the
 * appropriate length, and call again.
 *
 *  The state can be placed in a user supplied buffer. This buffer must be
 * aligned to a 16-byte boundary.
 *
 *  If the user passes in a buffer, it must be of length at least *lenmem.
 */
kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void *mem,
                            size_t *lenmem);

/*
 * kiss_fft(cfg,in_out_buf)
 *
 * Perform an FFT on a complex input buffer.
 * for a forward FFT,
 * fin should be  f[0] , f[1] , ... ,f[nfft-1]
 * fout will be   F[0] , F[1] , ... ,F[nfft-1]
 * Note that each element is complex and can be accessed like
    f[k].r and f[k].i
 * */
void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin, kiss_fft_cpx *fout);

/*
 A more generic version of the above function. It reads its input from every Nth
 sample.
 * */
void kiss_fft_stride(kiss_fft_cfg cfg, const kiss_fft_cpx *fin,
                     kiss_fft_cpx *fout, int fin_stride);

/* If kiss_fft_alloc allocated a buffer, it is one contiguous
   buffer which can be freed from the start of the buffer.
   If kiss_fft_alloc used a user-supplied buffer, this will
   still work as the user-buffer will be at the start.
*/
#define kiss_fft_free(ptr) free(ptr)

/*
 Cleans up some memory that gets managed internally. Not necessary to call, but
 it might clean up your compiler output to call this before you exit.
*/
void kiss_fft_cleanup(void);

/*
 * Returns the smallest integer k, such that k>=n and k has only "fast" factors
 * (2,3,5)
 */
int kiss_fft_next_fast_size(int n);

/* for real ffts, we need an even size */
#define kiss_fftr_next_fast_size_real(n)                                       \
  (kiss_fft_next_fast_size(((n) + 1) >> 1) << 1)

#ifdef __cplusplus
}
#endif

#endif
