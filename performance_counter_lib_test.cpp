#include "performance_counter_lib.hpp"

#include "gtest/gtest.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

using namespace std;

constexpr char TEST_PATH[] = "testdata/";
constexpr int32_t NUMDIRS = 20;
constexpr pid_t FAKE_PID = 1234;

namespace local_testing {

struct PcLibTest : public ::testing::Test {
  void createDirs(pid_t new_pid) {
    pid = new_pid;
    fs::current_path(fs::temp_directory_path());
    test_path = TEST_PATH + to_string(new_pid) + "/task";
    // Clean up mess from any aborted tests.
    std::filesystem::remove_all(test_path);
    ASSERT_TRUE(fs::create_directories(test_path));
    for (int i = 0; i < NUMDIRS; i++) {
      fs::path subpath = test_path;
      subpath /= to_string(i);
      ASSERT_TRUE(fs::create_directory(subpath));
    }
  }
  void createFakeCounters() {
    for (int i = 0; i < NUMDIRS; i++) {
      struct pcounter pc(static_cast<pid_t>(i));
      std::string task_path = test_path.string() + "/" + to_string(i);
      ASSERT_TRUE(fs::exists(task_path));
      std::string afile_path = task_path + "/afile";
      ASSERT_FALSE(fs::exists(afile_path));
      std::string bfile_path = task_path + "/bfile";
      ASSERT_FALSE(fs::exists(bfile_path));
      pc.group_fd[CYCLES] = open(afile_path.c_str(), O_RDWR | O_CREAT, 0744);
      EXPECT_EQ(pc.group_fd[CYCLES], STDERR_FILENO + (2 * i) + 1);
      // The second group_fd is not used by the test, but must created in order
      // to space the first group_fds properly.
      pc.group_fd[INSTRUCTIONS] =
          open(bfile_path.c_str(), O_RDWR | O_CREAT, 0744);
      EXPECT_EQ(pc.group_fd[INSTRUCTIONS], STDERR_FILENO + (2 * i) + 2);
      counters.insert(
          std::pair<pid_t, struct pcounter>{static_cast<pid_t>(i), pc});
    }
  }

  ssize_t tryWriteCounterFds(const int group_leader_fd,
                             const unique_ptr<struct read_format> event_data) {
    // event_data.release() results in a memory leak, as there is no longer a
    // reference to the pointer.
    errno = 0;
    ssize_t written =
        write(group_leader_fd, event_data.get(), sizeof(struct read_format));
    if ((errno) || (written != sizeof(struct read_format))) {
      std::cerr << "Write failed: " << strerror(errno) << std::endl;
      return -1;
    }
    // The following assertion produces an error "void value is not ignored as
    // it should be". ASSERT_EQ(0, syncfs(group_leader_fd)); The actual function
    // signature comes from linux/fs/sync.c
    // /home/alison/gitsrc/googletest/googletest/include/gtest/gtest.h:62
    // already defines _GNU_SOURCE, so that is not the problem.
    // Flush the file to disk.
    syncfs(group_leader_fd);
    return written;
  }

  void writeFakeCounters() {
    uint32_t ctr = 0u;
    for (auto it = counters.begin(); it != counters.end(); it++) {
      unique_ptr<struct read_format> per_event_values(new struct read_format);
      per_event_values->nr = OBSERVED_EVENTS;
      per_event_values->values[CYCLES].id = ctr + 1;
      it->second.event_id[CYCLES] = per_event_values->values[CYCLES].id;
      per_event_values->values[CYCLES].value = ctr + 2;
      per_event_values->values[INSTRUCTIONS].id = ctr + 3;
      per_event_values->values[INSTRUCTIONS].value = ctr + 4;
      it->second.event_id[INSTRUCTIONS] =
          per_event_values->values[INSTRUCTIONS].id;
      ASSERT_EQ(tryWriteCounterFds(it->second.group_fd[CYCLES],
                                   move(per_event_values)),
                sizeof(struct read_format));
      ctr++;
    }
  }

  void SetUp() { createDirs(FAKE_PID); }
  void TearDown() {
    ASSERT_NE(-1, fs::remove_all(TEST_PATH));
    ASSERT_TRUE(!fs::exists(TEST_PATH));
  }
  fs::path test_path;
  pid_t pid = INT_MIN;
  std::map<pid_t, struct pcounter> counters{};
};

TEST(PcLibSimpleTest, setupCounter) {
  struct pcounter acounter(FAKE_PID);
  setupCounter(acounter);
  for (const auto &ps : acounter.perfstruct) {
    EXPECT_EQ(PERF_TYPE_HARDWARE, ps.type);
    EXPECT_EQ(sizeof(struct perf_event_attr), ps.size);
    EXPECT_EQ(true, ps.disabled);
    EXPECT_EQ(PERF_FORMAT_GROUP | PERF_FORMAT_ID, ps.read_format);
  }
  EXPECT_EQ(PERF_COUNT_HW_CPU_CYCLES, acounter.perfstruct[0].config);
  EXPECT_EQ(PERF_COUNT_HW_INSTRUCTIONS, acounter.perfstruct[1].config);
}

TEST_F(PcLibTest, getProcessChildPids) {
  fs::current_path(fs::temp_directory_path());
  ASSERT_TRUE(fs::exists(test_path));

  std::set<pid_t> pids = getProcessChildPids(TEST_PATH, pid);
  ASSERT_EQ(20u, pids.size());
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 0));
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 19));
  EXPECT_EQ(pids.end(), std::find(pids.begin(), pids.end(), 20));

  pids = getProcessChildPids(TEST_PATH, 4321);
  ASSERT_EQ(0u, pids.size());
}

TEST_F(PcLibTest, cullCounter) {
  // Setup
  std::set<pid_t> to_cull;
  for (int i = 0; i < NUMDIRS; i++) {
    to_cull.emplace(static_cast<pid_t>(i * 2));
  }
  createFakeCounters();
  ASSERT_EQ(NUMDIRS, counters.size());

  // Action
  cullCounters(counters, to_cull);

  // Test
  EXPECT_EQ(NUMDIRS / 2U, counters.size());
  // Iterate over original counters array.
  for (int i = 0; i < NUMDIRS; i++) {
    // Should have been culled, so file descriptors are already closed.
    if (to_cull.end() != (std::find(to_cull.begin(), to_cull.end(), i))) {
      errno = 0;
      EXPECT_EQ(-1, close(STDERR_FILENO + (2 * i) + 1));
      EXPECT_EQ(EBADF, errno);

      errno = 0;
      EXPECT_EQ(-1, close(STDERR_FILENO + (2 * i) + 2));
      EXPECT_EQ(EBADF, errno);
    } else {
      // Should not have been culled so file descriptors should still be open.
      errno = 0;
      EXPECT_EQ(0, close(STDERR_FILENO + (2 * i) + 1));
      EXPECT_EQ(0, errno);
      errno = 0;
      EXPECT_EQ(0, close(STDERR_FILENO + (2 * i) + 2));
      EXPECT_EQ(0, errno);
    }
  }
}

TEST_F(PcLibTest, readCounters) {
  createFakeCounters();
  ASSERT_EQ(NUMDIRS, counters.size());
  writeFakeCounters();
  int ctr = 0;
  for (auto it = counters.begin(); it != counters.end(); it++) {
    ASSERT_EQ(ctr + STDERR_FILENO + 1, it->second.group_fd[0]);
    ASSERT_EQ(ctr + STDERR_FILENO + 2, it->second.group_fd[1]);
    errno = 0;
    std::unique_ptr<struct stat> cycles_buf(new struct stat);
    EXPECT_EQ(0, fstat(it->second.group_fd[CYCLES], cycles_buf.get()));
    EXPECT_EQ(sizeof(struct read_format), cycles_buf->st_size);
    std::unique_ptr<struct stat> instructions_buf(new struct stat);
    EXPECT_EQ(0,
              fstat(it->second.group_fd[INSTRUCTIONS], instructions_buf.get()));
    EXPECT_EQ(0, fstat(it->second.group_fd[CYCLES], instructions_buf.get()));
    EXPECT_EQ(sizeof(struct read_format), instructions_buf->st_size);

    // The write() syscall that populates the file data leaves the  file offset
    // at the end, with the result that read() syscall in readIt->Seconds()
    // reports that the file is empty.
    ASSERT_EQ(0u, lseek(it->second.group_fd[0], 0u, SEEK_SET));
    ASSERT_EQ(0u, lseek(it->second.group_fd[1], 0u, SEEK_SET));

    ctr += 2;
  }
  readCounters(counters);

  int idx = 0;
  for (auto it = counters.begin(); it != counters.end(); it++) {
    // First element of the map element is the key = PID.
    EXPECT_EQ(idx, it->first);
    EXPECT_EQ(OBSERVED_EVENTS, it->second.event_data.per_event_values.nr);
    EXPECT_EQ(idx + 1,
              it->second.event_data.per_event_values.values[CYCLES].id);
    EXPECT_EQ(it->second.event_id[CYCLES],
              it->second.event_data.per_event_values.values[CYCLES].id);
    EXPECT_EQ(idx + 2,
              it->second.event_data.per_event_values.values[CYCLES].value);
    EXPECT_EQ(idx + 3,
              it->second.event_data.per_event_values.values[INSTRUCTIONS].id);
    EXPECT_EQ(it->second.event_id[INSTRUCTIONS],
              it->second.event_data.per_event_values.values[INSTRUCTIONS].id);
    EXPECT_EQ(
        idx + 4,
        it->second.event_data.per_event_values.values[INSTRUCTIONS].value);

    errno = 0;
    EXPECT_EQ(0, close(it->second.group_fd[CYCLES]));
    EXPECT_EQ(0, close(it->second.group_fd[INSTRUCTIONS]));
    idx++;
  }
}

TEST_F(PcLibTest, getPidDelta) {
  createFakeCounters();
  ASSERT_EQ(NUMDIRS, counters.size());
  std::set<pid_t> pids = getProcessChildPids(TEST_PATH, FAKE_PID);
  ASSERT_EQ(NUMDIRS, pids.size());

  // Remove tasks with even-numbered PIDs.
  for (const std::filesystem::directory_entry &dir_entry :
       std::filesystem::directory_iterator(test_path)) {
    std::string task_name{dir_entry.path().filename()};
    // Make sure that the PID has only appropriate characters.
    // strtoul() returns 0 when presented with alphabetic characters.
    ASSERT_TRUE(std::all_of(task_name.begin(), task_name.end(),
                            [](char c) { return isdigit(c); }));
    errno = 0;
    uint64_t numeric_name = strtoul(basename(task_name.c_str()), nullptr, 10);
    if (ULONG_MAX == static_cast<unsigned long>(numeric_name)) {
      if (errno) {
        perror(strerror(errno));
      }
      std::cerr << "Bad directory name " << dir_entry.path() << std::endl;
    }
    // Remove directories corresponding to odd-numbered PIDs.
    if (1 == (numeric_name % 2)) {
      // Remove the task's top-level directory and the two files.
      EXPECT_EQ(3, std::filesystem::remove_all(dir_entry.path()));
    }
  }
  // Check that the fake procfs is correct.
  std::set<pid_t> new_pids = getProcessChildPids(TEST_PATH, FAKE_PID);
  ASSERT_EQ(pids.size() / 2u, new_pids.size());
  getPidDelta(TEST_PATH, FAKE_PID, counters, pids);
  ASSERT_EQ(NUMDIRS / 2u, counters.size());
}

} // namespace local_testing
