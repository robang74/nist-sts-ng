/*******************************************************************************
 *
 * (C) 2026, Roberto A. Foglietta <roberto.foglietta@gmail.com>, MIT license
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *      TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
 *        THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ******************************************************************************/

#ifndef ALIGNED_MEMORY_C /* .c */
#ifndef ALIGNED_MEMORY_H /* .h */
#define ALIGNED_MEMORY_H

#ifdef __STDC_VERSION__
#   if __STDC_VERSION__ >= 201112L
#   include <stdalign.h>
#   define ALIGN8_BEFORE alignas(8)
#   define ALIGN8_AFTER
#   endif
#endif

#ifndef ALIGN8_BEFORE
#define ALIGN8_BEFORE
#define ALIGN8_AFTER __attribute__((aligned(8)))
#endif

#define ALIGNED_DATA_BUFFER(_type, _nme, _len) \
    ALIGN8_BEFORE _type _nme[_len] ALIGN8_AFTER

#define MEM_PAGE_SIZE (4096U)

void * aligned_malloc(size_t sze);
#if _POSIX_C_SOURCE >= 200112L
#define aligned_free free
#else
void aligned_free(void *ptr);
#endif

#endif /* .h */
#else /* .c */

#if _POSIX_C_SOURCE >= 200112L

#include <errno.h>
#include <malloc.h>
void * aligned_malloc(size_t sze)
{
    static const size_t asz = sizeof(void *);
    void *ptr = NULL;
    if (sze == 0) {
        errno = EINVAL;
    } else
    if (sze > SIZE_MAX - (asz - 1)) {
        errno = ENOMEM;
    } else {
        sze = (sze + asz - 1) & ~(asz - 1);
        errno = posix_memalign(&ptr, asz, sze);
        if (errno) ptr = NULL;
    }
    return ptr;
}

//#define aligned_free free

#else

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void * aligned_malloc(size_t sze)
{
    static const size_t asz = sizeof(void *); /* power of two >= sizeof(void *) */
    const size_t overhead = (asz - 1) + sizeof(void *);

    if (sze == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (sze > SIZE_MAX - overhead) {
        errno = ENOMEM;
        return NULL;
    }

    void *raw = malloc(sze + overhead);
    if (!raw) {
        return NULL;
    }

    uintptr_t start = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (start + (asz - 1)) & ~(uintptr_t)(asz - 1);

    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

void aligned_free(void *ptr)
{
    if (ptr) {
        free(((void **)ptr)[-1]);
    }
}

#endif

#endif /* .c */
