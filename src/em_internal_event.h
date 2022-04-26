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
  * EM internal-event functions
  *
  */

#ifndef EM_INTERNAL_EVENT_H_
#define EM_INTERNAL_EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Receive function for handling internal ctrl events, called by the daemon EO
 */
void
internal_event_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		       em_queue_t queue, void *q_ctx);
int
send_core_ctrl_events(const em_core_mask_t *const mask, em_event_t ctrl_event,
		      void (*f_done_callback)(void *arg_ptr),
		      void *f_done_arg_ptr,
		      int num_notif, const em_notif_t notif_tbl[],
		      bool sync_operation);

em_event_group_t
internal_done_w_notif_req(int event_group_count,
			  void (*f_done_callback)(void *arg_ptr),
			  void  *f_done_arg_ptr,
			  int num_notif, const em_notif_t notif_tbl[],
			  bool sync_operation);
void evgrp_abort_delete(em_event_group_t event_group);

em_status_t
send_notifs(const int num_notif, const em_notif_t notif_tbl[]);

em_status_t
check_notif(const em_notif_t *const notif);
em_status_t
check_notif_tbl(const int num_notif, const em_notif_t notif_tbl[]);

void poll_unsched_ctrl_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_INTERNAL_EVENT_H_ */
