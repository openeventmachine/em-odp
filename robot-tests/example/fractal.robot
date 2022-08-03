*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Fractal -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH} =
...    Started\\s*Pixel\\s*handler\\s*-\\s*EO:0x[a-fA-F0-9]+\\.\\s*Q:0x[a-fA-F0-9]+\\.
...    Started\\s*Worker\\s*-\\s*EO:0x[a-fA-f0-9]+\\.\\s*Q:0x[a-fA-F0-9]+\\.
...    Started\\s*Imager\\s*-\\s*EO:0x[a-fA-f0-9]+\\.\\s*Q:0x[a-fA-f0-9]+\\.
...    Frames\\s*per\\s*second:\\s*[0-9]+\\s*|\\s*frames\\s*[0-9]+\\s*-\\s*[0-9]+


*** Test Cases ***
Test Fractal
    [Documentation]    fractal -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=30    regex_match=${REGEX_MATCH}
