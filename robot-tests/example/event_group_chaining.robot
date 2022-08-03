*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Event Group Chaining -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH} =
...    Start\\s*event\\s*group
...    Event\\s*group\\s*notification\\s*event\\s*received\\s*after\\s*[0-9]+\\s*data\\s*events\\.
...    Cycles\\s*curr:[0-9]+,\\s*ave:[0-9]+
...    Event\\s*group\\s*notification\\s*event\\s*received\\s*after\\s*[0-9]+\\s*data\\s*events\\.
...    Chained\\s*event\\s*group\\s*done


*** Test Cases ***
Test Event Group Chaining
    [Documentation]    event_group_chaining -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
