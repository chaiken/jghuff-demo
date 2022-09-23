#!/bin/bash
#
# Generate HTML-formatted gcov coverage analysis following the approach of
# https://qiaomuf.wordpress.com/2011/05/26/use-gcov-and-lcov-to-know-your-test-coverage/

set -e
set -u
set -o pipefail

if [ ! -e /usr/bin/lcov ]; then
  echo "Please install the lcov package."
  exit 1
fi

# Note: .gcno files are created by compilation with --coverage switch.
#       .gcda files are created by executing the binary directly or with
# 	 "lcov -c -o foo.info".

readonly test_name="performance_counter_lib_test_coverage"

# Actually run the test.  Not actually necessary, but test failure will as a
# side benefit terminate the script.
./"$test_name"

# "man lcov" Capture coverage data; creates info file
# Use the --directory option to capture counts for a user  space  program.
# Running the binary directly creates .gcda files, but only running this command
# creates .info files.  Running the command with existing .gcda files does not
# work.
lcov --base-directory . --directory . --capture -o "$test_name".info

# The blog posting above mentioned the same tracefile as input and output,
# which did not work for me.
# There does not appear to be any way to remove the test binary itself
# from the coverage analysis, which makes the resulting coverage percentages
# too high.
lcov --remove "$test_name".info \
     "/usr/*" \
     "/home/alison/gitsrc/googletest/*" \
     -o "$test_name"_processed.info

genhtml -o . -t "${test_name} coverage" \
    --num-spaces 2 "$test_name"_processed.info

# Removes all gcda files -- not what is needed.
# lcov --base-directory . --directory . --zerocounters
