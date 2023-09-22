*** Comments ***
Copyright (c) 2023, Nokia
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Run pool benchmarks
Resource    bench_common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Terminate All Processes    kill=true


*** Test Cases ***
Run bench_pool
    [Documentation]    Run bench_pool

    @{args} =    Create List    -w
    Run Bench    args=${args}    time_out=10s
