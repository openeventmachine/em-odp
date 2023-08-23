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


*** Test Cases ***
Test Emcli
    [Documentation]    hello -c ${CORE_MASK} -${APPLICATION_MODE}
    [TAGS]    ${core_mask}    ${application_mode}

    # Run hello application with given arguments
    ${app_handle} =    Process.Start Process  taskset  -c  ${TASKSET_CORES}  ${APPLICATION}
    ...    @{CM_ARGS}
    ...    stderr=STDOUT
    ...    shell=True
    ...    stdout=${TEMPDIR}/stdout.txt

    Sleep    10s
    Process Should Be Running    ${app_handle}

    # Open telnet connection
    Open Connection    localhost     port=55555

    # Test different EM-CLI commands
    Show Available Commands

    # Set the prompt used in this connection to "EM-ODP>"
    Set Prompt    EM-ODP>    true

    Test Atomic Group Print
    Test EO Print
    Test Event Group Print
    Test EM Info Print
    Test Pool Print
    Test Pool Stats
    Test Queue Print
    Test Queue Group Print
    Test Core Print

    Close All Connections

    Send Signal To Process    SIGINT    ${app_handle}    group=true

    ${output} =    Process.Wait For Process
    ...    handle=${app_handle}
    ...    timeout=${KILL_TIMEOUT}
    ...    on_timeout=kill

    # Log output
    Log    \n${output.stdout}    console=yes

    # Verify the return code matches any of the eligible code in RC_LIST
    List Should Contain Value
    ...    ${RC_LIST}
    ...    ${output.rc}
    ...    Application Return Code: ${output.rc}

    # Match em-app outputs with REGEX_MATCH
    FOR    ${line}    IN    @{REGEX_MATCH}
        Should Match Regexp    ${output.stdout}    ${line}
    END

    FOR    ${line}    IN    @{REGEX_NOT_MATCH}
        Should Not Match Regexp    ${output.stdout}    ${line}
    END

    Check Pool Statistics    output=${output}


*** Keywords ***
Show Available Commands
    [Documentation]    Write help command over the connection and check if it
    ...    list all suppoorted EM-CLI commands.

    ${pool_stats_help} =    Catenate    SEPARATOR=
    ...    ^\\s{2}em_pool_stats\\s+\\[i <pool id>\\|n <pool name>\\|s <pool id:
    ...    \\[subpool ids\\]>\\|h\\]\\s?$(?m)

    # (?m) switch on multiline mode, making ^ and $ match also at the start and end
    # of a new line, otherwise, ^ and $ match only at the start and end of whole text.
    @{telnet_regex} =    Create List
    ...    Commands available:
    ...    ^\\s{2}em_agrp_print\\s+\\[a\\|i <ag id>\\|n <ag name>\\|h\\]\\s?$(?m)
    ...    ^\\s{2}em_eo_print\\s+\\[a\\|i <eo id>\\|n <eo name>\\|h\\]\\s?$(?m)
    ...    ^\\s{2}em_egrp_print\\s+$(?m)
    ...    ^\\s{2}em_info_print\\s+\\[a\\|p\\|c\\|h\\]\\s?$(?m)
    ...    ^\\s{2}em_pool_print\\s+\\[a\\|i <pool id>\\|n <pool name>\\|h\\]\\s?$(?m)
    ...    ${pool_stats_help}
    ...    ^\\s{2}em_queue_print\\s+\\[a\\|c\\|h\\]\\s?$(?m)
    ...    ^\\s{2}em_qgrp_print\\s+\\[a\\|i <qgrp id>\\|n <qgrp name>\\|h\\]\\s?$(?m)
    ...    ^\\s{2}em_core_print\\s+$(?m)

    # Show available commands
    Write    help
    # . normally will not match a newline, use (?s) to make . to match a newline
    ${telnet_out} =    Read Until Regexp    (?s)EM-ODP>(.*?)EM-ODP>

    # Match telnet client outputs
    FOR    ${line}    IN    @{telnet_regex}
        Should Match Regexp    ${telnet_out}    ${line}
    END

Test Atomic Group Print
    [Documentation]    Test em_agrp_print command and check if it outputs text
    ...    as expected.

    # em_agrp_print should display "No atomic group has been created" for hello
    ${agrp} =    Execute Command    em_agrp_print
    Should Match Regexp    ${agrp}    ^No atomic group has been created\\s+$(?m)

    # em_agrp_print -h
    @{agrp_help_regex} =    Create List
    ...    Usage: em_agrp_print \\[OPTION\\]
    ...    Print info about atomic groups
    ...    Options:
    ...    ^\\s{2}-a, --all\\s+Print info about all atomic groups\\s?$(?m)
    ...    ^\\s{2}-i, --id <ag id>\\s+Print info about all queues of <ag id>\\s?$(?m)
    ...    ^\\s{2}-n, --name <ag name>\\s+Print info about all queues of <ag name>\\s?$(?m)
    ...    ^\\s{2}-h, --help\\s+Display this help\\s?$(?m)

    ${agrp_help} =    Execute Command    em_agrp_print -h
    FOR    ${line}    IN    @{agrp_help_regex}
        Should Match Regexp    ${agrp_help}    ${line}
    END

Test EO Print
    [Documentation]    Test em_eo_print command and check if it outputs EO info
    ...    as expected.

    ${eo_info_fmt} =    Catenate    SEPARATOR=
    ...    ^ID\\s+Name\\s+State\\s+Start-local\\s+Stop-local\\s+Multi-rcv\\s+Max-events
    ...    \\s+Err-hdl\\s+Q-num\\s+EO-ctx\\s?$(?m)

    # em_eo_print
    # 122 is from EO_INFO_LEN - 1 in em_eo.c
    @{eo_regex} =    Create List
    ...    Number of EOs: 2
    ...    ${eo_info_fmt}
    ...    -{122}
    ...    ^0x[A-Fa-f0-9]+\\s+EO [AB]\\s+RUNNING\\s+N\\s+N\\s+N\\s+1\\s+N\\s+1\\s+Y(?m)

    ${eo_print} =    Execute Command    em_eo_print
    FOR    ${line}    IN    @{eo_regex}
        Should Match Regexp    ${eo_print}    ${line}
    END

    # Extracts EO IDs which are not fixed across different runs (can't be hard coded),
    # first ID corresponds to EO A and second EO B. The extracted IDs
    # are needed while fetching EO queue info with EO ID.
    ${eo_ids} =    Get Regexp Matches    ${eo_print}    0x[A-Fa-f0-9]+

    # 84 is from EO_Q_INFO_LEN - 1 in em_eo.c
    @{eo_a_regex} =    Create List
    ...    EO ${eo_ids[0]}\\(EO A\\) has 1 queue\\(s\\):
    ...    ^Handle\\s+Name\\s+Priority\\s+Type\\s+State\\s+Qgrp\\s+Ctx\\s?$(?m)
    ...    -{84}
    ...    ^0x[A-Fa-f0-9]+\\s+queue A\\s+4\\s+ATOMIC\\s+READY\\s+0x[A-Fa-f0-9]+\\s+Y(?m)

    # Fetch information about queues belonging to EO A using its EO ID
    ${eo_a} =    Execute Command    em_eo_print -i ${eo_ids[0]}
    FOR    ${line}    IN    @{eo_a_regex}
        Should Match Regexp    ${eo_a}    ${line}
    END

    @{eo_b_regex} =    Create List
    ...    EO ${eo_ids[1]}\\(EO B\\) has 1 queue\\(s\\):
    ...    ^Handle\\s+Name\\s+Priority\\s+Type\\s+State\\s+Qgrp\\s+Ctx\\s?$(?m)
    ...    -{84}
    ...    ^0x[A-Fa-f0-9]+\\s+queue B\\s+4\\s+ATOMIC\\s+READY\\s+0x[A-Fa-f0-9]+\\s+Y(?m)

    ${eo_b} =    Execute Command    em_eo_print -n "EO B"
    FOR    ${line}    IN    @{eo_b_regex}
        Should Match Regexp    ${eo_b}    ${line}
    END

Test Event Group Print
    [Documentation]    Test em_egrp_print command and check if it outputs event
    ...    group info as expected.

    # em_egrp_print
    ${egrp_print} =    Execute Command    em_egrp_print
    Should Match Regexp    ${egrp_print}    ^No event group!\\s?$(?m)

    # em_egrp_print -h
    ${egrp_extra} =    Execute Command    em_egrp_print -h
    Should Match Regexp    ${egrp_extra}    ^Error: extra parameter given to command!\\s?$(?m)

Test EM Info Print
    [Documentation]    Test em_info_print command and check if it outputs EM info
    ...    as expected.

    # em_info_print
    @{em_info_regex} =    Create List
    ...    ^EM Info on target: em-odp\\s?$(?m)
    ...    ^EM API version:\\s+\\d+\\.\\d+\\s?$(?m)
    ...    ^Core mapping: EM-core <-> phys-core <-> ODP-thread\\s?$(?m)
    ...    ^ODP Queue Capabilities\\s?$(?m)
    ...    ^EM Queues\\s?$(?m)
    ...    ^EM Queue group\\(s\\): 1\\s?$(?m)
    ...    ^EM Event Pools: 2\\s?$(?m)
    ...    ^EM Events\\s?$(?m)

    ${em_info} =    Execute Command    em_info_print
    FOR    ${line}    IN    @{em_info_regex}
        Should Match Regexp    ${em_info}    ${line}
    END

    # Test invalid option -x
    ${info_invalid} =    Execute Command    em_info_print -x
    Should Match Regexp    ${info_invalid}    ^Error: invalid option -- 'x'\\s?$(?m)

Test Pool Print
    [Documentation]    Test em_pool_print command and check if it outputs em pool
    ...    info as expected.

    @{cm_regex} =    Create List
    ...    EM Event Pool
    ...    -{13}
    ...    ^\\s{2}id\\s+name\\s+type offset uarea sizes\\s+\\[size count\\(used/free\\) cache\\](?m)

    # em_pool_print --id 0x1
    ${id_regex} =    Catenate    SEPARATOR=
    ...    ^\\s{2}0x1\\s+default\\s+buf\\s+00\\s+00\\s+04\\s+
    ...    0:\\[sz=256 n=16384\\(\\d+\\/\\d+\\) \\$=64\\]\\s+
    ...    1:\\[sz=512 n=1024\\(\\d+\\/\\d+\\) \\$=32\\]\\s+
    ...    2:\\[sz=1024 n=1024\\(\\d+\\/\\d+\\) \\$=16\\]\\s+
    ...    3:\\[sz=2048 n=1024\\(\\d+\\/\\d+\\) \\$=8\\](?m)

    @{pool_id_regex} =    Create List
    ...    @{cm_regex}
    ...    ${id_regex}

    ${pool_id} =    Execute Command    em_pool_print --id 0x1
    FOR    ${line}    IN    @{pool_id_regex}
        Should Match Regexp    ${pool_id}    ${line}
    END

    ${id_lack_arg} =    Execute Command    em_pool_print --id
    Should Match Regexp    ${id_lack_arg}    ^Error: option requires an argument -- 'id'(?m)

    # em_pool_print --name appl_pool_1
    ${name_regex} =    Catenate    SEPARATOR=
    ...    ^\\s{2}0x[A-Fa-f0-9]+\\s+appl_pool_1\\s+pkt\\s+00\\s+00\\s+04\\s+
    ...    0:\\[sz=256 n=16384\\(\\d+\\/\\d+\\) \\$=128\\]\\s+
    ...    1:\\[sz=512 n=1024\\(\\d+\\/\\d+\\) \\$=64\\]\\s+
    ...    2:\\[sz=1024 n=1024\\(\\d+\\/\\d+\\) \\$=32\\]\\s+
    ...    3:\\[sz=2048 n=1024\\(\\d+\\/\\d+\\) \\$=16\\](?m)

    @{pool_name_regex} =    Create List
    ...    @{cm_regex}
    ...    ${name_regex}

    ${pool_name} =    Execute Command    em_pool_print --name appl_pool_1
    FOR    ${line}    IN    @{pool_name_regex}
        Should Match Regexp    ${pool_name}    ${line}
    END

Test Pool Stats
    [Documentation]    Test em_pool_stats command and check if it outputs pool
    ...    statistics as expected.

    ${stats_fmt} =    Catenate    SEPARATOR=
    ...    Subpool Available Alloc_ops Alloc_fails Free_ops Total_ops
    ...    \\sCache_available Cache_alloc_ops Cache_free_ops

    @{pool_stats_regex} =    Create List
    ...    ${stats_fmt}
    ...    ^([0-9]+\\s+){8}[0-9]+\\s?(?m)

    ${pool_stats} =    Execute Command    em_pool_stats -i 0x1
    Should Match Regexp    ${pool_stats}    EM pool statistics for pool 0x[A-Fa-f0-9]+:
    FOR    ${line}    IN    @{pool_stats_regex}
        Should Match Regexp    ${pool_stats}    ${line}
    END

    ${subpool_stats} =    Execute Command    em_pool_stats --subpools 0x1:[1,2]
    Should Match Regexp    ${subpool_stats}    EM subpool statistics for pool 0x[A-Fa-f0-9]+:
    FOR    ${line}    IN    @{pool_stats_regex}
        Should Match Regexp    ${subpool_stats}    ${line}
    END

Test Queue Print
    [Documentation]    Test em_queue_print command and check if it outputs em
    ...    queue info as expected.

    ${queue_fmt} =    Catenate    SEPARATOR=
    ...    ^Handle\\s+Name\\s+Priority\\s+Type\\s+State\\s+Qgrp\\s+Agrp\\s+EO\\s+
    ...    Multi-rcv\\s+Max-events\\s+Ctx(?m)

    ${unsched_queue} =    Catenate    SEPARATOR=
    ...    ^0x[A-Fa-f0-9]+\\s+EMctrl-unschedQ-core0\\s+255\\s+UNSCH\\s+UNSCH\\s+
    ...    \\(nil\\)\\s+\\(nil\\)\\s+\\(nil\\)\\s+N\\s+0\\s+N(?m)

    ${unsched_shared} =    Catenate    SEPARATOR=
    ...    ^0x[A-Fa-f0-9]+\\s+EMctrl-unschedQ-shared\\s+255\\s+UNSCH\\s+UNSCH\\s+
    ...    \\(nil\\)\\s+\\(nil\\)\\s+\\(nil\\)\\s+N\\s+0\\s+N(?m)

    ${queue_ab} =    Catenate    SEPARATOR=
    ...    ^0x[A-Fa-f0-9]+\\s+queue [AB]\\s+4\\s+ATOMIC\\s+READY\\s+0x[A-Fa-f0-9]+
    ...    \\s+\\(nil\\)\\s+0x[A-Fa-f0-9]+\\s+N\\s+1\\s+Y(?m)

    # 127 is calculated as (QUEUE_INFO_LEN - 1) where QUEUE_INFO_LEN is defined
    # in em_queue.c
    @{queue_list_regex} =    Create List
    ...    Number of queues: \\d+
    ...    ${queue_fmt}
    ...    -{127}
    ...    ${unsched_queue}
    ...    ${unsched_shared}
    ...    ${queue_ab}

    ${queue_list} =    Execute Command    em_queue_print
    FOR    ${line}    IN    @{queue_list_regex}
        Should Match Regexp    ${queue_list}    ${line}
    END

    # em_queue_print -c
    @{queue_capa_regex} =    Create List
    ...    ODP Queue Capabilities
    ...    ^\\s{2}Max number of ODP queues: \\d+(?m)
    ...    EM Queues
    ...    ^\\s{2}Max number of EM queues: \\d+ \\(0x[A-Fa-f0-9]+\\)(?m)
    ...    ^\\s{2}EM queue handle offset: \\d+ \\(0x[A-Fa-f0-9]+\\)(?m)
    ...    ^\\s{2}EM queue range:(?m)
    ...    ^\\s{4}static range:(?m)
    ...    ^\\s{4}internal range:(?m)
    ...    ^\\s{4}dynamic range:(?m)

    ${queue_capa} =    Execute Command    em_queue_print -c
    FOR    ${line}    IN    @{queue_capa_regex}
        Should Match Regexp    ${queue_capa}    ${line}
    END

Test Queue Group Print
    [Documentation]    Test em_qgrp_print command and check if it outputs em
    ...    queue group info as expected.

    # 108 is calculated as (QGRP_INFO_LEN - 1) where QGRP_INFO_LEN is defined in
    # em_queue_group.c
    @{qgrp_regex} =    Create List
    ...    EM Queue group\\(s\\): 1
    ...    ^ID\\s+Name\\s+EM-mask\\s+Cpumask\\s+ODP-mask\\s+Q-num(?m)
    ...    -{108}
    ...    ^0x[A-Fa-f0-9]+\\s+default\\s+0x[A-Fa-f0-9]+\\s+0x[A-Fa-f0-9]+\\s+0x[A-Fa-f0-9]+\\s+2(?m)

    ${qgrp_all} =    Execute Command    em_qgrp_print --all
    ${qgrp} =    Execute Command    em_qgrp_print

    # Output of command em_qgrp_print should equal that of command em_qgrp_print --all
    Should Be Equal As Strings    ${qgrp_all}    ${qgrp}
    FOR    ${line}    IN    @{qgrp_regex}
        Should Match Regexp    ${qgrp}    ${line}
    END

    # 74 is calculated as (QGRP_Q_LEN - 1) where QGRP_Q_LEN is defined in em_queue_group.c
    @{qgrp_default_regex} =    Create List
    ...    Queue group 0x[A-Fa-f0-9]+\\(default\\) has 2 queue\\(s\\):
    ...    ^Id\\s+Name\\s+Priority\\s+Type\\s+State\\s+Ctx(?m)
    ...    -{74}
    ...    ^0x[A-Fa-f0-9]+\\s+queue [AB]\\s+4\\s+ATOMIC\\s+READY\\s+Y(?m)

    ${qgrp_default} =    Execute Command    em_qgrp_print -n default
    FOR    ${line}    IN    @{qgrp_default_regex}
        Should Match Regexp    ${qgrp_default}    ${line}
    END

    # Should print error when fetching info about a non-existing queue group
    ${qgrp_non_exist} =    Execute Command    em_qgrp_print -n non-exist
    Should Match Regexp    ${qgrp_non_exist}    ^Error: can't find queue group non-exist!\\s?(?m)

Test Core Print
    [Documentation]    Test em_core_print command and check if it outputs core
    ...    mapping as expected.

    ${core_map} =    Execute Command    em_core_print
    Should Match Regexp    ${core_map}    Core mapping: EM-core <-> phys-core <-> ODP-thread
