
#include "gtest/gtest.h"
#include "audio.h"

namespace {

TEST(Audio, FindThisOrNextZeroCrossing) {
	Audio audio("tests/HEADER-4800.wav");

	auto tapeIndex = audio.FindThisOrNextZeroCrossing(9432, 0);

	EXPECT_EQ(tapeIndex, 9440);
}

} // namespace
