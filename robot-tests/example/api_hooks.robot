*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation     Test api_hooks -c ${CORE_MASK} -${APPLICATION_MODE}
Resource          ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
# Regex pattern that must be matched by all the test cases
@{REGEX_MATCH} =
...    Dispatch enter callback
...    Alloc-hook
...    Free-hook
...    Send-hook
...    Done\\s*-\\s*exit

# Regex pattern that must not be matched by all the test cases
@{REGEX_NOT_MATCH} =
...    EM ERROR
...    failed!


*** Test Cases ***
Test api_hooks -c ${CORE_MASK} -${APPLICATION_MODE}
    [Documentation]    api_hooks -c ${CORE_MASK} -${APPLICATION_MODE}
    [Tags]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
