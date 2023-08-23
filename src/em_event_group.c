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

#include "em_include.h"

static inline event_group_elem_t *
egrp_poolelem2egrp(objpool_elem_t *const event_group_pool_elem)
{
	return (event_group_elem_t *)((uintptr_t)event_group_pool_elem -
			offsetof(event_group_elem_t, event_group_pool_elem));
}

em_status_t
event_group_init(event_group_tbl_t *const event_group_tbl,
		 event_group_pool_t *const event_group_pool)
{
	event_group_elem_t *egrp_elem;
	const int cores = em_core_count();
	int ret;

	memset(event_group_tbl, 0, sizeof(event_group_tbl_t));
	memset(event_group_pool, 0, sizeof(event_group_pool_t));
	env_atomic32_init(&em_shm->event_group_count);

	for (int i = 0; i < EM_MAX_EVENT_GROUPS; i++) {
		em_event_group_t egrp = egrp_idx2hdl(i);

		egrp_elem = event_group_elem_get(egrp);
		if (unlikely(!egrp_elem))
			return EM_ERR_BAD_POINTER;

		egrp_elem->event_group = egrp; /* store handle */
		egrp_elem->all = 0; /* set num_notif = 0, ready = 0 */
		env_atomic64_set(&egrp_elem->post.atomic, 0);
		env_atomic64_set(&egrp_elem->pre.atomic, 0);
	}

	ret = objpool_init(&event_group_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (int i = 0; i < EM_MAX_EVENT_GROUPS; i++) {
		egrp_elem = &event_group_tbl->egrp_elem[i];
		objpool_add(&event_group_pool->objpool, i % cores,
			    &egrp_elem->event_group_pool_elem);
	}

	return EM_OK;
}

em_event_group_t
event_group_alloc(void)
{
	const event_group_elem_t *egrp_elem;
	objpool_elem_t *egrp_pool_elem;

	egrp_pool_elem = objpool_rem(&em_shm->event_group_pool.objpool,
				     em_core_id());
	if (unlikely(egrp_pool_elem == NULL))
		return EM_EVENT_GROUP_UNDEF;

	egrp_elem = egrp_poolelem2egrp(egrp_pool_elem);

	env_atomic32_inc(&em_shm->event_group_count);
	return egrp_elem->event_group;
}

em_status_t
event_group_free(em_event_group_t event_group)
{
	event_group_elem_t *egrp_elem = event_group_elem_get(event_group);

	if (unlikely(egrp_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(&em_shm->event_group_pool.objpool,
		    egrp_elem->event_group_pool_elem.subpool_idx,
		    &egrp_elem->event_group_pool_elem);

	env_atomic32_dec(&em_shm->event_group_count);
	return EM_OK;
}

unsigned int
event_group_count(void)
{
	return env_atomic32_get(&em_shm->event_group_count);
}

#define EGRP_INFO_HDR_FMT \
"Number of event groups: %d\n\n" \
"ID        Ready  Cnt(post)  Gen  Num-notif\n" \
"------------------------------------------\n%s\n"

#define EGRP_INFO_LEN 43
#define EGRP_INFO_FMT "%-10" PRI_EGRP "%-7c%-11d%-5d%-9d\n" /*43 bytes*/

void event_group_info_print(void)
{
	unsigned int egrp_num;
	em_event_group_t egrp;
	const event_group_elem_t *egrp_elem;
	egrp_counter_t egrp_count;
	int len = 0;
	int n_print = 0;

	egrp = em_event_group_get_first(&egrp_num);

	/*
	 * egrp_num may not match the amount of event groups actually returned
	 * by iterating with em_event_group_get_next() if event groups are added
	 * or removed in parallel by another core. Thus space for 10 extra event
	 * groups is reserved. If more than 10 event groups are added by other
	 * cores in paralle, we print only information of the (egrp_num + 10)
	 * event groups.
	 *
	 * The extra 1 byte is reserved for the terminating null byte.
	 */
	const int egrp_info_str_len = (egrp_num + 10) * EGRP_INFO_LEN + 1;
	char egrp_info_str[egrp_info_str_len];

	while (egrp != EM_EVENT_GROUP_UNDEF) {
		egrp_elem = event_group_elem_get(egrp);

		if (unlikely(egrp_elem == NULL || !event_group_allocated(egrp_elem))) {
			egrp = em_event_group_get_next();
			continue;
		}

		egrp_count.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);

		n_print = snprintf(egrp_info_str + len,
				   egrp_info_str_len - len,
				   EGRP_INFO_FMT, egrp,
				   egrp_elem->ready ? 'Y' : 'N',
				   egrp_count.count, egrp_count.gen,
				   egrp_elem->num_notif);

		/* Not enough space to hold more event group info */
		if (n_print >= egrp_info_str_len - len)
			break;

		len += n_print;
		egrp = em_event_group_get_next();
	}

	/* No event group */
	if (len == 0) {
		EM_PRINT("No event group!\n");
		return;
	}

	/*
	 * To prevent printing incomplete information of the last event group
	 * when there is not enough space to hold all event group info.
	 */
	egrp_info_str[len] = '\0';
	EM_PRINT(EGRP_INFO_HDR_FMT, egrp_num, egrp_info_str);
}
