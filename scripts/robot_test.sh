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
apps["timer_hello"]=programs/example/add-ons/timer_hello
apps["timer_test"]=programs/example/add-ons/timer_test
apps["api_hooks"]=programs/example/api-hooks/api_hooks
apps["dispatcher_callback"]=programs/example/dispatcher/dispatcher_callback
apps["error"]=programs/example/error/error
apps["event_group_abort"]=programs/example/event_group/event_group_abort
apps["event_group_assign_end"]=programs/example/event_group/event_group_assign_end
apps["event_group"]=programs/example/event_group/event_group
apps["event_group_chaining"]=programs/example/event_group/event_group_chaining
apps["fractal"]=programs/example/fractal/fractal
apps["hello"]=programs/example/hello/hello
apps["queue_group"]=programs/example/queue_group/queue_group
apps["ordered"]=programs/example/queue/ordered
apps["queue_types_ag"]=programs/example/queue/queue_types_ag
apps["queue_types_local"]=programs/example/queue/queue_types_local

# Performance Apps
apps["atomic_processing_end"]=programs/performance/atomic_processing_end
apps["loop"]=programs/performance/loop
apps["pairs"]=programs/performance/pairs
apps["queue_groups"]=programs/performance/queue_groups
apps["queues"]=programs/performance/queues
apps["queues_local"]=programs/performance/queues_local
apps["queues_unscheduled"]=programs/performance/queues_unscheduled
apps["send_multi"]=programs/performance/send_multi

# Set up conf files for robot tests
odp_conf="odp/config/odp-linux-generic.conf"
em_conf="config/em-odp.conf"

# Robot Tests
for app in "${!apps[@]}"; do
  for ((i = 0; i < ${#core_masks[@]}; i++)); do
    for ((j = 0; j < ${#modes[@]}; j++)); do
      ODP_CONFIG_FILE="${odp_conf}" \
        EM_CONFIG_FILE="${em_conf}" \
        robot \
        --variable application:"${apps[${app}]}" \
        --variable core_mask:"${core_masks[$i]}" \
        --variable mode:"${modes[$j]}" \
        --log NONE \
        --report NONE \
        --output NONE \
        ci/${app}.robot
    done
  done
done
