*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Error -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup    Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FOUTH_REGEX} =    SEPARATOR=
...    Appl\\s*EO\\s*specific\\s*error\\s*handler:\\s*EO\\s*0x[a-fA-Z0-9]+\\s*error
...    \\s*0x[a-fA-Z0-9]+\\s*escope\\s*0x[a-fA-Z0-9]+

@{REGEX_MATCH} =
...    EM\\s*ERROR:0x[a-fA-Z0-9]+\\s*ESCOPE:0x[a-fA-Z0-9]+\\s*EO:0x[a-fA-Z0-9]+-"EO\\s*[A-fA-F]+"
...    core:[0-9]+\\s*ecount:[0-9]+\\([0-9]+\\)\\s*event_machine_event.c:[0-9]+\\s*em_free\\(\\)
...    Error\\s*log\\s*from\\s*EO\\s*[a-fA-Z]+\\s*\\[[0-9]+\\]\\s*on\\s*core\\s*[0-9]+!
...    ${FOUTH_REGEX}
...    Done\\s*-\\s*exit

@{REGEX_NOT_MATCH} =    NO ERROR


*** Test Cases ***
Test Error
    [Documentation]    error -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=25    regex_match=${REGEX_MATCH}
