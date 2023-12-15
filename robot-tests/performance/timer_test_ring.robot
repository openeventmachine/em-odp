*** Comments ***
Copyright (c) 2023, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test timer_test_ring -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Test Cases ***
Test Timer Ring
    [Documentation]    timer_test_ring -c ${CORE_MASK} -${APPLICATION_MODE}

    @{app_args} =    Create List    --    --api-profile    --basehz    100
    ...    --multiplier    1,4,5    --looptime    10    --num-tmo    3

    # Make sure that in Analysis for loop, the "error %" is greater than -5%
    @{regex_not_match} =    Create List
    ...    ERROR
    ...    EM ERROR
    ...    (?:\\d+\\s+){2}\\d+.\\d{4}\\s+\\d+\\s+(?:\\d+.\\d{4}\\s+){2}-?[5-9].\\d{3}

    Run EM-ODP Test To Complete    args=${app_args}    time_out=50
    ...    regex_not_match=${regex_not_match}
