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

int main() {
  setLimits();

  // our counter and PID data
  std::vector<struct pcounter *> MyCounters = {};

  // get a PID to track from the user
  std::string input;
  pid_t pid;
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
  std::vector<pid_t> currentPids = getProcessChildPids(pid);
  createCounters(MyCounters, currentPids);

  while (true) {
    resetAndEnableCounters(MyCounters);
    std::this_thread::sleep_for(std::chrono::seconds(
        5)); // cross-platform method of sleeping, though it doesn't matter if
             // you are only targeting Linux
    disableCounters(MyCounters);
    readCounters(MyCounters);
    long long cycles = 0;
    long long instructions = 0;
    for (const auto &s : MyCounters) {
      cycles += s->gv[0][0];
      instructions += s->gv[0][1];
    }
    printResults(cycles, instructions);
    getPidDelta(pid, MyCounters, currentPids);
  }
}
