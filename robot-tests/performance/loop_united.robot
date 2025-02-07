*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Loop United -c ${CORE_MASK} -${APPLICATION_MODE} -- -l ${LOOP_TYPE}
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
    [Documentation]    loop_united -c ${CORE_MASK} -${APPLICATION_MODE} -- -l ${LOOP_TYPE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}    ${LOOP_TYPE}

    @{app_args} =    Create List
    ...    --    -l    ${LOOP_TYPE}

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}    args=${app_args}
