
#include "gtest/gtest.h"
#include "audio.h"
#include "PolyPhase.hpp"

namespace {

TEST(PolyPhase, ReadByte) {
	AudioPtr audio = std::make_shared<Audio>("tests/HEADER-4800.wav");

	audio->SetInvertPhase(true);

	PolyPhase polyPhase(audio, 4800, 200);

	auto result1 = polyPhase.ReadByte(298683);

	EXPECT_EQ(result1.second, 0xE6);

	auto result2 = polyPhase.ReadByte(result1.first);

	EXPECT_EQ(result2.second, 0xE6);

	auto result3 = polyPhase.ReadByte(result2.first);

	EXPECT_EQ(result3.second, 0xE6);
}

TEST(PolyPhase, ReadByteWithBits) {
	AudioPtr audio = std::make_shared<Audio>("tests/HEADER-4800.wav");

	PolyPhase polyPhase(audio, 4800, 200);

	audio->SetInvertPhase(true);

	auto result1 = polyPhase.ReadByteWithBits(298683);

	EXPECT_EQ(result1.value, 0xE6);
	EXPECT_EQ(result1.bits[2].startIndex, 298763);
	EXPECT_EQ(result1.bits[6].startIndex, 298920);

	auto result2 = polyPhase.ReadByteWithBits(result1.endIndex);
	EXPECT_TRUE(std::abs(result2.bits[2].startIndex - 299083) < 1);
	EXPECT_TRUE(std::abs(result2.bits[6].startIndex - 299245) < 1);

	EXPECT_EQ(result2.value, 0xE6);

	auto result3 = polyPhase.ReadByteWithBits(result2.endIndex);
	EXPECT_TRUE(std::abs(result3.bits[2].startIndex - 299407) < 1);
	EXPECT_TRUE(std::abs(result3.bits[6].startIndex - 299572) < 1);

	EXPECT_EQ(result3.value, 0xE6);
}

} // namespace
