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

constexpr std::chrono::seconds SLEEPTIME = std::chrono::seconds(5);
constexpr uint64_t SLEEPCOUNT = std::chrono::seconds(5).count();
constexpr uint32_t MIN_COUNTER_READSIZE = 40;

// The two kinds of perf events that are observed.
constexpr uint32_t CYCLES = 0U;
constexpr uint32_t INSTRUCTIONS = 1U;
constexpr uint32_t OBSERVED_EVENTS = 2U;

struct read_format { // read_format is declared in a performance counter header.
                     // However, it is never defined, so we have to define it
                     // ourselves
  read_format() {}

  unsigned long long nr; // how many events there are
  // Array values has two elements because there are two kinds of perf events
  // observed.
  struct {
    // the value of a particular event
    unsigned long long value;
    // the id of a particular event
    unsigned long long id;
  } values[OBSERVED_EVENTS];
};

struct pcounter { // our Modern C++ abstraction for a generic performance
                  // counter group for a PID
  pcounter(pid_t p)
      : pid(p), perfstruct{}, event_id{}, event_value{}, group_fd{},
        event_data{} {}

  pid_t pid;

  // organize the events per PID like [x, y] such that x is the
  // group, and y is the event within that group
  std::array<struct perf_event_attr, 2> perfstruct;
  // the id stores the event type in a numerical fashion
  std::array<unsigned long long, 2> event_id{0, 0};
  // the values of the events
  std::array<unsigned long long, 2> event_value{0, 0};
  // the group file descriptors for the counters
  // Each file descriptor corresponds to one event that is measured; these can
  // be grouped  together  to  measure multiple events simultaneously.
  std::array<int, 2> group_fd{0, 0};

  union {
    // buf size equation: (maximum events counted * 16) + 8
    char buf[MIN_COUNTER_READSIZE];
    struct read_format per_event_values;
  } event_data;
};

bool operator==(const struct pcounter &a, const struct pcounter &b);

std::vector<pid_t> getProcessChildPids(const std::string &proc_path, pid_t pid);

void createCounters(std::vector<struct pcounter> &counters,
                    const std::vector<pid_t> &pids);

void resetAndEnableCounters(const std::vector<struct pcounter> &counters);

void disableCounters(const std::vector<struct pcounter> &counters);

void readCounters(std::vector<struct pcounter> &counters);

void cullCounters(std::vector<struct pcounter> &counters,
                  const std::vector<pid_t> &pids);

void printResults(const long long cycles, const long long instructions);

void getPidDelta(const std::string &proc_path, const pid_t pid,
                 std::vector<struct pcounter> &MyCounters,
                 std::vector<pid_t> &currentPids);
