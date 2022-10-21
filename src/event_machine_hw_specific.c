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
 *
 * Event Machine HW specific functions and other additions.
 *
 */
#include "em_include.h"

/*
 * core mask manipulation prototypes or inlined functions
 */

void em_core_mask_zero(em_core_mask_t *mask)
{
	odp_cpumask_zero(&mask->odp_cpumask);
}

void em_core_mask_set(int core, em_core_mask_t *mask)
{
	odp_cpumask_set(&mask->odp_cpumask, core);
}

void em_core_mask_clr(int core, em_core_mask_t *mask)
{
	odp_cpumask_clr(&mask->odp_cpumask, core);
}

int em_core_mask_isset(int core, const em_core_mask_t *mask)
{
	return odp_cpumask_isset(&mask->odp_cpumask, core);
}

int em_core_mask_iszero(const em_core_mask_t *mask)
{
	odp_cpumask_t zero_mask;

	odp_cpumask_zero(&zero_mask);
	return odp_cpumask_equal(&zero_mask, &mask->odp_cpumask);
}

int em_core_mask_equal(const em_core_mask_t *mask1, const em_core_mask_t *mask2)
{
	return odp_cpumask_equal(&mask1->odp_cpumask, &mask2->odp_cpumask);
}

void em_core_mask_set_count(int count, em_core_mask_t *mask)
{
	int i;

	for (i = 0; i < count; i++)
		odp_cpumask_set(&mask->odp_cpumask, i);
}

void em_core_mask_copy(em_core_mask_t *dst, const em_core_mask_t *src)
{
	odp_cpumask_copy(&dst->odp_cpumask, &src->odp_cpumask);
}

int em_core_mask_count(const em_core_mask_t *mask)
{
	return odp_cpumask_count(&mask->odp_cpumask);
}

void em_core_mask_set_bits(const uint64_t bits[], int len, em_core_mask_t *mask)
{
	const int maxlen = (ODP_CPUMASK_SIZE + 63) / 64;
	const int maxcpu = ODP_CPUMASK_SIZE - 1;
	int cpu;

	len = len > maxlen ? maxlen : len;

	for (int i = 0; i < len; i++) {
		uint64_t mask64 = bits[i];

		for (int j = 0; mask64 && j < 64; j++) {
			cpu = i * 64 + j;
			if (unlikely(cpu > maxcpu))
				return;
			if (mask64 & ((uint64_t)1 << j)) {
				odp_cpumask_set(&mask->odp_cpumask, cpu);
				mask64 &= mask64 - 1; /*clear lowest set bit*/
			}
		}
	}
}

int em_core_mask_get_bits(uint64_t bits[/*out*/], int len,
			  const em_core_mask_t *mask)
{
	int u64s_set; /* return value */
	int maxcpu = ODP_CPUMASK_SIZE - 1;
	int i;
	int j;
	int cpu;

	if (unlikely(len < 1))
		return 0;

	if (maxcpu >= len * 64)
		maxcpu = len * 64 - 1;

	/* zero out the bits[] array*/
	for (i = 0; i < len; i++)
		bits[i] = 0;

	i = -1;
	cpu = odp_cpumask_first(&mask->odp_cpumask);
	while (cpu >= 0 && cpu <= maxcpu) {
		i = cpu / 64;
		j = cpu % 64;
		bits[i] |= (uint64_t)1 << j;
		cpu = odp_cpumask_next(&mask->odp_cpumask, cpu);
	}
	u64s_set = i + 1; /* >= 0 */
	return u64s_set;
}

int em_core_mask_set_str(const char *mask_str, em_core_mask_t *mask)
{
	odp_cpumask_t str_mask;

	odp_cpumask_from_str(&str_mask, mask_str);
	odp_cpumask_or(&mask->odp_cpumask, &mask->odp_cpumask, &str_mask);

	return 0;
}

void em_core_mask_tostr(char *mask_str, int len, const em_core_mask_t *mask)
{
	int32_t ret = odp_cpumask_to_str(&mask->odp_cpumask, mask_str, len);

	if (unlikely(ret <= 0 && len > 0))
		mask_str[0] = '\0';
}

int em_core_mask_idx(int n, const em_core_mask_t *mask)
{
	if (unlikely((unsigned int)(n - 1) >= EM_MAX_CORES))
		return -1;

	int i = 1;
	int cpu = odp_cpumask_first(&mask->odp_cpumask);

	while (cpu >= 0 && i < n) {
		cpu = odp_cpumask_next(&mask->odp_cpumask, cpu);
		i++;
	}

	/* cpu >=0 only if odp_cpumask_first/next successful */
	return cpu;
}

void em_core_mask_and(em_core_mask_t *dst, const em_core_mask_t *src1,
		      const em_core_mask_t *src2)
{
	odp_cpumask_and(&dst->odp_cpumask,
			&src1->odp_cpumask, &src2->odp_cpumask);
}

void em_core_mask_or(em_core_mask_t *dst, const em_core_mask_t *src1,
		     const em_core_mask_t *src2)
{
	odp_cpumask_or(&dst->odp_cpumask,
		       &src1->odp_cpumask, &src2->odp_cpumask);
}

void em_core_mask_xor(em_core_mask_t *dst, const em_core_mask_t *src1,
		      const em_core_mask_t *src2)
{
	odp_cpumask_xor(&dst->odp_cpumask,
			&src1->odp_cpumask, &src2->odp_cpumask);
}
