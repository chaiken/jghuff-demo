// Jackson Huff's LinuxCon 2022 performance counter demo program (that you can
// learn from!)

/*The ISC License

Copyright 2022 by Jackson Huff

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies. THE SOFTWARE IS PROVIDED "AS
IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT
SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.*/

#include "performance_counter_lib.hpp"

#include <thread>

constexpr char PROC_PATH[] = "/proc/";

// start by cranking up resource limits so we can track programs with many
// threads
void setLimits() {
  if (getuid()) { // the root user commonly has different and lower resource
                  // limit hard ceilings than nonroot users, so skip this if we
                  // are root
    struct rlimit rlimits;
    // std::cout << "Setting resource limits" << std::endl;
    if (getrlimit(RLIMIT_NOFILE, &rlimits) == -1) {
      std::cout << "Error getting resource limits; errno = " << errno
                << std::endl;
    }
    rlimits.rlim_cur =
        rlimits.rlim_max; // resize soft limit to max limit; the max limit is a
                          // ceiling for the soft limit
    if (setrlimit(RLIMIT_NOFILE, &rlimits) == -1) {
      std::cout << "Error changing resource limits; errno = " << errno
                << std::endl;
    }
  }
}

int main(int argc, char **argv) {
  setLimits();

  // our counter and PID data
  std::vector<struct pcounter> MyCounters = {};
  pid_t pid;

  // get a PID to track from the user
  if (argc > 2) {
    fprintf(stderr, "Usage is 'sudo ./Demo <pid>'.\n");
    exit(EXIT_FAILURE);
  }
  if (2 == argc) {
    errno = 0;
    long val{strtol(argv[1], NULL, 10)};
    if (errno || (0 == val)) {
      fprintf(stderr, "%s is not a valid PID.\n", argv[1]);
      exit(EXIT_FAILURE);
    }
    pid = val;
  }
  if (!pid) {
    std::string input;
    std::cout << "Enter a PID " << std::flush;
    std::cin >> input;
    try {
      pid = std::stol(input);
    } catch (...) { // PID must be a number
      std::cout << "Invalid PID" << std::endl;
      return 1;
    }
  }

  // the next step is to make counters for all the known children of our newly
  // obtained PID find all the children, then make counters for them
  std::vector<pid_t> currentPids = getProcessChildPids(PROC_PATH, pid);
  if (currentPids.empty()) {
    exit(EXIT_SUCCESS);
  }
  createCounters(MyCounters, currentPids);

  while (true) {
    resetAndEnableCounters(MyCounters);
    std::this_thread::sleep_for(
        SLEEPTIME); // cross-platform method of sleeping, though it doesn't
                    // matter if you are only targeting Linux
    disableCounters(MyCounters);
    readCounters(MyCounters);
    long long cycles = 0;
    long long instructions = 0;
    for (const auto &s : MyCounters) {
      cycles += s.event_value[0];
      instructions += s.event_value[1];
    }
    printResults(cycles, instructions);
    getPidDelta(PROC_PATH, pid, MyCounters, currentPids);
  }
}
