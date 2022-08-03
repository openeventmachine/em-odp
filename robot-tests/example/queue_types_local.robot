*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Queue Types Local -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${FIRST_REGEX} =    SEPARATOR=
...    EO\\s*0x[a-fA-F0-9]+\\s*starting\\s*EO-locq\\s*0x[0-9]+\\s*starting\\s*New
...    \\s*atomic\\s*group:group_[a-zA-Z]+\\s*for\\s*EO:\\s*0x[a-fA-F0-9]+

${SECOND_REGEX} =    SEPARATOR=
...    EO-locq\\s*0x[a-fA-F0-9]+\\s*starting\\s*New\\s*atomic\\s*group:group_[a-zA-Z]
...    +\\s*for\\s*EO:\\s*0x[a-fA-F0-9]+

${THIRD_REGEX} =    SEPARATOR=
...    EO\\s*0x[a-fA-F0-9]+\\s*starting\\s*EO-locq\\s*0x[a-fA-F0-9]+\\s*starting
...    \\s*EO\\s*0x[a-fA-F0-9]+\\s*starting

${FOURTH_REGEX} =    SEPARATOR=
...    Core-[0-9]+:\\s*A-L-A-L:\\s*[0-9]+\\s*P-L-P-L:\\s*[0-9]+\\s*PO-L-PO-L:\\s*
...    [0-9]+\\s*P-L-A-L:\\s*[0-9]+\\s*PO-L-A-L:\\s*[0-9]+\\s*PO-L-P-L:\\s*[0-9]+
...    \\s*AG-L-AG-L:\\s*[0-9]+\\s*AG-L-A-L:\\s*[0-9]+\\s*AG-L-P-L:\\s*[0-9]+\\s*
...    AG-L-PO-L:\\s*[0-9]+\\s*cycles/event:[0-9]+\\s*@[0-9]+MHz\\s*[0-9]+

@{REGEX_MATCH} =
...    ${FIRST_REGEX}
...    ${SECOND_REGEX}
...    ${THIRD_REGEX}
...    ${FOURTH_REGEX}
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Queue Types Local
    [Documentation]    queue_types_local -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=60    regex_match=${REGEX_MATCH}
