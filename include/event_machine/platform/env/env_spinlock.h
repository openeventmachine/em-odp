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

/*
 * env helper include file - don't include this file directly,
 * instead #include <environment.h>
 */

#ifndef _ENV_SPINLOCK_H_
#define _ENV_SPINLOCK_H_

typedef odp_spinlock_t env_spinlock_t;

static inline void
env_spinlock_init(env_spinlock_t *const lock)
{
	odp_spinlock_init((odp_spinlock_t *)lock);
}

static inline void
env_spinlock_lock(env_spinlock_t *const lock)
{
	odp_spinlock_lock((odp_spinlock_t *)lock);
}

static inline int
env_spinlock_trylock(env_spinlock_t *const lock)
{
	return odp_spinlock_trylock((odp_spinlock_t *)lock);
}

static inline int
env_spinlock_is_locked(env_spinlock_t *const lock)
{
	return odp_spinlock_is_locked((odp_spinlock_t *)lock);
}

static inline void
env_spinlock_unlock(env_spinlock_t *const lock)
{
	odp_spinlock_unlock((odp_spinlock_t *)lock);
}

#endif /* _ENV_SPINLOCK_H_ */
