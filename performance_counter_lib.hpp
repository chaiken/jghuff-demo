#include <linux/hw_breakpoint.h> //defines several necessary macros
#include <linux/perf_event.h>    //defines performance counter events
#include <sys/ioctl.h>
#include <sys/resource.h> //defines the rlimit struct and getrlimit
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct read_format { // read_format is declared in a performance counter header.
                     // However, it is never defined, so we have to define it
                     // ourselves
  unsigned long long nr = 0; // how many events there are
  struct {
    unsigned long long value; // the value of event nr
    unsigned long long id;    // the id of event nr
  } values[];
};

struct pcounter { // our Modern C++ abstraction for a generic performance
                  // counter group for a PID
  pid_t pid;

  std::array<std::array<struct perf_event_attr, 1>, 2>
      perfstruct; // organize the events per PID like [x, y] such that x is the
                  // group, and y is the event within that group
  std::array<std::array<unsigned long long, 1>, 2>
      gid; // the id stores the event type in a numerical fashion
  std::array<std::array<unsigned long long, 1>, 2>
      gv;                                // the values of the events
  std::array<std::array<int, 1>, 2> gfd; // the file descriptors for the
                                         // counters

  char buf[96]; // buf size equation: (maximum events counted * 16) + 8
  struct read_format *data = reinterpret_cast<struct read_format *>(
      buf); // in C, this would have been a void*, but C++ lets us do a raw
            // conversion explicitly
};

void setLimits();

std::vector<pid_t> getProcessChildPids(pid_t pid);
void createCounters(std::vector<struct pcounter *> &counters,
                    const std::vector<pid_t> &pids);
void resetAndEnableCounters(const std::vector<struct pcounter *> &counters);

void disableCounters(const std::vector<struct pcounter *> &counters);

void readCounters(std::vector<struct pcounter *> &counters);

std::vector<pid_t> getProcessChildPids(pid_t pid);

void createCounters(std::vector<struct pcounter *> &counters,
                    const std::vector<pid_t> &pids);

void cullCounters(std::vector<struct pcounter *> &counters,
                  const std::vector<pid_t> &pids);

void printResults(const long long cycles, const long long instructions);

void getPidDelta(const pid_t pid, std::vector<struct pcounter *> &MyCounters,
                 std::vector<pid_t> &currentPids);
