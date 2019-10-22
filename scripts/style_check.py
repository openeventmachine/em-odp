#!/usr/bin/env python

import os
import sys
import threading

# Conjure repo root dir. Presume that parent of script dir is repo root folder.
ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

# Style check script command
C_CHECK = ROOT_DIR + "/scripts/em_odp_check "

# File extensions to check
EXT = ('.c', '.h')

# Start checking from these folders
CHECK_DIRS = ["include", "src", "programs"]

# Filter out these directories
IGNORE_DIRS = []

# Set absolute paths to check dirs
CHECK_DIRS = [os.path.join(ROOT_DIR, dir) for dir in CHECK_DIRS]

# Multithread safe function to run the check script for file in file_list
def run_checks():
    global rc

    while file_list:
        file = file_list.pop()

        # Option to run different check script for different files
        # if file.endswith(('.c', '.h')):
        cmd = C_CHECK + file

        if os.system(cmd) is not 0:
            rc = 1

rc = 0
file_list = []
threads = []

# Collect and add all files to be checked to file_list
for check_dir in CHECK_DIRS:
    for root, dirs, files in os.walk(check_dir):
        if not any(path in root for path in IGNORE_DIRS):
            for file in files:
                if file.endswith(EXT):
                    file_list.append(os.path.join(root, file))


# Run checks on all files in file_list with multiple threads
for i in range(5):
    t = threading.Thread(target=run_checks)
    threads.append(t)
    t.start()

# Wait for threads
for t in threads:
    t.join()

if rc == 1:
    print("Style errors found.")
else:
    print("Style check OK!")

sys.exit(rc)
