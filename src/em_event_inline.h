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
  * EM internal event functions
  *
  */

#ifndef EM_EVENT_INLINE_H_
#define EM_EVENT_INLINE_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Convert an EM-event into an ODP-event */
static inline odp_event_t
event_em2odp(em_event_t event)
{
	/* Valid for both ESV enabled and disabled */
	evhdl_t evhdl = {.event = event};

	return (odp_event_t)(uintptr_t)evhdl.evptr;
}

/**
 * Convert an ODP-event into an EM-event
 *
 * @note The returned EM-event does NOT contain the ESV event-generation-count
 *       evhdl_t::evgen! This must be set separately when using ESV.
 */
static inline em_event_t
event_odp2em(odp_event_t odp_event)
{
	/* Valid for both ESV enabled and disabled */

	/*
	 * Setting 'evhdl.event = odp_event' is equal to
	 *         'evhdl.evptr = odp_event, evhdl.evgen = 0'
	 * (evhdl.evgen still needs to be set when using ESV)
	 */
	evhdl_t evhdl = {.event = (em_event_t)(uintptr_t)odp_event};

	return evhdl.event;
}

/** Convert an array of EM-events into an array ODP-events */
static inline void
events_em2odp(const em_event_t events[/*in*/],
	      odp_event_t odp_events[/*out*/], const unsigned int num)
{
	/* Valid for both ESV enabled and disabled */
	const evhdl_t *const evhdls = (const evhdl_t *)events;

	for (unsigned int i = 0; i < num; i++)
		odp_events[i] = (odp_event_t)(uintptr_t)evhdls[i].evptr;
}

/**
 * Convert an array of ODP-events into an array of EM-events
 *
 * @note The output EM-events do NOT contain the ESV event-generation-count
 *       evhdl_t::evgen! This must be set separately when using ESV.
 */
static inline void
events_odp2em(const odp_event_t odp_events[/*in*/],
	      em_event_t events[/*out*/], const unsigned int num)
{
	/* Valid for both ESV enabled and disabled */
	evhdl_t *const evhdls = (evhdl_t *)events;

	/*
	 * Setting 'evhdls[i].event = odp_events[i]' is equal to
	 *         'evhdls[i].evptr = odp_events[i], evhdl[i].evgen = 0'
	 * (evhdls[i].evgen still needs to be set when using ESV)
	 */
	for (unsigned int i = 0; i < num; i++)
		evhdls[i].event = (em_event_t)(uintptr_t)odp_events[i];
}

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_INLINE_H_ */
