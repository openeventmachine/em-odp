*** Comments ***
Copyright (c) 2024-2025, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test cli_top -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH} =
...    System has [0-9]+ timer\\(s\\)
...    Timer "ExampleTimer" info:
...    \\s+-resolution: [0-9]+ ns
...    \\s+-max_tmo: [0-9]+ us
...    \\s+-min_tmo: [0-9]+ us
...    \\s+-num_tmo: [0-9]+
...    \\s+-clk_src: [0-9]+
...    \\s+-tick Hz: [0-9]+ hz
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Cli_top
    [Documentation]    cli_top -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    @{app_args} =    Create List    --    --resolution    100000

    Run EM-ODP Test    args=${app_args}    sleep_time=30    regex_match=${REGEX_MATCH}
