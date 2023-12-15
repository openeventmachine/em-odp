*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation     Test timer_test_periodic -c ${CORE_MASK} -${APPLICATION_MODE}
Resource          ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Test Cases ***
Test timer_test_periodic -c ${CORE_MASK} -${APPLICATION_MODE}
    [Documentation]    timer_test_periodic -c ${CORE_MASK} -${APPLICATION_MODE}
    [Tags]    ${CORE_MASK}    ${APPLICATION_MODE}

    @{app_args} =    Create List    --    --resolution    1m    --period    10m
    ...    --tracebuf    500    --num-timers    2    --num-tmo    5    --api-prof
    ...    --num-runs    2    --recreate

    @{regex_match} =    Create List
    ...    Num late ack:      0 \\(0 %\\)
    ...    Max early arrival: 0.0 us
    ...    Done\\s*-\\s*exit

    @{regex_not_match} =    Create List
    ...    ERROR
    ...    EM ERROR
    ...    Max diff from tgt: [0-9]+.[0-9]+ us \\(res [0-9]+.[0-9]+ us\\) >2x res!

    Run EM-ODP Test To Complete    args=${app_args}    time_out=60
    ...    regex_match=${regex_match}
    ...    regex_not_match=${regex_not_match}
