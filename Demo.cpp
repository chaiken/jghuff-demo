#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <regex>
#include <array>

namespace fs = std::filesystem;

struct read_format {
	unsigned long long nr = 0; //how many events there are
	struct {
		unsigned long long value; //the value of event nr
		unsigned long long id; //the id of event nr
	} values[];
};

struct pcounter {
	long pid;

	std::array<std::array<struct perf_event_attr, 1>, 2> perfstruct;
	std::array<std::array<unsigned long long, 1>, 2> gid; //the id stores the event type in a numerical fashion
	std::array<std::array<unsigned long long, 1>, 2> gv; //the values of the events
	std::array<std::array<int, 1>, 2> gfd; //the file descriptors for the counters

	char buf[96]; //buf size equation: (maximum events counted * 16) + 8
	struct read_format* data = reinterpret_cast<struct read_format*>(buf); //in C, this would have been a void*, but C++ lets us do a raw conversion explicitly
};

std::vector<long> getProcessChildPids(long pid) {
	std::vector<long> pids;
	std::regex re("/proc/\\d+/task/", std::regex_constants::optimize);
	try {
		for (const auto& dir : fs::directory_iterator{"/proc/" + std::to_string(pid) + "/task"}) {
			pids.emplace_back(stol(std::regex_replace(dir.path().string(), re, "")));
		}
	} catch(...) {
		std::cout << "Could not add PID to list" << std::endl;
	}
	return pids;
}

void setupCounter(auto& s) {
	auto initArrays = [](auto& arr) {
		for (auto& outergroup : arr) {
			for (auto& innerelement : outergroup) {
				innerelement = 0;
			}
		}
	};
	initArrays(s->gid);
	initArrays(s->gv);
	initArrays(s->gfd);
	auto configureStruct = [&](auto& st, const auto perftype, const auto config) {
		memset(&(st), 0, sizeof(struct perf_event_attr)); //fill the struct with 0s
		st.type = perftype; //the type of event
		st.size = sizeof(struct perf_event_attr);
		st.config = config; //the event we want to measure
		st.disabled = true;
		st.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID; //format the result in our all-in-one data struct
	};
	auto setupEvent = [&](auto& fd, auto& id, auto& st, auto gfd) {
		fd = syscall(__NR_perf_event_open, &(st), s->pid, -1, gfd, 0);
		//std::cout << "fd = " << to_string(fd) << std::endl;
		if (fd > 0) {
			ioctl(fd, PERF_EVENT_IOC_ID, &(id));
		} else if (fd == -1) {
			switch(errno) {
				case E2BIG:
					std::cout << "Event perfstruct is too small" << std::endl;
					return;
				case EACCES:
					std::cout << "Performance counters not permitted or available; try using a newer Linux kernel or assigning the CAP_PERFMON capability" << std::endl;
					return;
				case EBADF:
					if (gfd > -1) {
						std::cout << "Event group_fd not valid" << std::endl;
					}
					return;
				case EBUSY:
					std::cout << "Another process has exclusive access to performance counters" << std::endl;
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
					std::cout << "Invalid PID for event; PID = " + std::to_string(s->pid) << std::endl;
					return;
				default:
					std::cout << "Other performance counter error; errno = " + std::to_string(errno) << std::endl;
					return;
			}
		}
	};
	//std::cout << "setting up counters for pid " << s->pid << std::endl;
	errno = 0;
	configureStruct(s->perfstruct[0][0], PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
    //PERF_COUNT_HW_CPU_CYCLES works on Intel and AMD but could be inaccurate. PERF_COUNT_HW_REF_CPU_CYCLES only works on Intel but is more accurate
	setupEvent(s->gfd[0][0], s->gid[0][0], s->perfstruct[0][0], -1); //put -1 for the group leader fd because we want to create a group leader
	configureStruct(s->perfstruct[0][1], PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
	setupEvent(s->gfd[0][1], s->gid[0][1], s->perfstruct[0][1], s->gfd[0][0]);
}

void createCounters(std::vector<struct pcounter*>& counters, const std::vector<long>& pids) {
	for (const auto& it : pids) {
		counters.emplace_back(new pcounter); //create a new pcounter object; don't use smart pointers because they dereference to 0
		counters.back()->pid = it;
		//std::cout << "creating counter for pid " << counters.back()->pid << std::endl;
		setupCounter(counters.back());
	}
}

void cullCounters(std::vector<struct pcounter*>& counters, const std::vector<long>& pids) {
	for (const auto culledpid : pids) {
		for (auto& s : counters) {
			if (s->pid == culledpid) {
				for (const auto group : s->gfd) {
					for (const auto filedescriptor : group) {
						if (filedescriptor > 2) { //check that we are not closing a built-in file descriptor for stdout, stdin, or stderr
							//std::cout << "closing fd " << filedescriptor << std::endl;
							close(filedescriptor);
						}
					}
				}
				//std::cout << "culling counter for pid " << s->pid << std::endl;
				counters.erase(std::find(begin(counters), end(counters), s));
			}
		}
	}
}

void resetAndEnableCounters(const auto& counters) {
	for (const auto& s : counters) {
		for (const auto& group : s->gfd) {
			ioctl(group[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP); //reset the counters for ALL the events that are members of the group
			ioctl(group[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
		}
	}
}

void disableCounters(const auto& counters) {
	for (const auto& s : counters) {
		for (const auto& group : s->gfd) {
			ioctl(group[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP); //disable all counters in the groups
		}
	}
}

void readCounters(auto& counters) {
	long size;
	for (auto& s : counters) {
		if (s->gfd[0][0] > 2) {
			size = read(s->gfd[0][0], s->buf, sizeof(s->buf)); //get information from the counters
			if (size >= 40) {
				for (int i = 0; i < s->data->nr; i++) { //read data from all the events in the struct pointed to by data
					if (s->data->values[i].id == s->gid[0][0]) { //data->values[i].id points to an event id, and we want to match this id to the one belonging to event 1
						s->gv[0][0] = s->data->values[i].value; //store the counter value in g1v1
					} else if (s->data->values[i].id == s->gid[0][1]) {
						s->gv[0][1] = s->data->values[i].value;
					}
				}
			}
		}
    }
}

int main() {
	//start by cranking up resource limits so we can track programs with many threads
	if (getuid()) { //the root user commonly has different and lower resource limit hard ceilings than nonroot users, so skip this if we are root
		struct rlimit rlimits;
		//std::cout << "Setting resource limits" << std::endl;
		if (getrlimit(RLIMIT_NOFILE, &rlimits) == -1) {
			std::cout << "Error getting resource limits; errno = " << errno << std::endl;
		}
		rlimits.rlim_cur = rlimits.rlim_max; //resize soft limit to max limit; the max limit is a ceiling for the soft limit
		if (setrlimit(RLIMIT_NOFILE, &rlimits) == -1) {
			std::cout << "Error changing resource limits; errno = " << errno << std::endl;
		} 
	}
	
	long pid;
    long long cycles;
    long long instructions;

	std::vector<struct pcounter*> MyCounters = {};
    std::vector<long> newPids = {};
	std::vector<long> diffPids = {};
	std::vector<long> currentPids;

    std::string input;
	std::cout << "Enter a PID " << std::flush;
	std::cin >> input;
	try {
		pid = stol(input);
	} catch(...) {
		std::cout << "Invalid PID" << std::endl;
		return 1;
	}
    
    currentPids = getProcessChildPids(pid);
    createCounters(MyCounters, currentPids);

    while (true) {
        resetAndEnableCounters(MyCounters);
        std::this_thread::sleep_for(std::chrono::seconds(5)); //cross-platform method of sleeping
        disableCounters(MyCounters);
		readCounters(MyCounters);
        cycles = 0;
        instructions = 0;
        for (const auto& s : MyCounters) {
            cycles += s->gv[0][0];
            instructions += s->gv[0][1];
        }

        newPids = getProcessChildPids(pid);
		diffPids.clear();
		std::set_difference(newPids.begin(), newPids.end(), currentPids.begin(), currentPids.end(), std::inserter(diffPids, diffPids.begin())); //calculate what's in newPids that isn't in oldPids
		createCounters(MyCounters, diffPids);
		diffPids.clear();
		std::set_difference(currentPids.begin(), currentPids.end(), newPids.begin(), newPids.end(), std::inserter(diffPids, diffPids.begin())); //calculate what's in oldPids that isn't in newPids
		cullCounters(MyCounters, diffPids);
		currentPids = newPids;
		
        std::cout << "----------------------------------------------------" << std::endl;
        std::cout << "Got " << cycles / 5 << " (" << (float)(cycles / 5) / 1000000000 << " billion) cycles per second" << std::endl;
        std::cout << "Got " << instructions / 5 << " (" << (float)(instructions / 5) / 1000000000 << " billion) instructions per second" << std::endl;
        std::cout << "IPC: " << (float)instructions / (float)cycles << std::endl;
    }
}