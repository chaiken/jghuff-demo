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

void close_counter_fds(const std::array<int, 2u> &fds) {
  for (const int fd : fds) {
    if (fd > STDERR_FILENO) {
      // std::cout << "closing fd " << filedescriptor << std::endl;
      // events, and performance counters as a  whole, are nothing but
      // file descriptors,  so we can simply close them to get rid of
      // counters
      errno = 0;
      int res = close(fd);
      if (res) {
        std::cerr << "Error closing fd " << fd << " " << strerror(errno)
                  << std::endl;
      }
    }
  }
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

std::set<pid_t> getProcessChildPids(const std::string &proc_path,
                                    const pid_t pid) {
  std::set<pid_t> pids{};
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
      pids.emplace(res.second);
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

void createCounters(std::map<pid_t, struct pcounter> &counters,
                    const std::set<pid_t> &pids) {
  for (const auto &pid : pids) {
    struct pcounter newpc(pid);
    setupCounter(newpc);
    counters.insert(std::pair<pid_t, struct pcounter>{pid, newpc});
    // std::cout << "creating counter for pid " << counters.back()->pid <<
    // std::endl;
  }
}

void cullCounters(std::map<pid_t, struct pcounter> &counters,
                  const std::set<pid_t> &pids) {
  for (const auto culledpid : pids) {
    for (auto it = counters.begin(); it != counters.end(); it++) {
      // First element is the key, which is the PID.
      if (it->first == culledpid) {
        // Second element is the pcounter associated with the PID.
        close_counter_fds(it->second.group_fd);
        // std::cout << "culling counter for pid " << counter.pid << std::endl;
        counters.erase(it);
        // A given PID can occur only once in a std::map or std::set, so exit
        // the loop.
        break;
      }
    }
  }
}

void resetAndEnableCounters(const std::map<pid_t, struct pcounter> &counters) {
  for (auto &counter : counters) {
    // Second element is the pcounter associated with the PID.
    for (const auto &group : counter.second.group_fd) {
      // reset the counters for ALL the events that are members of the group
      ioctl(group, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
      // enable all the events that are members of the group
      ioctl(group, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
  }
}

void disableCounters(const std::map<pid_t, struct pcounter> &counters) {
  for (auto &counter : counters) {
    // Second element is the pcounter associated with the PID.
    for (const auto &group : counter.second.group_fd) {
      // disable all counters in the groups
      ioctl(group, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    }
  }
}

void readCounters(std::map<pid_t, struct pcounter> &counters) {
  for (auto &counter : counters) {
    // checks if this fd is "good." If  it's an unused file descriptor, then
    // Linux will deallocate memory for cin instead which leads to segmentation
    // faults (borrow checkers can't prevent  this because it happens in the
    // kernel)
    if (counter.second.group_fd[CYCLES] > STDERR_FILENO) {
      errno = 0;
      // The second event's counts are available via the group to which it and
      // the first event belong.   It's not obvious that a read to the second
      // event's group_fd will even succeeed.
      ssize_t size =
          read(counter.second.group_fd[CYCLES], counter.second.event_data.buf,
               sizeof(counter.second.event_data.buf));
      //  If false, reading could give us false counter values.
      if (size == COUNTER_READSIZE) {
        for (int i = 0; i < static_cast<int>(
                                counter.second.event_data.per_event_values.nr);
             i++) {
          //  Match the cycle count for an event with its instruction count.
          if (counter.second.event_data.per_event_values.values[i].id ==
              counter.second.event_id[CYCLES]) {
            counter.second.event_value[CYCLES] =
                counter.second.event_data.per_event_values.values[i].value;
          } else if (counter.second.event_data.per_event_values.values[i].id ==
                     counter.second.event_id[INSTRUCTIONS]) {
            counter.second.event_value[INSTRUCTIONS] =
                counter.second.event_data.per_event_values.values[i].value;
          }
        }
      } else {
        if (errno) {
          std::cerr << strerror(errno) << " " << counter.second.group_fd[CYCLES]
                    << std::endl;
        } else {
          std::cerr << "Insufficient data " << size << " bytes for group "
                    << counter.second.group_fd[CYCLES] << std::endl;
        }
      }
    } else {
      std::cerr << "Bad file descriptor for task " << counter.second.pid
                << std::endl;
    }
  }
}

// Why not simply create a new map container for the new task list and ignore
// the exited tasks? Two reasons:
// clang-format off
// 0. The list of exited tasks is needed to close their file descriptors.
// 1. Creating new counters involves a fair amount of overhead, so destroying
//    and then recreating counters for ongoing tasks would be wasteful.
// clang-format on
void getPidDelta(const std::string &proc_path, const pid_t pid,
                 std::map<pid_t, struct pcounter> &MyCounters,
                 std::set<pid_t> &currentPids) {
  std::set<pid_t> diffPids{};

  // Reread the child tasks of the provided parent PID from procfs.
  std::set<pid_t> newPids = getProcessChildPids(proc_path, pid);

  // Find PIDs of newly running tasks.
  std::set_difference(newPids.begin(), newPids.end(), currentPids.begin(),
                      currentPids.end(),
                      std::inserter(diffPids, diffPids.begin()));
  // Create new counters for tasks which started since last iteration.
  createCounters(MyCounters, diffPids);
  diffPids.clear();

  // Find PIDs of tasks which exited since last iteration.
  std::set_difference(currentPids.begin(), currentPids.end(), newPids.begin(),
                      newPids.end(), std::inserter(diffPids, diffPids.begin()));

  // Close file descriptors associated with tasks that have exited and remove
  // their counters from the map.
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
