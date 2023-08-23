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
 * @brief Create EM's internal unscheduled control queues at startup.
 */
em_status_t create_ctrl_queues(void);

/**
 * @brief Delete EM's internal unscheduled control queues at teardown.
 */
em_status_t delete_ctrl_queues(void);

/**
 * @brief Poll EM's internal unscheduled control queues during dispatch.
 */
void poll_unsched_ctrl_queue(void);

/**
 * @brief Sends an internal control event to each core set in 'mask'.
 *
 * When all cores set in 'mask' has processed the event an additional
 * internal 'done' msg is sent to synchronize - this done-event can then
 * trigger any notifications for the user that the operation was completed.
 * Processing of the 'done' event will also call the 'f_done_callback'
 * function if given.
 *
 * @return 0 on success, otherwise return the number of ctrl events that could
 *         not be sent due to error
 */
int send_core_ctrl_events(const em_core_mask_t *const mask, em_event_t ctrl_event,
			  void (*f_done_callback)(void *arg_ptr),
			  void *f_done_arg_ptr,
			  int num_notif, const em_notif_t notif_tbl[],
			  bool sync_operation);

/**
 * @brief Helper func: Allocate & set up the internal 'done' event with
 * function callbacks and notification events. Creates the needed event group
 * and applies the event group count. A successful setup returns the event group
 * ready for use with em_send_group().
 *
 * @return An event group successfully 'applied' with count and notifications.
 * @retval EM_EVENT_GROUP_UNDEF on error
 *
 * @see evgrp_abort_delete() below for deleting the event group returned by this
 *      function.
 */
em_event_group_t internal_done_w_notif_req(int event_group_count,
					   void (*f_done_callback)(void *arg_ptr),
					   void  *f_done_arg_ptr,
					   int num_notif, const em_notif_t notif_tbl[],
					   bool sync_operation);

/**
 * @brief Helper func to send notifications events
 */
em_status_t send_notifs(const int num_notif, const em_notif_t notif_tbl[]);

/**
 * @brief Check that the usage of a notification is valid
 */
em_status_t check_notif(const em_notif_t *const notif);

/**
 * @brief Check that the usage of a table of notifications is valid
 */
em_status_t check_notif_tbl(const int num_notif, const em_notif_t notif_tbl[]);

/**
 * @brief internal_done_w_notif_req() 'companion' to abort and delete the
 *        event group created by the mentioned function.
 */
void evgrp_abort_delete(em_event_group_t event_group);

#ifdef __cplusplus
}
#endif

#endif /* EM_INTERNAL_EVENT_H_ */
