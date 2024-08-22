*** Comments ***
Copyright (c) 2024, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Loop Vectors -c ${CORE_MASK} -${APPLICATION_MODE}
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
Test Loop Vectors
    [Documentation]    loop_vectors -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}
