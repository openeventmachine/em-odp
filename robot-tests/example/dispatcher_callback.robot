*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Dispatcher Callback -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
# Common arguments used for all test cases
${FIRST_REGEX} =    SEPARATOR=
...    Dispatcher\\s*enter\\s*callback\\s*[1-2]+\\s*for\\s*EO:\\s*0x[a-fA-F0-9]+
...    \\s*\\(EO\\s*[A-B]+\\)\\s*Queue:\\s*0x[a-fA-F0-9]+\\s*on\\s*core\\
...    s*[0-9]+\\.\\s*Event\\s*seq:\\s*[0-9]+\\.

${SECOND_REGEX} =    SEPARATOR=
...    Ping\\s*from\\s*EO\\s*[A-B]+!\\s*Queue:\\s*0x[a-fA-f0-9]+\\s*on\\s*core
...    \\s*[0-9]+\\.\\s*Event\\s*seq:\\s*[0-9]+\\.

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    ${SECOND_REGEX}
...    Dispatcher\\s*exit\\s*callback\\s*[1-2]+\\s*for\\s*EO:\\s*0x[a-fA-f0-9]+
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Dispatcher Callback
    [Documentation]    dispatcher_callback -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
