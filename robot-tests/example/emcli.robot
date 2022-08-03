*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test EM CLI(Command Line Interface) with hello application
Resource    ../common.resource
Library           Telnet
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
@{REGEX_MATCH} =
...    cli\\.enable: true\\(1\\)
...    Starting CLI server on 127\\.0\\.0\\.1:55555
...    CLI server terminated!
...    Done\\s*-\\s*exit

@{TELNET_REGEX} =
...    Commands available:
...    call\\s+odp_cls_print_all
...    em_info_print\\s+[Name: all|cpu_arch|conf|help]
...    em_core_print\\s+[Name: map|help]


*** Test Cases ***
Test Emcli
    [Documentation]    hello -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${core_mask}    ${application_mode}

    # In order to start application log from a new line
    Log To Console    \n

    # Run hello application with given arguments
    ${app_handle} =    Process.Start Process    ${APPLICATION}
    ...    @{CM_ARGS}
    ...    stderr=STDOUT
    ...    shell=True
    ...    stdout=${TEMPDIR}/stdout.txt

    Sleep    6s
    Process Should Be Running    ${app_handle}

    # Open telnet connection
    Open Connection    localhost     port=55555
    Write    help
    # . normally will not match a newline, use (?s) to make . to match a newline
    ${telnet_out} =    Read Until Regexp    (?s)EM-ODP>(.*?)EM-ODP>
    Close All Connections

    Send Signal To Process    SIGINT    ${app_handle}    group=true

    ${output} =    Process.Wait For Process
    ...    handle=${app_handle}
    ...    timeout=${KILL_TIMEOUT}
    ...    on_timeout=kill

    # Log output
    Log To Console    \n
    Log    ${output.stdout}    console=yes
    Log To Console    \nTelnet client output:\n    # To seperate the two logs
    Log    ${telnet_out}    console=yes

    # Verify the return code matches any of the eligible code in RC_LIST
    List Should Contain Value
    ...    ${RC_LIST}
    ...    ${output.rc}
    ...    Application Return Code: ${output.rc}

    # Match telnet client outputs
    FOR    ${line}    IN    @{TELNET_REGEX}
        Should Match Regexp    ${telnet_out}    ${line}
    END

    # Match em-app outputs with REGEX_MATCH
    FOR    ${line}    IN    @{REGEX_MATCH}
        Should Match Regexp    ${output.stdout}    ${line}
    END

    FOR    ${line}    IN    @{REGEX_NOT_MATCH}
        Should Not Match Regexp    ${output.stdout}    ${line}
    END

    FOR    ${line}    IN    @{POOL_STATISTICS_MATCH}
        Should Match Regexp    ${output.stdout}    ${line}
    END
