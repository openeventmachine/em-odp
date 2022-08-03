*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Atomic Processing End -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    normal atomic processing:\\s*[0-9]+\\s*cycles/event\\s*events/s:[0-9]+\\.
...    [0-9]+\\s*M\\s*@[0-9]+\\.[0-9]+\\s*MHz\\s*\\(core-[0-9]+\\s*[0-9]+\\)

${SECOND_REGEX} =    SEPARATOR=
...    em_atomic_processing_end\\(\\):\\s*[0-9]+\\s*cycles/event\\s*events/s:[0-9]
...    +\\.[0-9]+\\s*M\\s*@[0-9]+.[0-9]+\\s*MHz\\s*\\(core-[0-9]+\\s*[0-9]+\\)

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    ${SECOND_REGEX}
...    Done\\s*-\\s*exit

@{REGEX_NOT_MATCH} =
...    EM ERROR


*** Test Cases ***
Test Atomic Processing End
    [Documentation]    atomic_processing_end -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}
