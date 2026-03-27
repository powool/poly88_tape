#pragma once

#include <cstdint>
#include <vector>
#include "audio.h"
#include <memory>

using TapeIndex = double;

struct ByteReadResult {
	TapeIndex startIndex = 0;
	TapeIndex endIndex = 0;
	uint8_t value = 0;
};

struct BitInfo {
	TapeIndex startIndex = 0;
	TapeIndex endIndex = 0;
	uint8_t value = 0;  // 0 or 1
};

struct BitReadResult {
	TapeIndex startIndex = 0;
	TapeIndex endIndex = 0;
	uint8_t value = 0;
	bool confident = true;
	std::vector<BitInfo> bits;  // per-bit positional data (MSB ordering matches decoder)
};

struct LeaderResult {
	TapeIndex sohStart = 0;   // tape index where the SOH byte begins
	TapeIndex nextIndex = 0;  // tape index after the SOH byte (next read position)
	uint8_t value = 0;        // the first non-0xe6 byte value (should be 0x01 SOH)
};

class DataInterface {
    public:

	// Read and re-sync as needed until we read a series of 0xe6 bytes.
	// Return the SOH byte start position, the next read index, and the byte value.
	virtual LeaderResult FindEndOfNextLeader(TapeIndex tapeIndex) = 0;

	// Read a single byte - the returned TapeIndex is a potentially
	// virtual bit pointer - it may or may not point to an audio
	// signal transition.
	virtual std::pair<TapeIndex, uint8_t> ReadByte(TapeIndex index) = 0;

	// Read a single byte with per-bit positional data.
	// Default implementation wraps ReadByteDetailed with no bit info.
	virtual BitReadResult ReadByteWithBits(TapeIndex index) = 0;

	virtual TapeIndex Rewind() = 0;

	// Allow adjusting of bitrate, hysterisis values, and so on.
	virtual void TweakSettings()  = 0;

	virtual void SetDebugByte(bool d) = 0;
	virtual void SetDebugBit(bool d) = 0;
	virtual double GetBitrateEstimate(TapeIndex tapeIndex, int waveformsToSample) = 0;
};

using DataInterfacePtr = std::shared_ptr<DataInterface>;
