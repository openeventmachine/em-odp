*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Queue Group -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    Created\\s*test\\s*queue:0x[a-fA-F0-9]+\\s*type:[A-Z]+\\([0-9]+\\)\\s*
...    queue\\s*group:0x[a-fA-F0-9]+\\s*\\(name:"[a-zA-Z0-9]+"\\)

${SEVENTH_REGEX} =    SEPARATOR=
...    Deleting\\s*test\\s*queue:0x[a-fA-F0-9]+,\\s*Qgrp\\s*ID:0x[a-fA-F0-9]+
...    \\s*\\(name:"[a-zA-Z0-9]+"\\)

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    Received\\s*[0-9]+\\s*events\\s*on\\s*Q:0x[a-fA-F0-9]+:
...    QueueGroup:0x[a-fA-F0-9]+,\\s*Curr\\s*Coremask:0x[a-fA-F0-9]+
...    Now\\s*Modifying:
...    QueueGroup:0x[a-fA-F0-9]+,\\s*New\\s*Coremask:0x[a-fA-F0-9]+
...    All\\s*cores\\s*removed\\s*from\\s*QueueGroup!
...    ${SEVENTH_REGEX}
...    !!!\\s*Restarting\\s*test\\s*!!!
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Queue Group
    [Documentation]    queue_group -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=50    regex_match=${REGEX_MATCH}
