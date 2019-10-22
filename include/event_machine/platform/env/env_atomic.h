/*
 *   Copyright (c) 2013, Nokia Solutions and Networks
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

/*
 * env helper include file - don't include this file directly
 */

#ifndef _ENV_ATOMIC_H_
#define _ENV_ATOMIC_H_

/*
 * Atomic operations - types & functions
 */
struct _env_atomic32 {
	uint32_t a32; /**< Storage for the atomic variable */
} ENV_ALIGNED(sizeof(uint32_t)); /* Enforce alignment! */

struct _env_atomic64 {
	uint64_t a64; /**< Storage for the atomic variable */
} ENV_ALIGNED(sizeof(uint64_t)); /* Enforce alignment! */

typedef struct _env_atomic32 env_atomic32_t;
typedef struct _env_atomic64 env_atomic64_t;

/*
 * 32 bit functions
 */
static inline void
env_atomic32_init(env_atomic32_t *atom)
{
	__atomic_store_n(&atom->a32, 0, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic32_set(env_atomic32_t *atom, uint32_t new_val)
{
	__atomic_store_n(&atom->a32, new_val, __ATOMIC_SEQ_CST);
}

static inline uint32_t
env_atomic32_get(env_atomic32_t *atom)
{
	return __atomic_load_n(&atom->a32, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic32_dec(env_atomic32_t *atom)
{
	(void)__atomic_fetch_sub(&atom->a32, 1, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic32_inc(env_atomic32_t *atom)
{
	(void)__atomic_fetch_add(&atom->a32, 1, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic32_add(env_atomic32_t *atom, uint32_t add_val)
{
	(void)__atomic_fetch_add(&atom->a32, add_val, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic32_sub(env_atomic32_t *atom, uint32_t sub_val)
{
	(void)__atomic_fetch_sub(&atom->a32, sub_val, __ATOMIC_SEQ_CST);
}

static inline uint32_t
env_atomic32_add_return(env_atomic32_t *atom, uint32_t add_val)
{
	return __atomic_add_fetch(&atom->a32, add_val, __ATOMIC_SEQ_CST);
}

static inline uint32_t
env_atomic32_return_add(env_atomic32_t *atom, uint32_t add_val)
{
	return __atomic_fetch_add(&atom->a32, add_val, __ATOMIC_SEQ_CST);
}

static inline uint32_t
env_atomic32_sub_return(env_atomic32_t *atom, uint32_t sub_val)
{
	return __atomic_sub_fetch(&atom->a32, sub_val, __ATOMIC_SEQ_CST);
}

static inline uint32_t
env_atomic32_return_sub(env_atomic32_t *atom, uint32_t sub_val)
{
	return __atomic_fetch_sub(&atom->a32, sub_val, __ATOMIC_SEQ_CST);
}

static inline int
env_atomic32_cmpset(env_atomic32_t *atom, uint32_t expected, uint32_t desired)
{
	return __atomic_compare_exchange_n(&atom->a32, &expected, desired,
					   0 /*strong*/, __ATOMIC_SEQ_CST,
					   __ATOMIC_SEQ_CST);
}

static inline uint32_t
env_atomic32_exchange(env_atomic32_t *atom, uint32_t new_val)
{
	return __atomic_exchange_n(&atom->a32, new_val, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic32_set_bits(env_atomic32_t *atom, uint32_t bit_mask)
{
	uint32_t old, new_val;
	int ret;

	do {
		old = env_atomic32_get(atom);
		new_val = old | bit_mask;
		ret = env_atomic32_cmpset(atom, old, new_val);
	} while (ret == 0);
}

static inline void
env_atomic32_clr_bits(env_atomic32_t *atom, uint32_t bit_mask)
{
	uint32_t old, new_val;
	int ret;

	do {
		old = env_atomic32_get(atom);
		new_val = old & (~bit_mask);
		ret = env_atomic32_cmpset(atom, old, new_val);
	} while (ret == 0);
}

static inline int
env_atomic32_cnt_bits(env_atomic32_t *atom)
{
	return __builtin_popcount(env_atomic32_get(atom));
}

/*
 * 64 bit functions
 */
static inline void
env_atomic64_init(env_atomic64_t *atom)
{
	__atomic_store_n(&atom->a64, 0, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic64_set(env_atomic64_t *atom, uint64_t new_val)
{
	__atomic_store_n(&atom->a64, new_val, __ATOMIC_SEQ_CST);
}

static inline uint64_t
env_atomic64_get(env_atomic64_t *atom)
{
	return __atomic_load_n(&atom->a64, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic64_dec(env_atomic64_t *atom)
{
	(void)__atomic_fetch_sub(&atom->a64, 1, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic64_inc(env_atomic64_t *atom)
{
	(void)__atomic_fetch_add(&atom->a64, 1, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic64_add(env_atomic64_t *atom, uint64_t add_val)
{
	(void)__atomic_fetch_add(&atom->a64, add_val, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic64_sub(env_atomic64_t *atom, uint64_t sub_val)
{
	(void)__atomic_fetch_sub(&atom->a64, sub_val, __ATOMIC_SEQ_CST);
}

static inline uint64_t
env_atomic64_add_return(env_atomic64_t *atom, uint64_t add_val)
{
	return __atomic_add_fetch(&atom->a64, add_val, __ATOMIC_SEQ_CST);
}

static inline uint64_t
env_atomic64_return_add(env_atomic64_t *atom, uint64_t add_val)
{
	return __atomic_fetch_add(&atom->a64, add_val, __ATOMIC_SEQ_CST);
}

static inline uint64_t
env_atomic64_sub_return(env_atomic64_t *atom, uint64_t sub_val)
{
	return __atomic_sub_fetch(&atom->a64, sub_val, __ATOMIC_SEQ_CST);
}

static inline uint64_t
env_atomic64_return_sub(env_atomic64_t *atom, uint64_t sub_val)
{
	return __atomic_fetch_sub(&atom->a64, sub_val, __ATOMIC_SEQ_CST);
}

static inline int
env_atomic64_cmpset(env_atomic64_t *atom, uint64_t expected, uint64_t desired)
{
	return __atomic_compare_exchange_n(&atom->a64, &expected, desired,
					   0 /*strong*/, __ATOMIC_SEQ_CST,
					   __ATOMIC_SEQ_CST);
}

static inline uint64_t
env_atomic64_exchange(env_atomic64_t *atom, uint64_t new_val)
{
	return __atomic_exchange_n(&atom->a64, new_val, __ATOMIC_SEQ_CST);
}

static inline void
env_atomic64_set_bits(env_atomic64_t *atom, uint64_t bit_mask)
{
	uint64_t old, new_val;
	int ret;

	do {
		old = env_atomic64_get(atom);
		new_val = old | bit_mask;
		ret = env_atomic64_cmpset(atom, old, new_val);
	} while (ret == 0);
}

static inline void
env_atomic64_clr_bits(env_atomic64_t *atom, uint64_t bit_mask)
{
	uint64_t old, new_val;
	int ret;

	do {
		old = env_atomic64_get(atom);
		new_val = old & (~bit_mask);
		ret = env_atomic64_cmpset(atom, old, new_val);
	} while (ret == 0);
}

static inline int
env_atomic64_cnt_bits(env_atomic64_t *atom)
{
	return __builtin_popcountll(env_atomic64_get(atom));
}

#endif /* _ENV_ATOMIC_H_ */
