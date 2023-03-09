/*
 *   Copyright (c) 2019, Nokia Solutions and Networks
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
 * Event Machine API callback hooks.
 *
 */

#include "em_include.h"

em_status_t
em_hooks_register_alloc(em_api_hook_alloc_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_API_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_REGISTER_ALLOC,
			"EM API callback hooks disabled");

	hook_fn.alloc = func;
	stat = hook_register(ALLOC_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_REGISTER_ALLOC,
			"Alloc hook register failed");

	return EM_OK;
}

em_status_t
em_hooks_unregister_alloc(em_api_hook_alloc_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_API_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_UNREGISTER_ALLOC,
			"EM API callback hooks disabled");

	hook_fn.alloc = func;
	stat = hook_unregister(ALLOC_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_UNREGISTER_ALLOC,
			"Alloc hook unregister failed");

	return EM_OK;
}

em_status_t
em_hooks_register_free(em_api_hook_free_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_API_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_REGISTER_FREE,
			"EM API callback hooks disabled");

	hook_fn.free = func;
	stat = hook_register(FREE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_REGISTER_FREE,
			"Free hook register failed");

	return EM_OK;
}

em_status_t
em_hooks_unregister_free(em_api_hook_free_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_API_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_UNREGISTER_FREE,
			"EM API callback hooks disabled");

	hook_fn.free = func;
	stat = hook_unregister(FREE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_UNREGISTER_FREE,
			"Free hook unregister failed");

	return EM_OK;
}

em_status_t
em_hooks_register_send(em_api_hook_send_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_API_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_REGISTER_SEND,
			"EM API callback hooks disabled");

	hook_fn.send = func;
	stat = hook_register(SEND_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_REGISTER_SEND,
			"Send hook register failed");

	return EM_OK;
}

em_status_t
em_hooks_unregister_send(em_api_hook_send_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_API_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_UNREGISTER_SEND,
			"EM API callback hooks disabled");

	hook_fn.send = func;
	stat = hook_unregister(SEND_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_UNREGISTER_SEND,
			"Send hook unregister failed");

	return EM_OK;
}

em_status_t
em_hooks_register_to_idle(em_idle_hook_to_idle_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_IDLE_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_REGISTER_TO_IDLE,
			"EM IDLE callback hooks disabled");

	hook_fn.to_idle = func;
	stat = hook_register(TO_IDLE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_REGISTER_TO_IDLE,
			"To_idle hook register failed");

	return EM_OK;
}

em_status_t
em_hooks_unregister_to_idle(em_idle_hook_to_idle_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_IDLE_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_UNREGISTER_TO_IDLE,
			"EM IDLE callback hooks disabled");

	hook_fn.to_idle = func;
	stat = hook_unregister(TO_IDLE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_UNREGISTER_TO_IDLE,
			"To_idle hook unregister failed");

	return EM_OK;
}

em_status_t
em_hooks_register_to_active(em_idle_hook_to_active_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_IDLE_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_REGISTER_TO_ACTIVE,
			"EM IDLE callback hooks disabled");

	hook_fn.to_active = func;
	stat = hook_register(TO_ACTIVE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_REGISTER_TO_ACTIVE,
			"To_active hook register failed");

	return EM_OK;
}

em_status_t
em_hooks_unregister_to_active(em_idle_hook_to_active_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_IDLE_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_UNREGISTER_TO_ACTIVE,
			"EM IDLE callback hooks disabled");

	hook_fn.to_active = func;
	stat = hook_unregister(TO_ACTIVE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_UNREGISTER_TO_ACTIVE,
			"To_active hook unregister failed");

	return EM_OK;
}

em_status_t
em_hooks_register_while_idle(em_idle_hook_while_idle_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_IDLE_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_REGISTER_WHILE_IDLE,
			"EM IDLE callback hooks disabled");

	hook_fn.while_idle = func;
	stat = hook_register(WHILE_IDLE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_REGISTER_WHILE_IDLE,
			"While_idle hook register failed");

	return EM_OK;
}

em_status_t
em_hooks_unregister_while_idle(em_idle_hook_while_idle_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_IDLE_HOOKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_HOOKS_UNREGISTER_WHILE_IDLE,
			"EM IDLE callback hooks disabled");

	hook_fn.while_idle = func;
	stat = hook_unregister(WHILE_IDLE_HOOK, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_HOOKS_UNREGISTER_WHILE_IDLE,
			"While_idle hook unregister failed");

	return EM_OK;
}
