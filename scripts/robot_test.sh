#!/bin/bash

set -o errexit # abort on nonzero exit status
set -o nounset # abort on undeclared variable

# Github hosted Ubuntu runners are all x86_64 architecture
# Github hosted ubuntu runners supports 2-core CPU
core_masks=("0x3")

# Default Mode
modes=("t")

# Apps
declare -A apps

# Example Apps
apps["api_hooks"]=programs/example/api-hooks/api_hooks
apps["dispatcher_callback"]=programs/example/dispatcher/dispatcher_callback
# emcli runs hello program with em-odp.conf cli.enable=true and checks extra regex"
apps["emcli"]=programs/example/hello/hello
apps["error"]=programs/example/error/error
apps["event_group_abort"]=programs/example/event_group/event_group_abort
apps["event_group_assign_end"]=programs/example/event_group/event_group_assign_end
apps["event_group_chaining"]=programs/example/event_group/event_group_chaining
apps["event_group"]=programs/example/event_group/event_group
apps["fractal"]=programs/example/fractal/fractal
apps["hello"]=programs/example/hello/hello
apps["ordered"]=programs/example/queue/ordered
apps["queue_types_ag"]=programs/example/queue/queue_types_ag
apps["queue_types_local"]=programs/example/queue/queue_types_local
apps["queue_group"]=programs/example/queue_group/queue_group
apps["timer_hello"]=programs/example/add-ons/timer_hello
apps["timer_test"]=programs/example/add-ons/timer_test

# Performance Apps
apps["atomic_processing_end"]=programs/performance/atomic_processing_end
apps["loop"]=programs/performance/loop
apps["loop_multircv"]=programs/performance/loop_multircv
apps["loop_refs"]=programs/performance/loop_refs
apps["pairs"]=programs/performance/pairs
apps["queue_groups"]=programs/performance/queue_groups
apps["queues_local"]=programs/performance/queues_local
apps["queues_unscheduled"]=programs/performance/queues_unscheduled
apps["queues"]=programs/performance/queues
apps["send_multi"]=programs/performance/send_multi
apps["timer_periodic"]=programs/performance/timer_test_periodic

# Set up conf files for robot tests
odp_conf="odp/config/odp-linux-generic.conf"
# - set system.cpu_mhz = 2800
sed -i 's/cpu_mhz\s*=.*/cpu_mhz = 2800/' "${odp_conf}"
# - set system.cpu_mhz_max = 2800
sed -i 's/cpu_mhz_max\s*=.*/cpu_mhz_max = 2800/' "${odp_conf}"
# - set timer.inline = 1: Use inline timer implementation
sed -i 's/inline\s*=.*/inline = 1/' "${odp_conf}"
#  - set timer.inline_thread_type = 1: Only worker threads process non-private timer pools
sed -i 's/inline_thread_type\s*=.*/inline_thread_type = 1/' "${odp_conf}"

em_conf="config/em-odp.conf"
#  - set pool.statistics_enable = true
sed -i 's/statistics_enable.*/statistics_enable = true/' "${em_conf}"
#  - set queue.priority.map_mode = 1
sed -i 's/\(^[[:space:]]*map_mode.*=[[:space:]]\).*/\11/' "${em_conf}"
#  - set esv.prealloc_pools = false
sed -i 's/prealloc_pools.*/prealloc_pools = false/' "${em_conf}"

# Robot Tests
for app in "${!apps[@]}"; do
  if [[ "${apps[${app}]}" == *"example"* ]]; then
    robot_file_path="robot-tests/example"
  else
    robot_file_path="robot-tests/performance"
  fi

  # Enable CLI only for emcli test
  #  - set cli.enable = true
  if [[ "${app}" == "emcli" ]]; then
    # There are two lines containing "enable =" in em-odp.conf, in
    # order to modify only cli.enable, regular expressions are used
    # to determine on which lines the sed command will be executed.
    # E.g. here "/^cli:\s{/,/^\t#\sIP\saddress/" specifies that the
    # sed command executes only from the line staring with "cli: {"
    # to the line starting with "\t# IP address".
    sed -i '/^cli:\s{/,/^\t#\sIP\saddress/s/\tenable\s*=.*/\tenable = true/' "${em_conf}"
  fi

  for ((i = 0; i < ${#core_masks[@]}; i++)); do
    for ((j = 0; j < ${#modes[@]}; j++)); do
        ODP_CONFIG_FILE="${odp_conf}" \
        EM_CONFIG_FILE="${em_conf}" \
        robot \
        --variable APPLICATION:"${apps[${app}]}" \
        --variable TASKSET_CORES:"0-1" \
        --variable CORE_MASK:"${core_masks[$i]}" \
        --variable APPLICATION_MODE:"${modes[$j]}" \
        --log NONE \
        --report NONE \
        --output NONE \
        "${robot_file_path}/${app}.robot"
    done
  done
done
