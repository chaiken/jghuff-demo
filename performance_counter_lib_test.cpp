#include "performance_counter_lib.hpp"

#include "gtest/gtest.h"

#include <fcntl.h>
#include <limits.h>

using namespace std;

constexpr char TEST_PATH[] = "testdata/";
constexpr int32_t NUMDIRS = 20;

namespace local_testing {

struct PcLibTest : public ::testing::Test {
  void createDirs(pid_t new_pid) {
    pid = new_pid;
    fs::current_path(fs::temp_directory_path());
    test_path = TEST_PATH + to_string(new_pid) + "/task";
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
      pc.group_fd[0] = creat(afile_path.c_str(), 0644);
      EXPECT_EQ(pc.group_fd[0], STDERR_FILENO + (2 * i) + 1);
      pc.group_fd[1] = creat(bfile_path.c_str(), 0644);
      EXPECT_EQ(pc.group_fd[1], STDERR_FILENO + (2 * i) + 2);
      counters.push_back(pc);
    }
  }
  void SetUp() { createDirs(1234); }
  void TearDown() {
    ASSERT_NE(-1, fs::remove_all(TEST_PATH));
    ASSERT_TRUE(!fs::exists(TEST_PATH));
  }
  fs::path test_path;
  pid_t pid = INT_MIN;
  std::vector<struct pcounter> counters{};
};

TEST_F(PcLibTest, getProcessChildPids) {
  fs::current_path(fs::temp_directory_path());
  ASSERT_TRUE(fs::exists(test_path));

  std::vector<pid_t> pids = getProcessChildPids(TEST_PATH, pid);
  ASSERT_EQ(20u, pids.size());
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 0));
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 19));
  EXPECT_EQ(pids.end(), std::find(pids.begin(), pids.end(), 20));

  pids = getProcessChildPids("testdata/", 4321);
  ASSERT_EQ(0u, pids.size());
}

TEST_F(PcLibTest, cullCounter) {
  // Setup
  std::vector<pid_t> to_cull;
  for (int i = 0; i < NUMDIRS; i++) {
    to_cull.push_back(static_cast<pid_t>(i * 2));
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

} // namespace local_testing
