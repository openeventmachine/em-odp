/*
 *   Copyright (c) 2015, Nokia Solutions and Networks
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * Env bit mask functions - don't include this file directly,
 * instead #include "environment.h"
 */

#ifndef _ENV_BITMASK_H_
#define _ENV_BITMASK_H_

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/*
 * Type for a bit mask.
 */
typedef struct {
	odp_cpumask_t odp_cpumask;
} env_bitmask_t;

/**
 * Zero the whole mask.
 *
 * @param mask      Bit mask
 */
static inline void env_bitmask_zero(env_bitmask_t *mask)
{
	odp_cpumask_zero(&mask->odp_cpumask);
}

/**
 * Set a bit in the mask.
 *
 * @param bit      Bit id
 * @param mask      Bit mask
 */
static inline void env_bitmask_set(int bit, env_bitmask_t *mask)
{
	odp_cpumask_set(&mask->odp_cpumask, bit);
}

/**
 * Clear a bit in the mask.
 *
 * @param bit      Bit id
 * @param mask      Bit mask
 */
static inline void env_bitmask_clr(int bit, env_bitmask_t *mask)
{
	odp_cpumask_clr(&mask->odp_cpumask, bit);
}

/**
 * Test if a bit is set in the mask.
 *
 * @param bit      Bit id
 * @param mask      Bit mask
 *
 * @return Non-zero if bit id is set in the mask
 */
static inline int env_bitmask_isset(int bit, const env_bitmask_t *mask)
{
	return odp_cpumask_isset(&mask->odp_cpumask, bit);
}

/**
 * Test if the mask is all zero.
 *
 * @param mask      Bit mask
 *
 * @return Non-zero if the mask is all zero
 */
static inline int env_bitmask_iszero(const env_bitmask_t *mask)
{
	odp_cpumask_t zero_mask;

	odp_cpumask_zero(&zero_mask);

	return odp_cpumask_equal(&zero_mask, &mask->odp_cpumask);
}

/**
 * Test if two masks are equal
 *
 * @param mask1     First bit mask
 * @param mask2     Second bit mask
 *
 * @return Non-zero if the two masks are equal
 */
static inline int env_bitmask_equal(const env_bitmask_t *mask1,
				    const env_bitmask_t *mask2)
{
	return odp_cpumask_equal(&mask1->odp_cpumask, &mask2->odp_cpumask);
}

/**
 * Set a range (0...count-1) of bits in the mask.
 *
 * @param count     Number of bits to set
 * @param mask      Bit mask
 */
static inline void env_bitmask_set_count(int count, env_bitmask_t *mask)
{
	int i;

	for (i = 0; i < count; i++)
		odp_cpumask_set(&mask->odp_cpumask, i);
}

/**
 * Copy bit mask
 *
 * @param dst       Destination bit mask
 * @param src       Source bit mask
 */
static inline void env_bitmask_copy(env_bitmask_t *dst,
				    const env_bitmask_t *src)
{
	odp_cpumask_copy(&dst->odp_cpumask, &src->odp_cpumask);
}

/**
 * Count the number of bits set in the mask.
 *
 * @param mask      Bit mask
 *
 * @return Number of bits set
 */
static inline int env_bitmask_count(const env_bitmask_t *mask)
{
	return odp_cpumask_count(&mask->odp_cpumask);
}

/**
 * Set specified bits from 'bits[]' in bit mask.
 *
 * bit 0:  bits[0] = 0x1 (len = 1)
 * bit 1:  bits[0] = 0x2 (len = 1)
 * ...
 * bit 64: bits[0] = 0x0, bits[1] = 0x1 (len = 2)
 * bit 65: bits[0] = 0x0, bits[1] = 0x2 (len = 2)
 * ...
 * cores 0-127: bits[0]=0xffffffffffffffff, bits[1]=0xffffffffffffffff (len=2)
 * ...
 * @param bits[] array of uint64_t:s containing the bits to set in the bit mask
 * @param len    number of array elements in bits[].
 * @param mask   bit mask to set.
 *
 * @note bits ar 'or'ed into mask, so any previously set bits will remain set.
 */
static inline void env_bitmask_set_bits(const uint64_t bits[], int len,
					env_bitmask_t *mask)
{
	(void)bits;
	(void)len;
	(void)mask;

	fprintf(stderr, "%s() function not implemented!\n", __func__);
}

/**
 * Get bit mask, stored in a uint64_t array for the user
 *
 * bit 0:  bits[0] = 0x1 (len = 1)
 * bit 1:  bits[0] = 0x2 (len = 1)
 * ...
 * bit 64: bits[0] = 0x0, bits[1] = 0x1 (len = 2)
 * bit 65: bits[0] = 0x0, bits[1] = 0x2 (len = 2)
 * ...
 * cores 0-127: bits[0]=0xffffffffffffffff, bits[1]=0xffffffffffffffff (len=2)
 * ...
 * @param[out] bits[] array of uint64_t:s that the bit mask will be stored in
 * @param      len    number of array elements in bits[].
 * @param      mask   bit mask to get bits from.
 *
 * @return  The number of uint64_t:s written into bits[].
 */
static inline int env_bitmask_get_bits(uint64_t bits[/*out*/], int len,
				       const env_bitmask_t *mask)
{
	(void)bits;
	(void)len;
	(void)mask;

	fprintf(stderr, "%s() function not implemented!\n", __func__);

	return 0;
}

/**
 * Return the index (position) of the Nth set bit in the bit mask
 *
 * @param n     Nth set bit, note n=1 means first set bit, n=[1...MaxCores]
 * @param mask  bit mask
 *
 * @return  Index of the Nth set bit, <0 on error or if no such bit.
 */
static inline int env_bitmask_idx(int n, const env_bitmask_t *mask)
{
	if (unlikely((unsigned int)(n - 1) >= ODP_CPUMASK_SIZE))
		return -1;

	int i = 1;
	int cpu = odp_cpumask_first(&mask->odp_cpumask);

	while (cpu >= 0 && i < n) {
		cpu = odp_cpumask_next(&mask->odp_cpumask, cpu);
		i++;
	}

	return cpu;
}

/**
 * Bitwise AND operation on two masks, store the result in 'dst'
 *
 * dst = src1 & src2
 *
 * @param dst    destination bit mask, result is stored here
 * @param src1   source mask #1
 * @param scr2   source mask #2
 */
static inline void env_bitmask_and(env_bitmask_t *dst,
				   const env_bitmask_t *src1,
				   const env_bitmask_t *src2)
{
	odp_cpumask_and(&dst->odp_cpumask,
			&src1->odp_cpumask, &src2->odp_cpumask);
}

/**
 * Bitwise OR operation on two masks, store the result in 'dst'
 *
 * dst = src1 | src2
 *
 * @param dst    destination bit mask, result is stored here
 * @param src1   source mask #1
 * @param scr2   source mask #2
 */
static inline void env_bitmask_or(env_bitmask_t *dst,
				  const env_bitmask_t *src1,
				  const env_bitmask_t *src2)
{
	odp_cpumask_or(&dst->odp_cpumask,
		       &src1->odp_cpumask, &src2->odp_cpumask);
}

/**
 * Bitwise XOR operation on two masks, store the result in 'dst'
 *
 * dst = src1 ^ src2
 *
 * @param dst    destination bit mask, result is stored here
 * @param src1   source mask #1
 * @param scr2   source mask #2
 */
static inline void env_bitmask_xor(env_bitmask_t *dst,
				   const env_bitmask_t *src1,
				   const env_bitmask_t *src2)
{
	odp_cpumask_xor(&dst->odp_cpumask,
			&src1->odp_cpumask, &src2->odp_cpumask);
}

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* _ENV_BITMASK_H_ */
