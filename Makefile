# See ~/gitsrc/googletest/googletest/make/Makefile
# Points to the root of Google Test, relative to where this file is.
GTEST_DIR = $(HOME)/gitsrc/googletest
# Wrong: do not include $(GTEST_DIR)/include/gtest/internal/*.h
GTEST_HEADERS = $(GTEST_DIR)/googletest/include
GTEST_LIB_PATH=$(GTEST_DIR)/build/lib
# See ~/gitsrc/googletest/googletest/README.md.
# export GTEST_DIR=/home/alison/gitsrc/googletest/
# g++ -std=c++11 -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -pthread -c ${GTEST_DIR}/src/gtest-all.cc
# cd make; make all
# 'make' in the README.md above doesn't create libgtest_main.a.  'make all' does.
GTEST_LIBS= $(GTEST_LIB_PATH)/libgtest.a $(GTEST_LIB_PATH)/libgtest_main.a

CXX=/usr/bin/g++
CXXFLAGS = -std=c++17 -ggdb -Wall -Wextra -Werror -g -O0 -fno-inline -fsanitize=address,undefined -isystem $(GTEST_HEADERS)
CXXFLAGS-NOSANITIZE = -std=c++17 -ggdb -Wall -Wextra -Werror -g -O0 -fno-inline -isystem $(GTEST_HEADERS)
LDFLAGS= -ggdb -g -fsanitize=address -L$(GTEST_LIB_PATH)
LDFLAGS-NOSANITIZE= -ggdb -g -L$(GTEST_LIB_PATH)
LDFLAGS-NOTEST= -ggdb -g -fsanitize=address

CLANG_TIDY_BINARY=/usr/bin/clang-tidy
CLANG_TIDY_OPTIONS=--warnings-as-errors --header_filter=.*
CLANG_TIDY_CLANG_OPTIONS=-std=c++17 -x c++  -I ~/gitsrc/googletest/googletest/include/
CLANG_TIDY_CHECKS=bugprone,core,cplusplus,cppcoreguidelines,deadcode,modernize,performance,readability,security,unix,apiModeling.StdCLibraryFunctions,apiModeling.google.GTest

clean:
	rm -rf *.o *~ Demo performance_counter_lib_test

performance_counter_lib: performance_counter_lib.cpp performance_counter_lib.hpp

%_test:  %.o %_test.o
	$(CXX) $(CXXFLAGS)  $(LDFLAGS) $^ $(GTEST_LIBS) -o $@

Demo: Demo.cpp performance_counter_lib.cpp
	make clean
	$(CXX) $(CXXFLAGS)  performance_counter_lib.cpp Demo.cpp $(LDFLAGS) -o Demo

setcaps: Demo
	sudo setcap "cap_perfmon+ep" Demo

# clang-tidy as of 14.0.6 does not support C++20 well.
Demo-clang-tidy: Demo.cpp performance_counter_lib.cpp performance_counter_lib.hpp performance_counter_lib_test.cpp
	make clean
	$(CLANG_TIDY_BINARY) $(CLANG_TIDY_OPTIONS) -checks=$(CLANG_TIDY_CHECKS)  performance_counter_lib.cpp Demo.cpp performance_counter_lib.hpp performance_counter_lib_test.cpp -- $(CLANG_TIDY_CLANG_OPTIONS)

COVERAGE_EXTRA_FLAGS = --coverage

performance_counter_lib_test_coverage: CXXFLAGS = $(CXXFLAGS-NOSANITIZE) $(COVERAGE_EXTRA_FLAGS)
performance_counter_lib_test_coverage: LDFLAGS = $(LDFLAGS-NOSANITIZE)
performance_counter_lib_test_coverage:  performance_counter_lib_test.cpp performance_counter_lib.cpp $(GTESTHEADERS)
	make clean
	$(CXX) $(CXXFLAGS)  $(LDFLAGS) $^ $(GTEST_LIBS) -o $@
	run_lcov.sh
