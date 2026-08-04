*** Comments ***
Copyright (c) 2020-2026, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Loop United -c ${CORE_MASK} -${APPLICATION_MODE} -- --loop=${LOOP_TYPE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    cycles/event:\\s*[0-9]+\\.[0-9]+\\s*Mevents/s/core:\\s*[0-9]+\\.[0-9]+
...    \\s*[0-9]+\\s*MHz\\s*core[0-9]+\\s*[0-9]+

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Loop United Loop
    [Documentation]    loop_united -c ${CORE_MASK} -${APPLICATION_MODE} -- --loop=${LOOP_TYPE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}    ${LOOP_TYPE}    loop_type

    @{app_args} =    Create List
    ...    --    --loop=${LOOP_TYPE}

    IF  ${PRINT_COUNT}
        Append To List   ${app_args}   --count-to-print=${PRINT_COUNT}
    END

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}    args=${app_args}

Test Loop United Atomic Groups Pairs
    [Documentation]    loop_united -c ${CORE_MASK} -${APPLICATION_MODE}
    ...                -- --loop=p --ag-queues=4 --eo-count=32 --event-amount=32
    ...
    ...                Run loop_united in pairs mode with atomic groups
    ...                enabled. Scale chosen to match the default pairs
    ...                run (128 queues, 4096 events):
    ...                32 EOs (16 pairs) * 4 queues per AG = 128 queues,
    ...                32 events per queue = 4096 events, exercising
    ...                4 queues per atomic group across 16 EO pairs.
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}    pairs    atomic_group

    @{app_args} =    Create List
    ...    --    --loop=p    --ag-queues=4    --eo-count=32    --event-amount=32

    IF  ${PRINT_COUNT}
        Append To List   ${app_args}   --count-to-print=${PRINT_COUNT}
    END

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}    args=${app_args}
