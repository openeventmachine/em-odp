*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Timer Hello -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH} =
...    EO *
...    System has [0-9]+ timer
...    EO local start
...    [0-9]+\. tick
...    tock
...    Meditation time.
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Timer Hello
    [Documentation]    timer_hello -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=120    regex_match=${REGEX_MATCH}
