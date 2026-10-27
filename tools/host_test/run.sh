#!/bin/sh
# Builds and runs the host-side tests (needs only g++). Run from the project root.
set -e
cd "$(dirname "$0")/../.."
g++ -std=gnu++11 -Wall -Wextra -O1 tools/host_test/host_test.cpp lib/switchreset/*.cpp -o /tmp/switchreset_host_test
/tmp/switchreset_host_test
