*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation     Test loop_multircv -c ${CORE_MASK} -${APPLICATION_MODE}
Resource          ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    cycles/event:\\s*[0-9]+\\.[0-9]+\\s*Mevents/s/core:\\s*[0-9]+\\.[0-9]+
...    \\s*[0-9]+\\s*MHz\\s*core[0-9]+\\s*[0-9]+

# Regex pattern that must be matched by all the test cases
@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    Done\\s*-\\s*exit


*** Test Cases ***
Test loop_multircv -c ${CORE_MASK} -${APPLICATION_MODE}
    [Documentation]    loop_multircv -c ${CORE_MASK} -${APPLICATION_MODE}
    [Tags]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=40    regex_match=${REGEX_MATCH}
