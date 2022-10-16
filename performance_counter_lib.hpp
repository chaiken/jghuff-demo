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
#include <map>
#include <regex>
#include <set>
#include <string>

namespace fs = std::filesystem;

constexpr std::chrono::seconds SLEEPTIME = std::chrono::seconds(5);
constexpr uint64_t SLEEPCOUNT = std::chrono::seconds(5).count();

// The two kinds of perf events that are observed.
constexpr uint32_t CYCLES = 0U;
constexpr uint32_t INSTRUCTIONS = 1U;
constexpr uint32_t OBSERVED_EVENTS = 2U;
constexpr uint32_t COUNTER_READSIZE = OBSERVED_EVENTS * 16U + 8U;

/*
  From "man perf_event_open:"
       Here is the layout of the data returned by a read:
       If  PERF_FORMAT_GROUP  was specified to allow reading all events in a
         group at once:
             struct read_format {
                 u64 nr;             The number of events
                 struct {
                     u64 value;      The value of the event
                     u64 id;         if PERF_FORMAT_ID
                 } values[nr];
             };
*/
struct read_format {
  read_format() {}

  // nr     The number of events in this file descriptor.
  uint64_t nr;
  struct {
    // value  An unsigned 64-bit value containing the counter result.
    uint64_t value;
    // id     A globally unique value for this particular event
    uint64_t id;
  } values[OBSERVED_EVENTS];
};

struct pcounter { // our Modern C++ abstraction for a generic performance
                  // counter group for a PID
  pcounter(pid_t p)
      : pid(p), perfstruct{}, event_id{}, event_value{}, group_fd{},
        event_data{} {}

  pid_t pid;

  // The array contains a pair of specifications for the two observed events.
  std::array<struct perf_event_attr, 2> perfstruct;
  // The id pair is associated with the two events in the group.
  std::array<uint64_t, 2> event_id{0, 0};
  // The array holds the measured values of the events.
  std::array<uint64_t, 2> event_value{0, 0};
  // Each file descriptor corresponds to one event that is measured; these can
  // be grouped  together  to  measure multiple events simultaneously.
  std::array<int, 2> group_fd{0, 0};

  union {
    char buf[COUNTER_READSIZE];
    struct read_format per_event_values;
  } event_data;
};

std::set<pid_t> getProcessChildPids(const std::string &proc_path, pid_t pid);

void setupCounter(struct pcounter &s);

void createCounters(std::map<pid_t, struct pcounter> &counters,
                    const std::set<pid_t> &pids);

void resetAndEnableCounters(const std::map<pid_t, struct pcounter> &counters);

void disableCounters(const std::map<pid_t, struct pcounter> &counters);

void readCounters(std::map<pid_t, struct pcounter> &counters);

void cullCounters(std::map<pid_t, struct pcounter> &counters,
                  const std::set<pid_t> &pids);

void printResults(const uint64_t cycles, const uint64_t instructions);

void getPidDelta(const std::string &proc_path, const pid_t pid,
                 std::map<pid_t, struct pcounter> &MyCounters,
                 std::set<pid_t> &currentPids);
