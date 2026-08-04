*** Comments ***
Copyright (c) 2020-2025, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test event_types -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    EM-core[0-9]+, EO [A|B]: Event from Q:0x[0-9|A-F|a-f]+

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    Done\\s*-\\s*exit


*** Test Cases ***
Test event_types
    [Documentation]    event_types -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
