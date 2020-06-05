/* Copyright (c) 2020 Nokia Solutions and Networks
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-3-Clause
 */

/**
 * @file
 * EM internal daemon eo types & definitions
 *
 */

#ifndef EM_DAEMON_EO_TYPES_H_
#define EM_DAEMON_EO_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Daemon EO
 */
typedef struct {
	em_eo_t eo;
} daemon_eo_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_DAEMON_EO_TYPES_H_ */
