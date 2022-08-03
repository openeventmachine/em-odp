*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Queues -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${THIRD_REGEX} =    SEPARATOR=
...    [0-9]+\\s*[0-9]+\\.[0-9]+\\s*M\\s*[0-9]+\\s*[0-9]+\\s*[0-9]+\\s*[0-9]+
...    \\s*[0-9]+\\s*MHz\\s*[0-9]+

@{REGEX_MATCH} =
...    Number\\s*of\\s*queues:\\s*[0-9]+
...    Number\\s*of\\s*events:\\s*[0-9]+
...    ${THIRD_REGEX}
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Queues
    [Documentation]    queues -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=200    regex_match=${REGEX_MATCH}
