/*
 *   Copyright (c) 2024, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_PACKET_H_
#define EVENT_MACHINE_PACKET_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_packet Packet events
 *  Operations on packet events.
 * @{
 * ### Packet events
 * Event (major) Type: EM_EVENT_TYPE_PACKET
 *
 * EM APIs for basic manipulation of packet events (i.e. events allocated from
 * EM pools created with event type EM_EVENT_TYPE_PACKET).
 * More advanced packet manipulation should be done via ODP APIs by first
 * converting the EM event to an ODP packet
 * (see include/event_machine/platform/event_machine_odp_ext.h).
 *
 * Packet events contain a certain amount of headroom before the start of the
 * actual packet data. The headroom allows new data, e.g. additional protocol
 * headers, to be inserted before the start of the existing packet data. The
 * amount of headroom available for newly allocated packets can be configured
 * globally via the EM config file or, specifically for a pool via the
 * em_pool_cfg_t struct passed to em_pool_create().
 * Similarly, the packet may contain a certain amount of tailroom that can be
 * used to extend the payload with additional data. The tailroom is located
 * after the end of the packet payload until the end of the packet buffer.
 * Pushing out the head/tail via APIs is required to include head- or tailroom
 * into the packet data, see below.
 *
 * The start of the packet data area can be modified by "pushing out" the head
 * into the headroom or "pulling in" the head from the headroom. Similarly, the
 * end of the packet data area can be modified by "pushing out" or "pulling in"
 * the tail.
 * Pushing out the head of the packet increases the packet data area from the
 * beginning. This allows for e.g. insertion of new packet headers (prepend).
 * Pulling in the head of the packet allows for e.g. dropping of packet headers
 * from the beginning of the packet data.
 * Pushing out the tail of the packet increases the packet data area at the end
 * of the packet. This allows for extending the packet with new data (append).
 * Pulling in the tail can be used to trim the end of unwanted data or set the
 * packet size exactly so no extra data is present (avoids extra data over
 * packet I/O).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Get the packet event data pointer
 *
 * Returns a pointer to the first byte of the packet data.
 *
 * The data pointer can be adjusted with em_packet_push/pull_head().
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * @param pktev  Packet event handle
 *
 * @return Pointer to the beginning of the packet data area
 * @retval NULL on error
 *
 * @see em_event_pointer()
 */
void *em_packet_pointer(em_event_t pktev);

/**
 * @brief Get the packet size
 *
 * Returns the number of linearly accessible data bytes that follows after the
 * current packet data pointer position.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * @param pktev  Packet event handle
 *
 * @return Packet data size in bytes following em_packet_pointer()
 * @retval 0 on error
 */
uint32_t em_packet_size(em_event_t pktev);

/**
 * @brief Get the packet event data pointer as well as the packet size.
 *
 * Returns a pointer to the first byte of the packet data and optionally
 * outputs the packet payload size.
 *
 * This API is a combination of em_packet_pointer() and em_packet_size() since
 * both are often needed at the same time.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * @param      pktev  Packet event handle
 * @param[out] size   Optional output arg into which the packet event
 *                    payload size (in bytes) is stored. Use 'size=NULL' if no
 *                    size information is needed. Only set by the function when
 *                    no errors occurred.
 *
 * @return Pointer to the beginning of the packet data area
 * @retval NULL on error ('size' not touched)
 *
 * @see em_packet_pointer(), em_packet_size()
 * @see em_event_pointer(), em_event_get_size(), em_event_pointer_and_size()
 */
void *em_packet_pointer_and_size(em_event_t pktev, uint32_t *size /*out*/);

/**
 * @brief Resize a packet event
 *
 * Set a new size for a packet, by adjusting the end of the packet via the
 * packet tailroom.
 * Only the end (tail) of the packet is modified to change the size,
 * the beginning (head) is untouched.
 *
 * The packet can be enlarged with max 'em_packet_tailroom()' bytes or
 * reduced to min one (1) byte:
 *       1 <= 'size' <= (em_packet_size() + em_packet_tailroom())
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * The event is not modified on error.
 *
 * @param pktev  Packet event handle
 * @param size   New size of the packet event (1 ... current-size + tailroom)
 *
 * @return Pointer to the beginning of the packet data
 * @retval NULL on unsupported event type, invalid size or other error
 *
 * @see em_packet_push_tail(), em_packet_pull_tail()
 */
void *em_packet_resize(em_event_t pktev, uint32_t size);

/**
 * @brief Packet event headroom length
 *
 * Returns the current length of the packet headroom.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * @param pktev  Packet event handle
 *
 * @return Headroom length
 * @retval 0 if no headroom, but also on error (e.g. if the event isn't a packet)
 */
uint32_t em_packet_headroom(em_event_t pktev);

/**
 * @brief Packet event tailroom length
 *
 * Returns the current length of the packet tailroom.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * @param pktev  Packet event handle
 *
 * @return Tailroom length
 * @retval 0 if no tailroom, but also on error (e.g. if the event isn't a packet)
 */
uint32_t em_packet_tailroom(em_event_t pktev);

/**
 * @brief Push out the beginning of the packet into the headroom
 *
 * Increase the packet data length by moving the beginning (head) of the packet
 * into the packet headroom. The packet headroom is decreased by the same amount.
 * The packet head may be pushed out by up to 'headroom' bytes.
 * The packet is not modified if there's not enough space in the headroom.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * The event is not modified on error.
 *
 * @param pktev  Packet event handle
 * @param len    The number of bytes to push out the head (0 ... headroom)
 *
 * @return Pointer to the new beginning of the increased packet data area
 * @retval NULL on unsupported event type, invalid length or other error
 */
void *em_packet_push_head(em_event_t pktev, uint32_t len);

/**
 * @brief Pull in the beginning of the packet from the headroom.
 *
 * Decrease the packet data length by removing data from the beginning (head) of
 * the packet. The packet headroom is increased by the same amount.
 * The packet head may be pulled in until only 1 byte of the packet data
 * remains.
 * The packet is not modified if there's not enough data.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * The event is not modified on error.
 *
 * @param pktev  Packet event handle
 * @param len    The number of bytes to pull in the head with
 *               (0 ... packet data len - 1)
 *
 * @return Pointer to the new beginning of the decreased packet data area
 * @retval NULL on unsupported event type, invalid length or other error
 */
void *em_packet_pull_head(em_event_t pktev, uint32_t len);

/**
 * @brief Push out the end of the packet into the tailroom
 *
 * Increase the packet data length by moving the end (tail) of the packet into
 * the packet tailroom. The packet tailroom is decreased by the same amount.
 * The packet tail may be pushed out by up to 'tailroom' bytes.
 * The packet is not modified if there's not enough space in the tailroom.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * The event is not modified on error.
 *
 * @param pktev  Packet event handle
 * @param len    The number of bytes to push out the tail (0 ... tailroom)
 *
 * @return Pointer to the start of the newly added data at the end of the packet
 * @retval NULL on unsupported event type or other error
 */
void *em_packet_push_tail(em_event_t pktev, uint32_t len);

/**
 * Pull in the end of the packet from the tailroom.
 *
 * Decrease the packet data length by removing data from the end (tail) of the
 * packet. The packet tailroom is increased by the same amount.
 * The packet tail may be pulled in until only 1 byte of the packet data
 * remains.
 * The packet is not modified if there's not enough data.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * The event is not modified on error.
 *
 * @param pktev  Packet event handle
 * @param len    The number of bytes to pull in the tail with
 *               (0 ... packet data len - 1)
 *
 * @return Pointer to the end of the packet
 * @retval NULL on unsupported event type, invalid length or other error
 */
void *em_packet_pull_tail(em_event_t pktev, uint32_t len);

/**
 * Reset a packet event
 *
 * Resets packet event metadata and adjusts the start of the packet data back to
 * pool (and subpool) defaults. The packet data size is set to 'size' and must
 * fit the used subpool sizing.
 *
 * The event type, set by em_alloc...() (or em_event_set_type()) is untouched by
 * the reset-operation.
 *
 * The event user area metadata is reset, i.e. the user area id is unset but the
 * actual user area content is untouched.
 *
 * Logically, this reset operation is similar to freeing the event and then
 * allocating the same event again with a given 'size' and the same type (but
 * without any actual free & alloc via the event pool being done).
 *
 * This function must not be called for events with references.
 *
 * Only events allocated from EM pools created with event type
 * EM_EVENT_TYPE_PACKET can be used with this API.
 *
 * @param pktev  Packet event handle
 * @param size   Packet data size
 *
 * @return EM_OK when successful or an EM error code on error.
 */
em_status_t em_packet_reset(em_event_t pktev, uint32_t size);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_PACKET_H_ */
