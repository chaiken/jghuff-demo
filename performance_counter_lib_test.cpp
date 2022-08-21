#include "performance_counter_lib.hpp"

#include "gtest/gtest.h"

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
  void TearDown() { ASSERT_NE(-1, fs::remove_all(TEST_PATH)); }
  fs::path test_path;
  pid_t pid = INT_MIN;
};

TEST_F(PcLibTest, getProcessChildPids) {
  createDirs(1234);
  fs::current_path(fs::temp_directory_path());
  ASSERT_TRUE(fs::exists(test_path));

  std::vector pids = getProcessChildPids(TEST_PATH, pid);
  ASSERT_EQ(20u, pids.size());
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 0));
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 19));
  EXPECT_EQ(pids.end(), std::find(pids.begin(), pids.end(), 20));

  pids = getProcessChildPids("testdata/", 4321);
  ASSERT_EQ(0u, pids.size());
}

} // namespace local_testing
