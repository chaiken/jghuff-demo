#include "performance_counter_lib.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace local_testing {

TEST(PclibTest, getProcessChildPids) {
  std::vector pids = getProcessChildPids("testdata/", 1234);
  ASSERT_EQ(20u, pids.size());
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 0));
  EXPECT_NE(pids.end(), std::find(pids.begin(), pids.end(), 19));
  EXPECT_EQ(pids.end(), std::find(pids.begin(), pids.end(), 20));

  pids = getProcessChildPids("testdata/", 4321);
  ASSERT_EQ(0u, pids.size());
}

} // namespace local_testing
