/*
 *   Copyright (c) 2017, Nokia Solutions and Networks
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
#include "em_include.h"
#include <event_machine_timer.h>

#include "em_timer.h"

/* thread-local pointer to shared state */
ENV_LOCAL timer_shm_t *timer_shm;

em_status_t timer_init_global(void)
{
	odp_shm_info_t info;
	int i;

	/*
	 * The EM-timer uses its own shared memory region.
	 * As timeouts live in a buffer pool now this starts to be unnecessary?
	 * Move to common EM shm or just use one buffer?
	 */
	odp_shm_t shm = odp_shm_reserve(EM_ODP_TIMER_SHM_NAME,
					sizeof(timer_shm_t), sizeof(uint64_t),
					ODP_SHM_SW_ONLY | ODP_SHM_SINGLE_VA);
	if (shm == ODP_SHM_INVALID) {
		EM_LOG(EM_LOG_ERR, "odp_shm_reserve() failed\n");
		return EM_ERR_LIB_FAILED;
	}

	timer_shm_t *const tconf = odp_shm_addr(shm);

	if (tconf == NULL)
		return EM_ERR_LIB_FAILED;

	/* init shared memory data */
	memset(tconf, 0, sizeof(timer_shm_t));
	tconf->odp_shm = shm;

	for (i = 0; i < EM_ODP_MAX_TIMERS; i++)
		tconf->timer[i].idx = i;

	odp_ticketlock_init(&tconf->tlock);

	if (odp_shm_info(shm, &info) == 0) {
		EM_LOG(EM_LOG_PRINT,
		       "Event timer created %ldB ShMem on %ldk pages\n",
		       info.size, info.page_size / 1024);
	} else {
		EM_LOG(EM_LOG_ERR, "%s: can't read SHM info\n", __func__);
	}

	return EM_OK;
}

em_status_t timer_init_local(void)
{
	/* init thread local data ptr */
	odp_shm_t shm = odp_shm_lookup(EM_ODP_TIMER_SHM_NAME);

	if (shm == ODP_SHM_INVALID) {
		timer_shm = NULL;
		return EM_OK;	/* assume timer was disabled */
	}

	timer_shm = (timer_shm_t *)odp_shm_addr(shm);

	return timer_shm != NULL ? EM_OK : EM_ERR;
}

em_status_t timer_term_local(void)
{
	return EM_OK;
}

em_status_t timer_term_global(void)
{
	if (timer_shm == NULL)
		return EM_OK;	/* assume timer was not enabled */

	return odp_shm_free(timer_shm->odp_shm) == 0 ?
				EM_OK : EM_ERR_LIB_FAILED;
}
