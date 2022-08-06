CXX=/usr/bin/g++
CXXFLAGS = -std=c++17 -ggdb -Wall -Wextra -Werror -g -O0 -fno-inline -fsanitize=address,undefined -isystem $(GTEST_DIR)/include
LDFLAGS= -ggdb -g -fsanitize=address

CLANG_TIDY_BINARY=/usr/bin/clang-tidy
CLANG_TIDY_OPTIONS=--warnings-as-errors --header_filter=.*
CLANG_TIDY_CLANG_OPTIONS=-std=c++17 -x c++
CLANG_TIDY_CHECKS=bugprone,core,cplusplus,cppcoreguidelines,deadcode,modernize,performance,readability,security,unix,apiModeling.StdCLibraryFunctions,apiModeling.google.GTest

clean:
	rm -rf *.o *~ Demo

performance_counter_lib: performance_counter_lib.cpp performance_counter_lib.hpp

Demo: Demo.cpp performance_counter_lib.cpp
	make clean
	$(CXX) $(CXXFLAGS)  performance_counter_lib.cpp Demo.cpp $(LDFLAGS) -o Demo

test: Demo
	sudo setcap "cap_perfmon+ep" Demo

# clang-tidy as of 14.0.6 does not support C++20 well.
Demo-clang-tidy: Demo.cpp performance_counter_lib
	make clean
	$(CLANG_TIDY_BINARY) $(CLANG_TIDY_OPTIONS) -checks=$(CLANG_TIDY_CHECKS)  performance_counter_lib.cpp Demo.cpp -- $(CLANG_TIDY_CLANG_OPTIONS)