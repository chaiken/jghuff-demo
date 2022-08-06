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

#include <sys/resource.h> //defines the rlimit struct and getrlimit
#include <thread>

int main() {
  // start by cranking up resource limits so we can track programs with many
  // threads
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

  // set up our data variables that both the backend and the frontend will use
  // in real programs, these are the only interface between the frontend and the
  // backend, and so these should be atomic (but they don't have to be here)
  long pid;
  long long cycles;
  long long instructions;

  // our counter and PID data
  std::vector<struct pcounter *> MyCounters = {};
  std::vector<long> newPids = {};
  std::vector<long> diffPids = {};
  std::vector<long> currentPids;

  // get a PID to track from the user
  std::string input;
  std::cout << "Enter a PID " << std::flush;
  std::cin >> input;
  try {
    pid = std::stol(input);
  } catch (...) { // PID must be a number
    std::cout << "Invalid PID" << std::endl;
    return 1;
  }

  // the next step is to make counters for all the known children of our newly
  // obtained PID find all the children, then make counters for them
  currentPids = getProcessChildPids(pid);
  createCounters(MyCounters, currentPids);

  while (true) {
    resetAndEnableCounters(MyCounters);
    std::this_thread::sleep_for(std::chrono::seconds(
        5)); // cross-platform method of sleeping, though it doesn't matter if
             // you are only targeting Linux
    disableCounters(MyCounters);
    readCounters(MyCounters);
    cycles = 0;
    instructions = 0;
    for (const auto &s : MyCounters) {
      cycles += s->gv[0][0];
      instructions += s->gv[0][1];
    }

    // calculate the PID delta
    newPids = getProcessChildPids(pid);
    diffPids.clear();
    std::set_difference(
        newPids.begin(), newPids.end(), currentPids.begin(), currentPids.end(),
        std::inserter(diffPids,
                      diffPids.begin())); // calculate what's in newPids that
                                          // isn't in oldPids
    createCounters(MyCounters, diffPids);
    diffPids.clear();
    std::set_difference(
        currentPids.begin(), currentPids.end(), newPids.begin(), newPids.end(),
        std::inserter(diffPids,
                      diffPids.begin())); // calculate what's in oldPids that
                                          // isn't in newPids
    cullCounters(MyCounters, diffPids);
    currentPids = newPids;

    // the frontend
    // in real programs, this section should be in the calling thread
    // we only access our frontend variables here, as everything having to do
    // with counters is abstracted away elsewhere
    std::cout << "----------------------------------------------------"
              << std::endl;
    std::cout << "Got " << cycles / 5 << " ("
              << (float)(cycles / 5) / 1000000000
              << " billion) cycles per second"
              << std::endl; // divide our data variables by the sleep time to
                            // get per-second measurements (you could make this
                            // a constexpr variable or a macro)
    std::cout << "Got " << instructions / 5 << " ("
              << (float)(instructions / 5) / 1000000000
              << " billion) instructions per second" << std::endl;
    std::cout
        << "IPC: " << (float)instructions / (float)cycles
        << std::endl; // footgun: never forget to convert to float (or double)
                      // when dividing to get a result with decimals
  }
}
