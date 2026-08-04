*** Comments ***
Copyright (c) 2023, Nokia
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Variables ***
# The file to which standard output is written
${OUT_FILE} =    ${TEMPDIR}/em_robot_stdout.txt


*** Settings ***
Documentation    Resource file for benchmarks
Library    Process
Library    OperatingSystem


*** Keywords ***
Kill Any Hanging Applications
    [Documentation]    Kill any hanging applications
    Terminate All Processes    kill=true
    Run Keyword And Ignore Error    Remove File    ${OUT_FILE}
    File Should Not Exist    ${OUT_FILE}

Run Bench
    [Documentation]    Run benchmarks

    [Arguments]    ${args}=@{EMPTY}    ${time_out}=30

    ${output} =    Process.Run Process    ${APPLICATION}    @{args}
    ...    stderr=STDOUT
    ...    timeout=${time_out}
    ...    shell=True
    ...    stdout=${OUT_FILE}

    # Verify the return code is 0
    Should Be Equal    ${output.rc}    ${0}

    # Log output
    Log    ${output.stdout}\n\nApplication Return Code: ${output.rc}    console=yes
