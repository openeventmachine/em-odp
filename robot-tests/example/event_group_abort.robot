*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Event Group Abort -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH}    Entering the event dispatch loop
...    Round [0-9]+
...    Created\\s*[0-9]+\\s*event\\s*group\\(s\\)\\s*with\\s*count\\s*of\\s*[0-9]+
...    Abort\\s*group\\s*when\\s*received\\s*[0-9]+\\s*events
...    Evgrp\\s*events:\\s*Valid:[0-9]+\\s*Expired:[0-9]+
...    Evgrp\\s*increments:\\s*Valid:[0-9]+\\s*Failed:[0-9]+
...    Evgrp\\s*assigns:\\s*Valid:[0-9]+\\s*Failed:[0-9]+
...    Aborted\\s*[0-9]+\\s*event\\s*groups
...    Failed\\s*to\\s*abort\\s*[0-9]+\\s*times
...    Received\\s*[0-9]+\\s*notification\\s*events
...    Freed\\s*[0-9]+\\s*notification\\s*events


*** Test Cases ***
Test Event Group Abort
    [Documentation]    event_group_abort -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
