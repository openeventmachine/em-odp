*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Queues Types AG -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    Stat\\s*Core-[0-9]+:\\s*Count/PairType\\s*A-A:\\s*[0-9]+\\s*P-P:\\s*[0-9]+
...    \\s*PO-PO:\\s*[0-9]+\\s*P-A:\\s*[0-9]+\\s*PO-A:\\s*[0-9]+\\s*PO-P:\\s*[0-9]
...    +\\s*AG-AG:\\s*[0-9]+\\s*AG-A:\\s*[0-9]+\\s*AG-P:\\s*[0-9]+\\s*AG-PO:\\s*
...    [0-9]+\\s*cycles/event:[0-9]+\\s*@[0-9]+MHz\\s*[0-9]+

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Queue Types AG
    [Documentation]    queues_types_ag -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}
