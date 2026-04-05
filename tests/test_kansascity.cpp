#include <format>

#include "gtest/gtest.h"
#include "audio.h"
#include "KansasCity.hpp"
#include "Record.hpp"

namespace {

TEST(KansasCity, ReadByte) {
	AudioPtr audio = std::make_shared<Audio>("tests/LUNAR_BYTE_1ST_RECORD.wav");

	KansasCity kansasCity(audio, 300, 0);

	auto result1 = kansasCity.ReadByte(4680476);

	EXPECT_EQ(result1.second, 0xE6);

	auto result2 = kansasCity.ReadByte(result1.first);

	EXPECT_EQ(result2.second, 0xE6);

	auto result3 = kansasCity.ReadByte(result2.first);

	EXPECT_EQ(result3.second, 0xE6);
}

TEST(KansasCity, ReadByteWithBits) {
	AudioPtr audio = std::make_shared<Audio>("tests/LUNAR_BYTE_1ST_RECORD.wav");

	KansasCity kansasCity(audio, 300, 0);

	auto result1 = kansasCity.ReadByteWithBits(4680476);

	EXPECT_EQ(result1.value, 0xE6);
	EXPECT_EQ(result1.bits[2].startIndex, 4681765);
	EXPECT_EQ(result1.bits[6].startIndex, 4684364);

	auto result2 = kansasCity.ReadByteWithBits(result1.endIndex);
	EXPECT_EQ(result2.bits[2].startIndex, 4688901);
	EXPECT_EQ(result2.bits[6].startIndex, 4691481);

	EXPECT_EQ(result2.value, 0xE6);

	auto result3 = kansasCity.ReadByteWithBits(result2.endIndex);
	EXPECT_EQ(result3.bits[2].startIndex, 4696024);
	EXPECT_EQ(result3.bits[6].startIndex, 4698636);

	EXPECT_EQ(result3.value, 0xE6);
}

TEST(KansasCity, ReadRecord) {
	AudioPtr audio = std::make_shared<Audio>("tests/LUNAR_BYTE_1ST_RECORD.wav");

	DataInterfacePtr dataInterface = std::make_shared<KansasCity>(audio, 300, 0);

	Record record;

	record.ReadFromDecoder(dataInterface, 6973181);

	EXPECT_TRUE(record.HeaderChecksumIsValid());
	EXPECT_TRUE(record.DataChecksumIsValid());

	std::cout << record.GetHeaderHexDump() << std::endl;
	std::cout << record.GetHexDump() << std::endl;

}

TEST(KansasCity, ReadRecordFromOffset) {
	AudioPtr audio = std::make_shared<Audio>("tests/LUNAR_BYTE_1ST_RECORD.wav");

	DataInterfacePtr dataInterface = std::make_shared<KansasCity>(audio, 300, 0);

	Record record;

	record.ReadFromDecoder(dataInterface, 6973220);

	EXPECT_TRUE(record.HeaderChecksumIsValid());
	EXPECT_TRUE(record.DataChecksumIsValid());

//	auto dump = record.GetHexDump();

//	std::cout << dump;
}

} // namespace
