*** Comments ***
Copyright (c) 2023, Nokia
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Run event benchmarks
Resource    bench_common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Terminate All Processes    kill=true


*** Test Cases ***
Run bench_event
    [Documentation]    Run bench_event

    @{args} =    Create List    -w
    Run Bench    args=${args}    time_out=5m30s
