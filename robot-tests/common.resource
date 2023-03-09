*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Resource file
Library    Collections
Library    Process
Library    String


*** Variables ***
${KILL_TIMEOUT} =    30s

# Match Pool Statistics
@{POOL_STATISTICS_MATCH} =
...    (buf|pkt)(\\s*[0-9]+)+(\\s*[0-3]+:\\[sz=[0-9]+\\s*n=[0-9]+\\(0/[0-9]+\\)\\s*\\$=[0-9]+\\])+

# List of eligible return codes
# The return code seem to depend on the version of the used robot framework.
# Terminating a test with Ctrl-C sometimes returned 0, sometimes SIGINT(-2).
# Timeout should return SIGKILL(-9) but not always. Since most of our programs
# keeps running once started, our robot tests send SIGINT to the programs after
# running them for a while or SIGKILL on timeout. So all of the return code 0,
# -2(SIGINT) and -9(SIGKILL) can be considered as eligible.
@{RC_LIST} =    ${0}    ${-2}    ${-9}

# Regex pattern that must not be matched by all test cases
@{REGEX_NOT_MATCH} =
...    EM ERROR
...    APP ERROR

# Common arguments used for all test cases
@{CM_ARGS} =    -c    ${CORE_MASK}    -${APPLICATION_MODE}


*** Keywords ***
Kill Any Hanging Applications
    [Documentation]    Kill any hanging applications
    Terminate All Processes    kill=true

Check Pool Statistics
    [Documentation]    Check if pool statistics printed just before program exit
    ...    match given regax pattern.

    # output: the output from an EM-ODP program
    [Arguments]    ${output}

    # Match only the pool statistic printed just before program exit
    ${last_pool_statistic} =    Get Regexp Matches    ${output.stdout}
    ...    ^Status before delete:(.|\n)*Done - exit$(?m)

    # Should have only one match
    ${num_match} =    Get Length    ${last_pool_statistic}
    Should Be Equal As Integers    ${num_match}    1

    FOR    ${line}    IN    @{POOL_STATISTICS_MATCH}
        Should Match Regexp    ${last_pool_statistic}[0]    ${line}
    END

Run EM-ODP Test
    [Documentation]    Run em-odp application in the background, send SIGINT
    ...    signal to the application process after sleeping the given sleep_time,
    ...    then wait for the application to be completed or killed and check if
    ...    the return code matches any of the eligible codes in RC_LIST and stdout
    ...    matches given regex_match.

    # sleep_time: sleep time (in seconds) before sending SIGINT to the em-odp application
    # regex_match: regex pattern that must be matched
    [Arguments]    ${sleep_time}    ${regex_match}

    # In order to start application log from a new line
    Log To Console    \n

    # Run application with given arguments
    ${app_handle} =    Process.Start Process  taskset  -c  ${TASKSET_CORES}  ${APPLICATION}
    ...    @{CM_ARGS}
    ...    stderr=STDOUT
    ...    shell=True
    ...    stdout=${TEMPDIR}/stdout.txt

    IF    ${sleep_time}
        # Sleep ${sleep_time} seconds
        Sleep    ${sleep_time}
        Send Signal To Process    SIGINT    ${app_handle}    group=true
    END

    ${output} =    Process.Wait For Process
    ...    handle=${app_handle}
    ...    timeout=${KILL_TIMEOUT}
    ...    on_timeout=kill

    # Log output
    Log    ${output.stdout}    console=yes

    # Verify the return code matches any of the eligible code in RC_LIST
    List Should Contain Value
    ...    ${RC_LIST}
    ...    ${output.rc}
    ...    Application Return Code: ${output.rc}

    # Match regular expression lines
    FOR    ${line}    IN    @{regex_match}
        Should Match Regexp    ${output.stdout}    ${line}
    END

    FOR    ${line}    IN    @{REGEX_NOT_MATCH}
        Should Not Match Regexp    ${output.stdout}    ${line}
    END

    Check Pool Statistics    output=${output}
