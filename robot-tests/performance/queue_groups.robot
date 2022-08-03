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
...    Cycles/Event:\\s*[0-9]+\\s*Events/s:\\s*[0-9]+\\.[0-9]+\\s*M\\s*Latency:
...    \\s*Hi-prio=[0-9]+\\s*Lo-prio=[0-9]+\\s*@[0-9]+\\s*MHz\\([0-9]+\\)

@{REGEX_MATCH} =
...    ${FIRST_REGEX}


*** Test Cases ***
Test Queue Groups
    [Documentation]    queue_groups -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=40    regex_match=${REGEX_MATCH}
