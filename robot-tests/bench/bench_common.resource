*** Comments ***
Copyright (c) 2023, Nokia
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Resource file for benchmarks
Library    Process


*** Keywords ***
Run Bench
    [Documentation]    Run benchmarks

    [Arguments]    ${args}=@{EMPTY}    ${time_out}=30

    ${output} =    Process.Run Process    ${APPLICATION}    @{args}
    ...    stderr=STDOUT
    ...    timeout=${time_out}
    ...    shell=True
    ...    stdout=${TEMPDIR}/stdout.txt

    # Verify the return code is 0
    Should Be Equal    ${output.rc}    ${0}

    # Log output
    Log    ${output.stdout}\n\nApplication Return Code: ${output.rc}    console=yes
