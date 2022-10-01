#include "performance_counter_lib.hpp"

#include <cstring>

constexpr uint32_t BILLION = 1e9;

namespace {
std::pair<bool, uint64_t> safe_strtoul(const std::string &str) {
  std::pair<bool, uint64_t> retval;
  errno = 0;
  retval.second = strtoul(str.c_str(), NULL, 10);
  if (errno) {
    retval.first = false;
    return retval;
  }
  retval.first = true;
  return retval;
}
} // namespace

std::string lookupErrorMessage(const int errnum) {
  switch (errnum) {
  case E2BIG:
    return "Event perfstruct is too small";
  case EACCES:
    return "Performance counters not permitted or available; try using a "
           "newer Linux kernel or assigning the CAP_PERFMON capability";
  case EBADF:
    return "Event group_fd not valid";
  case EBUSY:
    return "Another process has exclusive access to performance counters";
  case EFAULT:
    return "Invalid memory address";
  case EINVAL:
    return "Invalid event";
  case EMFILE:
    return "Not enough file descriptors available";
  case ENODEV:
    return "Event not supported on this CPU";
  case ENOENT:
    return "Invalid event type";
  case ENOSPC:
    return "Too many hardware breakpoint events";
  case EOPNOTSUPP:
    return "Hardware support not available";
  case EPERM:
    return "Unsupported event exclusion setting";
  case ESRCH:
    return "Invalid PID for event";
  default:
    return "Other performance counter error; errno = " + std::to_string(errnum);
  }
}

bool operator==(const struct pcounter &a, const struct pcounter &b) {
  return (a.pid == b.pid);
}

std::vector<pid_t> getProcessChildPids(const std::string &proc_path,
                                       const pid_t pid) {
  std::vector<pid_t> pids{};
  const fs::path task_path{proc_path + std::to_string(pid) + "/task"};
  if (!fs::exists(task_path)) {
    std::cout << "No such PID " << pid
              << std::endl; // we need better error handling here, but this
                            // works fine for a demo
  } else {
    std::regex re(proc_path + "\\d+/task/", std::regex_constants::optimize);
    for (const auto &dir :
         fs::directory_iterator{proc_path + std::to_string(pid) + "/task"}) {
      // the full value of dir.path().string() looks like
      // /proc/the_PID/task/some_number. Remove /proc/the_PID/task/ to yield
      // just the number of the child PID, then add it to our list of the found
      // child(ren)
      std::string pidstr = std::regex_replace(dir.path().string(), re, "");
      std::pair<bool, uint64_t> res = safe_strtoul(pidstr);
      if (!res.first) {
        std::cerr << "Failed numeric conversion of " << dir.path().string()
                  << " with " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
      }
      pids.emplace_back(res.second);
    }
  }
  return pids;
}

// The only user of the second event group_fd is the ioctl that associates the
// second event with the group created when the first event was enabled.
void setupEvent(struct pcounter &s, uint32_t event_num, int group_fd) {
  // pid > 0 and cpu == -1 measures the specified process/thread on any CPU.
  s.group_fd[event_num] = syscall(SYS_perf_event_open, &s.perfstruct[event_num],
                                  s.pid, -1, group_fd, 0);
  // std::cout << "fd = " << fd << std::endl;
  if (s.group_fd[event_num] > STDERR_FILENO) {
    //  PERF_EVENT_IOC_ID returns the event ID value for the given event file
    //  descriptor.
    // The argument is a pointer to a 64-bit unsigned integer to hold the
    // result.
    ioctl(s.group_fd[event_num], PERF_EVENT_IOC_ID, &s.event_id[event_num]);
  } else {
    std::cout << lookupErrorMessage(errno) << std::endl;
  }
}

// these are common settings for each event.
// Changing a setting here will apply everywhere
void configureStruct(struct perf_event_attr &st, const perf_type_id perftype,
                     const perf_hw_id config) {
  memset(&(st), 0,
         sizeof(struct perf_event_attr)); // fill the struct with 0s
  st.type = perftype;                     // the type of event
  st.size = sizeof(struct perf_event_attr);
  st.config = config; // the event we want to measure
  st.disabled = true; // start disabled by default to not count, and skip
                      // extra syscalls to disable upon creation
  /* Specifies the format of the data returned by read(2) on a perf_event_open()
     file descriptor. PERF_FORMAT_ID Adds a 64-bit unique value that corresponds
     to the  event group. PERF_FORMAT_GROUP sets all  counter  values in an
     event group to be read with one read(). */
  st.read_format =
      PERF_FORMAT_GROUP |
      PERF_FORMAT_ID; // format the result in our all-in-one data struct
}

void setupCounter(struct pcounter &s) {
  // std::cout << "setting up counters for pid " << s.pid << std::endl;
  errno = 0;
  // PERF_COUNT_HW_CPU_CYCLES works on Intel and AMD (and wherever else this
  // event is supported) but could be inaccurate. PERF_COUNT_HW_REF_CPU_CYCLES
  // only works on Intel (unsure? needs more testing) but is more accurate
  configureStruct(s.perfstruct[CYCLES], PERF_TYPE_HARDWARE,
                  PERF_COUNT_HW_CPU_CYCLES);
  // Create the event group with s.group_fd[CYCLES] and associate the CYCLES
  // event_id with it.
  setupEvent(s, CYCLES, -1);
  configureStruct(s.perfstruct[INSTRUCTIONS], PERF_TYPE_HARDWARE,
                  PERF_COUNT_HW_INSTRUCTIONS);
  // Associate the event_id for the INSTRUCTIONS event with the group_fd created
  // for the CYCLES event.
  setupEvent(s, INSTRUCTIONS, s.group_fd[CYCLES]);
}

void createCounters(std::vector<struct pcounter> &counters,
                    const std::vector<pid_t> &pids) {
  for (const auto &pid : pids) {
    counters.push_back(pcounter(pid));
    // std::cout << "creating counter for pid " << counters.back()->pid <<
    // std::endl;
    setupCounter(counters.back());
  }
}

void cullCounters(std::vector<struct pcounter> &counters,
                  const std::vector<pid_t> &pids) {
  for (const auto culledpid : pids) {
    for (auto &counter : counters) {
      if (counter.pid == culledpid) {
        for (const auto filedescriptor : counter.group_fd) {
          if (filedescriptor > STDERR_FILENO) {
            // std::cout << "closing fd " << filedescriptor << std::endl;
            // events, and performance counters as a  whole, are nothing but
            // file descriptors,  so we can simply close them to get rid of
            // counters
            errno = 0;
            int res = close(filedescriptor);
            if (res) {
              std::cerr << "Error closing fd " << filedescriptor << " "
                        << strerror(errno) << std::endl;
            }
          }
        }
        // std::cout << "culling counter for pid " << counter.pid << std::endl;
        counters.erase(
            std::find(std::begin(counters), std::end(counters), counter));
      }
    }
  }
}

void resetAndEnableCounters(const std::vector<struct pcounter> &counters) {
  for (const auto &counter : counters) {
    for (const auto &group : counter.group_fd) {
      // reset the counters for ALL the events that are members of the group
      ioctl(group, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
      // enable all the events that are members of the group
      ioctl(group, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
  }
}

void disableCounters(const std::vector<struct pcounter> &counters) {
  for (const auto &counter : counters) {
    for (const auto &group : counter.group_fd) {
      // disable all counters in the groups
      ioctl(group, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    }
  }
}

void readCounters(std::vector<struct pcounter> &counters) {
  for (auto &counter : counters) {
    // checks if this fd is "good." If  it's an unused file descriptor, then
    // Linux will deallocate memory for cin instead which leads to segmentation
    // faults (borrow checkers can't prevent  this because it happens in the
    // kernel)
    if (counter.group_fd[CYCLES] > STDERR_FILENO) {
      errno = 0;
      // The second event's counts are available via the group to which it and
      // the first event belong.   It's not obvious that a read to the second
      // event's group_fd will even succeeed.
      ssize_t size = read(counter.group_fd[CYCLES], counter.event_data.buf,
                          sizeof(counter.event_data.buf));
      //  If false, reading could give us false counter values.
      if (size == COUNTER_READSIZE) {
        for (int i = 0;
             i < static_cast<int>(counter.event_data.per_event_values.nr);
             i++) {
          //  Match the cycle count for an event with its instruction count.
          if (counter.event_data.per_event_values.values[i].id ==
              counter.event_id[CYCLES]) {
            counter.event_value[CYCLES] =
                counter.event_data.per_event_values.values[i].value;
          } else if (counter.event_data.per_event_values.values[i].id ==
                     counter.event_id[INSTRUCTIONS]) {
            counter.event_value[INSTRUCTIONS] =
                counter.event_data.per_event_values.values[i].value;
          }
        }
      } else {
        if (errno) {
          std::cerr << strerror(errno) << " " << counter.group_fd[CYCLES]
                    << std::endl;
        } else {
          std::cerr << "Insufficient data " << size << " bytes for group "
                    << counter.group_fd[CYCLES] << std::endl;
        }
      }
    } else {
      std::cerr << "Bad file descriptor for task " << counter.pid << std::endl;
    }
  }
}

void getPidDelta(const std::string &proc_path, const pid_t pid,
                 std::vector<struct pcounter> &MyCounters,
                 std::vector<pid_t> &currentPids) {
  std::vector<pid_t> diffPids{};

  // calculate the PID delta
  std::vector<pid_t> newPids = getProcessChildPids(proc_path, pid);
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
  currentPids = std::move(newPids);
}

// the frontend
// in real programs, this section should be in the calling thread
// we only access our frontend variables here, as everything having to do
// with counters is abstracted away elsewhere
void printResults(const uint64_t cycles, const uint64_t instructions) {
  if (!cycles) {
    return;
  }
  std::cout << "----------------------------------------------------"
            << std::endl;
  std::cout << "Got " << cycles / SLEEPCOUNT << " ("
            << (float)(cycles / SLEEPCOUNT) / BILLION
            << " billion) cycles per second"
            << std::endl; // divide our data variables by the sleep time to
                          // get per-second measurements (you could make this
                          // a constexpr variable or a macro)
  std::cout << "Got " << instructions / SLEEPCOUNT << " ("
            << (float)(instructions / SLEEPCOUNT) / BILLION
            << " billion) instructions per second" << std::endl;
  std::cout << "IPC: " << (float)instructions / (float)cycles
            << std::endl; // footgun: never forget to convert to float (or
                          // double) when dividing to get a result with decimals
}
