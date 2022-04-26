/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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
 * EM internal-event types & definitions
 *
 */

#ifndef EM_INTERNAL_EVENT_TYPES_H_
#define EM_INTERNAL_EVENT_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal event IDs
 */
#define EVENT_ID_MASK                   (0xabcd0000)

#define EM_INTERNAL_DONE                (EVENT_ID_MASK | 0x00) /* First */

#define EO_START_LOCAL_REQ              (EVENT_ID_MASK | 0x10)
#define EO_START_SYNC_LOCAL_REQ         (EVENT_ID_MASK | 0x11)
#define EO_STOP_LOCAL_REQ               (EVENT_ID_MASK | 0x12)
#define EO_STOP_SYNC_LOCAL_REQ          (EVENT_ID_MASK | 0x13)

#define EO_REM_QUEUE_LOCAL_REQ          (EVENT_ID_MASK | 0x20)
#define EO_REM_QUEUE_SYNC_LOCAL_REQ     (EVENT_ID_MASK | 0x21)
#define EO_REM_QUEUE_ALL_LOCAL_REQ      (EVENT_ID_MASK | 0x22)
#define EO_REM_QUEUE_ALL_SYNC_LOCAL_REQ (EVENT_ID_MASK | 0x23)

#define QUEUE_GROUP_ADD_REQ             (EVENT_ID_MASK | 0x30)
#define QUEUE_GROUP_REM_REQ             (EVENT_ID_MASK | 0x31)

/**
 * Store local function return values into a common struct for later inspection
 */
typedef struct {
	/** EO elem related to the local functions */
	eo_elem_t *eo_elem;
	/** Queue elem related to the local functions */
	queue_elem_t *q_elem;
	/**
	 * Request to also delete the queues when removing them,
	 * em_eo_remove_queue_all(_sync)(...) only
	 */
	int delete_queues;
	/** store allocated event handle inside struct for em_free() */
	em_event_t event;
	/**
	 * For error situation handling, use count to determine a safe time
	 * to free this struct/event without multicore race conditions
	 */
	env_atomic32_t free_at_zero;
	/** Slot for each core to store the actual local func retval */
	em_status_t core[EM_MAX_CORES];
} loc_func_retval_t;

/**
 * Internal event
 */
typedef union internal_event_t {
	/** All internal events start with 'id' */
	uint64_t id;

	/** 'done' event */
	struct {
		uint64_t id;
		em_event_group_t event_group;
		void (*f_done_callback)(void *arg_ptr);
		void  *f_done_arg_ptr;
		int num_notif;
		em_notif_t notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];
	} done;

	/** 'queue group' operation event */
	struct {
		uint64_t id;
		em_queue_group_t queue_group;
	} q_grp;

	/** core local function call request event */
	struct {
		uint64_t id;
		em_event_group_t event_group;
		eo_elem_t *eo_elem;
		queue_elem_t *q_elem;
		int delete_queues; /* for em_eo_remove_queue_all(_sync)(...) */
		loc_func_retval_t *retvals; /* ptr to separate allocation */
	} loc_func;

} internal_event_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_INTERNAL_EVENT_TYPES_H_ */
