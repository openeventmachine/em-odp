*** Comments ***
Copyright (c) 2023, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test pool_perf -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Test Cases ***
Test Pool Perf
    [Documentation]    pool_perf -c ${CORE_MASK} -${APPLICATION_MODE}

    @{app_args} =    Create List
    ...    --    --allocs    -8    --frees    -8    --window    16
    ...    --pool    10000,512,32,PACKET    --ignore    5    --no-first

    Run EM-ODP Test To Complete    args=${app_args}    time_out=50
