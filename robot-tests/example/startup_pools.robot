*** Comments ***
Copyright (c) 2020-2022, Nokia Solutions and Networks
All rights reserved.
SPDX-License-Identifier: BSD-3-Clause


*** Settings ***
Documentation    Test Startup Pools with hello application
Library    OperatingSystem
Resource    ../common.resource
Test Setup        Set Log Level    TRACE
Test Teardown     Kill Any Hanging Applications


*** Variables ***
${HELLO_PRINT} =    SEPARATOR=
...    Hello world from EO [AB]\!\\s+My queue is 0x[0-9|A-F|a-f]+.\\s+I'm on co
...    re [0-9]+.\\s+Event seq is [0-9]+.

@{REGEX_MATCH} =
...    EM-startup_pools config:
...    startup_pools.num: 1
...    startup_pools.conf\\[0\\].name: default
...    startup_pools.conf\\[0\\].pool: 1
...    startup_pools.conf\\[0\\].pool_cfg.event_type: EM_EVENT_TYPE_SW
...    startup_pools.conf\\[0\\].pool_cfg.align_offset.in_use: true
...    startup_pools.conf\\[0\\].pool_cfg.align_offset.value: 0
...    startup_pools.conf\\[0\\].pool_cfg.user_area.in_use: true
...    startup_pools.conf\\[0\\].pool_cfg.user_area.size: 0
...    startup_pools.conf\\[0\\].pool_cfg.pkt.headroom.in_use: false
...    startup_pools.conf\\[0\\].pool_cfg.pkt.headroom.value: 0
...    startup_pools.conf\\[0\\].pool_cfg.num_subpools: 4
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[0\\].size: 256
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[0\\].num: 16384
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[0\\].cache_size: 64
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[1\\].size: 512
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[1\\].num: 1024
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[1\\].cache_size: 32
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[2\\].size: 1024
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[2\\].num: 1024
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[2\\].cache_size: 16
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[3\\].size: 2048
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[3\\].num: 1024
...    startup_pools.conf\\[0\\].pool_cfg.subpools\\[3\\].cache_size: 8
...    ${HELLO_PRINT}
...    Done\\s*-\\s*exit

# For prints that are over 120 characters
${NUM_CONF_NOT_MATCH_PRINT} =    SEPARATOR=
...    The number of pool configuration\\(s\\) given in\n
...    'startup_pools.conf':2 does not match number of\n
...    startup_pools specified in 'startup_pools.num': 3

${NUM_SUBPOOLS_NOT_MATCH_PRINT} =    SEPARATOR=
...    The number of subpool configuration given\n
...    in 'startup_pools.conf\\[0\\].pool_cfg.subpools' does not matc
...    h 'startup_pools.conf\\[0\\].pool_cfg.num_subpools'.

${NO_SUBPOOLS_SIZE_PRINT} =    SEPARATOR=
...    Option 'startup_pools.conf\\[0\\].pool_cfg.subpools\\[0\\].size' no
...    t found or wrong type.

${NO_SUBPOOLS_NUM_PRINT} =    SEPARATOR=
...    Option 'startup_pools.conf\\[0\\].pool_cfg.subpools\\[0\\].num' no
...    t found or wrong type.

${INVALID_ALIGN_OFFSET_PRINT} =    SEPARATOR=
...    Invalid 'startup_pools.conf\\[0\\].pool_cfg.align_offset.value': 3\n
...    Max align_offset is \\d+ and it must be power of 2

${INVALID_PKT_HEADROOM_PRINT} =    SEPARATOR=
...    'startup_pools.conf\\[0\\].pool_cfg.pkt.headroom.value' 512 too larg
...    e \\(max=[0-9]+\\)

${INVALID_NAME_PRINT} =    SEPARATOR=
...    'startup_pools.conf\\[0\\].name' has wrong data type\\(expect string\\)

${INVALID_OR_NO_IN_USE_PRINT} =    SEPARATOR=
...    'startup_pools.conf\\[0\\].pool_cfg.align_offset.in_use' not found or wrong type

${NO_VALUE_PRINT} =    SEPARATOR=
...    'startup_pools.conf\\[0\\].pool_cfg.align_offset.value' not found or wrong type

# Output for each test startup_pools conf
&{CONF_OUT} =
...    bad_num.conf=Number of startup_pools 64 is too large or too small
...    no-conf.conf=Conf option 'startup_pools.conf' not found
...    num-conf-not-match.conf=${NUM_CONF_NOT_MATCH_PRINT}
...    no-pool-cfg.conf=Conf option 'startup_pools.conf\\[0\\].pool_cfg' not found
...    no-event-type.conf='startup_pools.conf\\[0\\].pool_cfg.event_type' not found.
...    no-num-subpools.conf='startup_pools.conf\\[0\\].pool_cfg.num_subpools' not found.
...    no-subpools.conf='startup_pools.conf\\[0\\].pool_cfg.subpools' not found.
...    num-subpools-not-match.conf=${NUM_SUBPOOLS_NOT_MATCH_PRINT}
...    no-subpools-size.conf=${NO_SUBPOOLS_SIZE_PRINT}
...    no-subpools-num.conf=${NO_SUBPOOLS_NUM_PRINT}
...    default-name-non-default-id.conf=Default name "default" with non-default ID 2
...    default-id-non-default-name.conf=Default pool ID 1 with non-default name "test"
...    invalid-align-offset.conf=${INVALID_ALIGN_OFFSET_PRINT}
...    invalid-user-area.conf=Event user area too large: 512
...    invalid-pkt-headroom.conf=${INVALID_PKT_HEADROOM_PRINT}
...    invalid-name.conf=${INVALID_NAME_PRINT}
...    no-align-offset-in-use-value.conf=${INVALID_OR_NO_IN_USE_PRINT}
...    no-align-offset-in-use.conf=${INVALID_OR_NO_IN_USE_PRINT}
...    no-align-offset-value.conf=${NO_VALUE_PRINT}

# Directory where all confs to be tested are stored
${CONF_DIR} =    ${CURDIR}${/}test-startup-pools-confs

${MOUNT_POINT} =    /dev/hugepages2M


*** Keywords ***
Set Up Hugepage
    [Documentation]    Set up huge pages
    Run    umount ${MOUNT_POINT}
    Run    rm -fr ${MOUNT_POINT}
    Run    mkdir ${MOUNT_POINT}
    Run    mount --types hugetlbfs --options pagesize=2M none ${MOUNT_POINT}
    Run    echo 4096 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages


*** Test Cases ***
Test Invalid Startup Pool Confs
    [Documentation]    Test all invalid 'startup_pools' options and verify that
    ...    they fail as expected.
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    ${status}    Run Keyword And Return Status    Get Environment Variable    SKIP_INVALID_CONFS
    Skip if    ${status}    msg=Skipped testing invalid startup pool confs

    FOR    ${conf}    IN    @{CONF_OUT}
        # In this test, the hello program is re-started for each startup_pools
        # setting, huge pages are thus accumulated and used up. To fix this,
        # huge pages are cleared and setup before running the hello program.
        Set Up Hugepage

        # Include the 'startup_pools' conf to em-odp.conf
        # sed syntax explained: $ matches the last line, a is the append command
        Run    sed -i -e '$a@include "${CONF_DIR}/${conf}"' %{EM_CONFIG_FILE}

        # Run hello program with given arguments
        ${output} =    Process.Run Process    taskset    -c    ${TASKSET_CORES}
        ...    ${APPLICATION}
        ...    @{CM_ARGS}
        ...    stderr=STDOUT
        ...    shell=True
        ...    stdout=${TEMPDIR}/stdout.txt

        # Delete the 'starup_pools' option that has been tested
        Run    sed -i '$ d' %{EM_CONFIG_FILE}

        # Check output
        Should Match Regexp    ${output.stdout}    ${CONF_OUT}[${conf}]
    END

Test Default Startup Pool Conf
    [Documentation]    Test the default 'startup_pools' option that is commented
    ...    out in em-odp.conf. The default pool configuration should override the
    ...    one passed to em_init(), namely, em_conf_t::default_pool_cfg.
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    # Uncomment option 'startup_pools'
    Run    sed -i '/^#startup_pools:\\s{/,/^#}/s/^#//g' %{EM_CONFIG_FILE}

    # Run hello program, should work as usual, except that startup_pools config
    # should be printed out
    Run Keyword And Continue On Failure    Run EM-ODP Test    sleep_time=30
    ...    regex_match=${REGEX_MATCH}

    # Comment option 'startup_pools' back
    Run    sed -i '/^startup_pools:\\s{/,/^}/s/^/#/g' %{EM_CONFIG_FILE}

Test Non-default Startup Pool Conf
    [Documentation]    Run hello program with a valid and non-default pool
    ...    configuration configured via 'startup_pools' in em-odp.conf
    [TAGS]    ${CORE_MASK}    ${APPLICATION_MODE}

    # Include valid non-default pool configurations to em-odp.conf
    Run    sed -i -e '$a@include "${CONF_DIR}/non-default-pools.conf"' %{EM_CONFIG_FILE}

    # Run hello program, should work as usual, except that now the program
    # should have more pools: default pool which is configured through
    # parameter passed to em_init(), appl_pool_1 and the five configured
    # in file non-default-pool.conf
    @{three_pools} =    Create List    EM Event Pools: 7
    Run Keyword And Continue On Failure    Run EM-ODP Test    sleep_time=30
    ...    regex_match=${three_pools}

    # Delete the 'starup_pools' setting that has been tested
    Run    sed -i '$ d' %{EM_CONFIG_FILE}
