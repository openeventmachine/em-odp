*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Event Group Assign End -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${THIRD_REGEX} =    SEPARATOR=
...    Assigned\\s*event\\s*group\\s*notification\\s*event\\s*received\\s*after
...    \\s*[0-9]+\\s*data\\s*events\\.

${FIFTH_REGEX} =    SEPARATOR=
...    "Normal"\\s*event\\s*group\\s*notification\\s*event\\s*received\\s*after
...    \\s*2048\\s*data\\s*events\\.

@{REGEX_MATCH} =
...    Start\\s*event\\s*group
...    Start\\s*assigned\\s*event\\s*group\\s*
...    ${THIRD_REGEX}
...    Cycles\\s*curr:[0-9]+,\\s*ave:[0-9]+
...    ${FIFTH_REGEX}
...    Cycles\\s*curr:[0-9]+,\\s*ave:[0-9]+
...    Chained\\s*event\\s*group\\s*done
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Event Group Assign End
    [Documentation]    event_group_assign_end -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
