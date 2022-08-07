#include "performance_counter_lib.hpp"

std::vector<pid_t> getProcessChildPids(pid_t pid) {
  std::vector<pid_t> pids;
  std::regex re("/proc/\\d+/task/", std::regex_constants::optimize);
  try {
    for (const auto &dir :
         fs::directory_iterator{"/proc/" + std::to_string(pid) + "/task"}) {
      pids.emplace_back(stol(std::regex_replace(
          dir.path().string(), re,
          ""))); // the full value of dir.path().string() looks like
                 // /proc/the_PID/task/some_number. Remove /proc/the_PID/task/
                 // to yield just the number of the child PID, then add it to
                 // our list of the found child(ren)
    }
  } catch (...) {
    std::cout << "Could not add PID to list"
              << std::endl; // we need better error handling here, but this
                            // works fine for a demo
  }
  return pids;
}

void setupCounter(struct pcounter *s) {
  auto initArrays = [](auto &arr) {
    for (auto &outergroup : arr) {
      for (auto &innerelement : outergroup) {
        innerelement = 0;
      }
    }
  };
  initArrays(s->gid); // C++ std::arrays are initialized with unknown values by
                      // default. If some events go unusued, nonzero data fools
                      // our code into thinking it's a live event, so we must
                      // initialize all arrays to 0
  initArrays(s->gv);
  initArrays(s->gfd);
  auto configureStruct =
      [&](auto &st, const auto perftype,
          const auto config) { // these are common settings for each event.
                               // Chaning a setting here will apply everywhere
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
      };
  auto setupEvent = [&](auto &fd, auto &id, auto &st, auto gfd) {
    fd = syscall(SYS_perf_event_open, &(st), s->pid, -1, gfd, 0);
    // std::cout << "fd = " << fd << std::endl;
    if (fd > 0) {
      ioctl(fd, PERF_EVENT_IOC_ID, &(id));
    } else if (fd == -1) {
      switch (errno) {
      case E2BIG:
        std::cout << "Event perfstruct is too small" << std::endl;
        return;
      case EACCES:
        std::cout
            << "Performance counters not permitted or available; try using a "
               "newer Linux kernel or assigning the CAP_PERFMON capability"
            << std::endl;
        return;
      case EBADF:
        if (gfd > -1) {
          std::cout << "Event group_fd not valid" << std::endl;
        }
        return;
      case EBUSY:
        std::cout
            << "Another process has exclusive access to performance counters"
            << std::endl;
        return;
      case EFAULT:
        std::cout << "Invalid memory address" << std::endl;
        return;
      case EINVAL:
        std::cout << "Invalid event" << std::endl;
        return;
      case EMFILE:
        std::cout << "Not enough file descriptors available" << std::endl;
        return;
      case ENODEV:
        std::cout << "Event not supported on this CPU" << std::endl;
        return;
      case ENOENT:
        std::cout << "Invalid event type" << std::endl;
        return;
      case ENOSPC:
        std::cout << "Too many hardware breakpoint events" << std::endl;
        return;
      case EOPNOTSUPP:
        std::cout << "Hardware support not available" << std::endl;
        return;
      case EPERM:
        std::cout << "Unsupported event exclusion setting" << std::endl;
        return;
      case ESRCH:
        std::cout << "Invalid PID for event; PID = " << s->pid << std::endl;
        return;
      default:
        std::cout << "Other performance counter error; errno = " << errno
                  << std::endl;
        return;
      }
    }
  };
  // std::cout << "setting up counters for pid " << s->pid << std::endl;
  errno = 0;
  configureStruct(s->perfstruct[0][0], PERF_TYPE_HARDWARE,
                  PERF_COUNT_HW_CPU_CYCLES);
  // PERF_COUNT_HW_CPU_CYCLES works on Intel and AMD (and wherever else this
  // event is supported) but could be inaccurate. PERF_COUNT_HW_REF_CPU_CYCLES
  // only works on Intel (unsure? needs more testing) but is more accurate
  setupEvent(s->gfd[0][0], s->gid[0][0], s->perfstruct[0][0],
             -1); // put -1 for the group leader fd because we want to create a
                  // group leader
  configureStruct(s->perfstruct[0][1], PERF_TYPE_HARDWARE,
                  PERF_COUNT_HW_INSTRUCTIONS);
  setupEvent(s->gfd[0][1], s->gid[0][1], s->perfstruct[0][1], s->gfd[0][0]);
}

void createCounters(std::vector<struct pcounter *> &counters,
                    const std::vector<pid_t> &pids) {
  for (const auto &it : pids) {
    counters.emplace_back(
        new pcounter); // create a new pcounter object; don't use smart pointers
                       // because they dereference to 0
    counters.back()->pid = it;
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
        for (const auto group : s->gfd) {
          for (const auto filedescriptor : group) {
            if (filedescriptor >
                2) { // check that we are not closing a built-in file descriptor
                     // for stdout, stdin, or stderr
              // std::cout << "closing fd " << filedescriptor << std::endl;
              close(filedescriptor); // events, and performance counters as a
                                     // whole, are nothing but file descriptors,
                                     // so we can simply close them to get rid
                                     // of counters
            }
          }
        }
        // std::cout << "culling counter for pid " << s->pid << std::endl;
        counters.erase(std::find(begin(counters), end(counters), s));
      }
    }
  }
}

void resetAndEnableCounters(const std::vector<struct pcounter *> &counters) {
  for (const auto &s : counters) {
    for (const auto &group : s->gfd) {
      ioctl(group[0], PERF_EVENT_IOC_RESET,
            PERF_IOC_FLAG_GROUP); // reset the counters for ALL the events that
                                  // are members of the group
      ioctl(group[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
  }
}

void disableCounters(const std::vector<struct pcounter *> &counters) {
  for (const auto &s : counters) {
    for (const auto &group : s->gfd) {
      ioctl(group[0], PERF_EVENT_IOC_DISABLE,
            PERF_IOC_FLAG_GROUP); // disable all counters in the groups
    }
  }
}

void readCounters(std::vector<struct pcounter *> &counters) {
  long size;
  for (auto &s : counters) {
    if (s->gfd[0][0] >
        2) { // checks if this fd is "good." If we do not check and it's an
             // unusued file descriptor, then Linux will deallocate memory for
             // cin instead which leads to segmentation faults (borrow checkers
             // can't prevent this because it happens in the kernel)
      size = read(s->gfd[0][0], s->buf,
                  sizeof(s->buf)); // get information from the counters
      if (size >= 40) { // check if there is sufficient data to read from. If
                        // not, then reading could give us false counter values
        for (int i = 0; i < static_cast<int>(s->data->nr);
             i++) { // read data from all the events in the struct pointed to by
                    // data
          if (s->data->values[i].id ==
              s->gid[0][0]) { // data->values[i].id points to an event id, and
                              // we want to match this id to the one belonging
                              // to event 1
            s->gv[0][0] =
                s->data->values[i].value; // store the counter value in g1v1
          } else if (s->data->values[i].id == s->gid[0][1]) {
            s->gv[0][1] = s->data->values[i].value;
          }
        }
      }
    }
  }
}
