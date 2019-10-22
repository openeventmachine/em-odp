/*
 *   Copyright (c) 2014, Nokia Solutions and Networks
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

#ifndef EM_ATOMIC_H_
#define EM_ATOMIC_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Types and macros to avoid #ifdef EM_64_BIT/EM_32_BIT thoughout the code
 */

#if defined(EM_64_BIT)

typedef env_atomic64_t em_atomic_t;

#define EM_ATOMIC_INIT(...)         env_atomic64_init(__VA_ARGS__)
#define EM_ATOMIC_GET(...)          env_atomic64_get(__VA_ARGS__)
#define EM_ATOMIC_SET(...)          env_atomic64_set(__VA_ARGS__)
#define EM_ATOMIC_ADD(...)          env_atomic64_add(__VA_ARGS__)
#define EM_ATOMIC_ADD_RETURN(...)   env_atomic64_add_return(__VA_ARGS__)
#define EM_ATOMIC_RETURN_ADD(...)   env_atomic64_return_add(__VA_ARGS__)
#define EM_ATOMIC_INC(...)          env_atomic64_inc(__VA_ARGS__)
#define EM_ATOMIC_DEC(...)          env_atomic64_dec(__VA_ARGS__)
#define EM_ATOMIC_SUB(...)          env_atomic64_sub(__VA_ARGS__)
#define EM_ATOMIC_SUB_RETURN(...)   env_atomic64_sub_return(__VA_ARGS__)
#define EM_ATOMIC_RETURN_SUB(...)   env_atomic64_return_sub(__VA_ARGS__)
#define EM_ATOMIC_CMPSET(...)       env_atomic64_cmpset(__VA_ARGS__)
#define EM_ATOMIC_EXCHANGE(...)     env_atomic64_exchange(__VA_ARGS__)

#elif defined(EM_32_BIT)

typedef env_atomic32_t em_atomic_t;

#define EM_ATOMIC_INIT(...)         env_atomic32_init(__VA_ARGS__)
#define EM_ATOMIC_GET(...)          env_atomic32_get(__VA_ARGS__)
#define EM_ATOMIC_SET(...)          env_atomic32_set(__VA_ARGS__)
#define EM_ATOMIC_ADD(...)          env_atomic32_add(__VA_ARGS__)
#define EM_ATOMIC_ADD_RETURN(...)   env_atomic32_add_return(__VA_ARGS__)
#define EM_ATOMIC_RETURN_ADD(...)   env_atomic32_return_add(__VA_ARGS__)
#define EM_ATOMIC_INC(...)          env_atomic32_inc(__VA_ARGS__)
#define EM_ATOMIC_DEC(...)          env_atomic32_dec(__VA_ARGS__)
#define EM_ATOMIC_SUB(...)          env_atomic32_sub(__VA_ARGS__)
#define EM_ATOMIC_SUB_RETURN(...)   env_atomic32_sub_return(__VA_ARGS__)
#define EM_ATOMIC_RETURN_SUB(...)   env_atomic32_return_sub(__VA_ARGS__)
#define EM_ATOMIC_CMPSET(...)       env_atomic32_cmpset(__VA_ARGS__)
#define EM_ATOMIC_EXCHANGE(...)     env_atomic32_exchange(__VA_ARGS__)

#else
#error "Neither EM_64_BIT nor EM_32_BIT set!"
#endif

#ifdef __cplusplus
}
#endif

#endif
