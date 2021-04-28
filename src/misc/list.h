/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2013-2015, Nokia Solutions and Networks
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

#ifndef MISC_LIST_H_
#define MISC_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Double linked list node
 */
typedef struct list_node_t {
	struct list_node_t *next;
	struct list_node_t *prev;
} list_node_t;

/**
 * Initialize the list
 */
static inline void
list_init(list_node_t *const head)
{
	head->next = head;
	head->prev = head;
}

/**
 * Check whether the list is empty
 */
static inline int
list_is_empty(const list_node_t *const head)
{
	return (head->next == head);
}

/**
 * Double linked list add node (add last in list)
 */
static inline void
list_add(list_node_t *const head, list_node_t *const node)
{
	list_node_t *const next = head;
	list_node_t *const prev = head->prev;

	node->next = next;
	node->prev = prev;

	prev->next = node;
	next->prev = node;
}

/**
 * Double linked list remove node
 */
static inline void
list_rem(const list_node_t *const head, list_node_t *const node)
{
	list_node_t *const next = node->next;
	list_node_t *const prev = node->prev;

	(void)head;  /* unused */

	prev->next = next;
	next->prev = prev;

	/* just for safety */
	node->next = node;
	node->prev = node;
}

/**
 * Double linked list remove first node (FIFO-mode)
 */
static inline list_node_t *
list_rem_first(const list_node_t *const head)
{
	list_node_t *node = NULL;

	if (!list_is_empty(head)) {
		/* first node in the list */
		node = head->next;
		list_rem(head, node);
	}

	return node;
}

/**
 * Double linked list remove last node (LIFO-mode)
 */
static inline list_node_t *
list_rem_last(const list_node_t *const head)
{
	list_node_t *node = NULL;

	if (!list_is_empty(head)) {
		/* last node in the list */
		node = head->prev;
		list_rem(head, node);
	}

	return node;
}

/**
 * Macro for accessing each node in a queue list
 *
 * @param head  Points to the head node of the list
 * @param pos   Internal pointer for tracking position, don't use in loop!
 * @param cur   Points to the current node, access this inside loop
 */
#define list_for_each(head, pos, cur) \
	for ((pos) = (head)->next; \
	     (cur) = (void *)(pos), (pos) = (pos)->next, \
	     (cur) != (head);)

#ifdef __cplusplus
}
#endif

#endif
