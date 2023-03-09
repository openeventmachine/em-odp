# Nokia: Event Machine Robot Tests

## Install robotframework in Ubuntu

```bash
sudo apt install python3-pip
sudo pip3 install robotframework
```

## Manually run individual robot tests

```bash
robot --variable APPLICATION:<path_to_application>/<application> --variable TASKSET_CORES:<cpu_list> --variable CORE_MASK:<core_mask> --variable APPLICATION_MODE:<t/p> <path_to_robot_files>/<application>.robot
e.g
$ robot --variable APPLICATION:/home/username/EM/em-odp/build/programs/example/hello/hello --variable TASKSET_CORES:1-7 --variable CORE_MASK:0xFE --variable APPLICATION_MODE:t /home/username/EM/em-odp/robot-tests/hello.robot
```
