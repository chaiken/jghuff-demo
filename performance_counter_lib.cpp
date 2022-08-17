#include "performance_counter_lib.hpp"

#include <cstring>

constexpr uint32_t MIN_COUNTER_READSIZE = 40;
constexpr uint32_t BILLION = 1e9;

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

std::vector<pid_t> getProcessChildPids(const std::string &proc_path,
                                       pid_t pid) {
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
      pids.emplace_back(std::stol(std::regex_replace(
          dir.path().string(), re,
          ""))); // the full value of dir.path().string() looks like
                 // /proc/the_PID/task/some_number. Remove /proc/the_PID/task/
                 // to yield just the number of the child PID, then add it to
                 // our list of the found child(ren)
    }
  }
  return pids;
}

void setupEvent(struct pcounter *s, int &fd, long long unsigned int &id,
                perf_event_attr &st, int gfd) {
  fd = syscall(SYS_perf_event_open, &(st), s->pid, -1, gfd, 0);
  // std::cout << "fd = " << fd << std::endl;
  if (fd > STDERR_FILENO) {
    ioctl(fd, PERF_EVENT_IOC_ID, &(id));
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
  st.read_format =
      PERF_FORMAT_GROUP |
      PERF_FORMAT_ID; // format the result in our all-in-one data struct
}

void setupCounter(struct pcounter *s) {
  auto initArrays = [](auto &arr) {
    for (auto &x : arr) {
      x = 0;
    }
  };
  initArrays(s->gid); // C++ std::arrays are initialized with unknown values by
                      // default. If some events go unusued, nonzero data fools
                      // our code into thinking it's a live event, so we must
                      // initialize all arrays to 0
  initArrays(s->gv);
  initArrays(s->gfd);
  // std::cout << "setting up counters for pid " << s->pid << std::endl;
  errno = 0;
  configureStruct(s->perfstruct[0], PERF_TYPE_HARDWARE,
                  PERF_COUNT_HW_CPU_CYCLES);
  // PERF_COUNT_HW_CPU_CYCLES works on Intel and AMD (and wherever else this
  // event is supported) but could be inaccurate. PERF_COUNT_HW_REF_CPU_CYCLES
  // only works on Intel (unsure? needs more testing) but is more accurate
  // put -1 for the group leader fd because we want to create a  group leader
  setupEvent(s, s->gfd[0], s->gid[0], s->perfstruct[0], -1);
  configureStruct(s->perfstruct[1], PERF_TYPE_HARDWARE,
                  PERF_COUNT_HW_INSTRUCTIONS);
  setupEvent(s, s->gfd[1], s->gid[1], s->perfstruct[1], s->gfd[0]);
}

void createCounters(std::vector<struct pcounter *> &counters,
                    const std::vector<pid_t> &pids) {
  for (const auto &it : pids) {
    counters.push_back(
        new pcounter(it)); // create a new pcounter object; don't use smart
                           // pointers because they dereference to 0
    // std::cout << "creating counter for pid " << counters.back()->pid <<
    // std::endl;
    setupCounter(counters.back());
  }
}

void cullCounters(std::vector<struct pcounter *> &counters,
                  const std::vector<pid_t> &pids) {
  for (const auto culledpid : pids) {
    for (auto &s : counters) {
      if (s->pid == culledpid) {
        for (const auto filedescriptor : s->gfd) {
          if (filedescriptor > STDERR_FILENO) {
            // std::cout << "closing fd " << filedescriptor << std::endl;
            close(filedescriptor); // events, and performance counters as a
                                   // whole, are nothing but file descriptors,
                                   // so we can simply close them to get rid
                                   // of counters
          }
        }
        // std::cout << "culling counter for pid " << s->pid << std::endl;
        counters.erase(std::find(std::begin(counters), std::end(counters), s));
      }
    }
  }
}

void resetAndEnableCounters(const std::vector<struct pcounter *> &counters) {
  for (const auto &s : counters) {
    for (const auto &group : s->gfd) {
      ioctl(group, PERF_EVENT_IOC_RESET,
            PERF_IOC_FLAG_GROUP); // reset the counters for ALL the events that
                                  // are members of the group
      ioctl(group, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
  }
}

void disableCounters(const std::vector<struct pcounter *> &counters) {
  for (const auto &s : counters) {
    for (const auto &group : s->gfd) {
      ioctl(group, PERF_EVENT_IOC_DISABLE,
            PERF_IOC_FLAG_GROUP); // disable all counters in the groups
    }
  }
}

void readCounters(std::vector<struct pcounter *> &counters) {
  long size;
  for (auto &s : counters) {
    if (s->gfd[0] >
        STDERR_FILENO) { // checks if this fd is "good." If we do not check and
                         // it's an unusued file descriptor, then Linux will
                         // deallocate memory for cin instead which leads to
                         // segmentation faults (borrow checkers can't prevent
                         // this because it happens in the kernel)
      size =
          read(s->gfd[0], s->event_data.buf,
               sizeof(s->event_data.buf)); // get information from the counters
      if (size >= MIN_COUNTER_READSIZE) {  // check if there is sufficient data
                                           // to read from. If
        // not, then reading could give us false counter values
        for (int i = 0; i < static_cast<int>(s->event_data.per_event_values.nr);
             i++) { // read data from all the events in the struct pointed to by
                    // data
          if (s->event_data.per_event_values.values[i].id ==
              s->gid[0]) { // data.values[i].id points to an event id, and
                           // we want to match this id to the one belonging
                           // to event 1
            s->gv[0] = s->event_data.per_event_values.values[i]
                           .value; // store the counter value in g1v1
          } else if (s->event_data.per_event_values.values[i].id == s->gid[1]) {
            s->gv[1] = s->event_data.per_event_values.values[i].value;
          }
        }
      }
    }
  }
}

void getPidDelta(const std::string &proc_path, const pid_t pid,
                 std::vector<struct pcounter *> &MyCounters,
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
void printResults(const long long cycles, const long long instructions) {
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
