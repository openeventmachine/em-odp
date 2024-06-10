*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Timer -c ${CORE_MASK} -${APPLICATION_MODE}
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH} =
...    EO *
...    Timer\: Creating [0-9]+ timeouts took [0-9]+ ns \\([0-9]+ ns each\\)
...    Linux\: Creating [0-9]+ timeouts took [0-9]+ ns \\([0-9]+ ns each\\)
...    Running
...    Heartbeat count [0-9]+
...    ONESHOT\:
...    Received: [0-9]+
...    Cancelled\: [0-9]+
...    Cancel failed \\(too late\\)\: [0-9]+
...    SUMMARY/TICKS: min [0-9]+, max [0-9]+, avg [0-9]+
...    /[A-Z]S: min [0-9]+, max [0-9]+, avg [0-9]+
...    SUMMARY/LINUX [A-Z]S: min -?[0-9]+, max -?[0-9]+, avg -?[0-9]+
...    PERIODIC\:
...    Received\: [0-9]+
...    Cancelled\: [0-9]+
...    Cancel failed \\(too late\\)\: [0-9]+
...    Errors\: [0-9]+
...    TOTAL RUNTIME/[A-Z]S\: min [0-9]+, max [0-9]+
...    Cleaning up
...    Timer\: Deleting [0-9]+ timeouts took [0-9]+ ns \\([0-9]+ ns each\\)
...    Linux\: Deleting [0-9]+ timeouts took [0-9]+ ns \\([0-9]+ ns each\\)
...    Done\\s*-\\s*exit


*** Test Cases ***
Test Timer
    [Documentation]    timer -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    Run EM-ODP Test    sleep_time=90    regex_match=${REGEX_MATCH}
