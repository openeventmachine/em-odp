/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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

#ifndef CM_POOL_CONFIG_H
#define CM_POOL_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Note: DEFAULT_POOL_ID = EM_POOL_DEFAULT,
 * configuration given in em_conf_t to em_init().
 * The name of the default EM event pool is EM_POOL_DEFAULT_NAME (="default")
 */
#define DEFAULT_POOL_CFG { \
	.event_type = EM_EVENT_TYPE_SW, \
	.num_subpools = 4, \
	.subpool[0] = {.size =  256, .num = 16384}, \
	.subpool[1] = {.size =  512, .num = 1024}, \
	.subpool[2] = {.size = 1024, .num = 1024}, \
	.subpool[3] = {.size = 2048, .num = 1024}  \
}

#define APPL_POOL_1  ((em_pool_t)2)
#define APPL_POOL_1_NAME "appl_pool_1"
#define APPL_POOL_1_CFG { \
	.event_type = EM_EVENT_TYPE_SW, \
	.num_subpools = 4, \
	.subpool[0] = {.size =  256, .num = 16384}, \
	.subpool[1] = {.size =  512, .num = 1024}, \
	.subpool[2] = {.size = 1024, .num = 1024}, \
	.subpool[3] = {.size = 2048, .num = 1024}  \
}

/*
 * #define APPL_POOL_2  ((em_pool_t)3)
 * #define APPL_POOL_2_NAME "appl_pool_2"
 * #define APPL_POOL_2_CFG { \
 *	.event_type = EM_EVENT_TYPE_PACKET, \
 *	.num_subpools = 4, \
 *	.subpool[0] = {.size =  256, .num = 1024}, \
 *	.subpool[1] = {.size =  512, .num = 1024}, \
 *	.subpool[2] = {.size = 1024, .num = 1024}, \
 *	.subpool[3] = {.size = 2048, .num = 1024}  \
 * }
 */

#ifdef __cplusplus
}
#endif

#endif /* CM_POOL_CONFIG_H */
