*** Comments ***
Copyright (c) 2023, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Schedule Latency -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    :\\stime\\(h\\) cores events\\(M\\)\\srate\\(M/s\\)\\smin\\[ns\\]\\smax\\[ns\\]\\savg\\[ns\\]
...    \\smin ev#\\s*max ev#\\s*max_do\\[ns\\]\\smax_eo\\[ns\\]


*** Test Cases ***
Test Schedule Latency
    [Documentation]    scheduling_latency -c ${CORE_MASK} -${APPLICATION_MODE}

    # Note that --levents should be one less than the number of cores in order
    # to see hi-prio latency (one core free fro hi-prio). Since --levents is
    # fixed at 2 in this test, the number of cores must be at least 3 for this
    # robot test to pass.
    @{app_args} =    Create List    --    --levents    2

    @{regex_match} =    Create List
    ...    Backround work: 2 normal priority events with 2.00us work
    ...    ${FIRST_REGEX}
    ...    : bg events\\(M\\) rate\\(M/s\\)
    ...    Done\\s*-\\s*exit

    Run EM-ODP Test To Complete    args=${app_args}    regex_match=${regex_match}
