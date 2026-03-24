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
	bool confident = true;  // false if sync was lost or bits were ambiguous
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

class DataInterface {
    public:

	// Read and re-sync as needed until we read a series of 0xe6 bytes.
	// Return the tape index and first non-0xe6 byte.
	virtual std::pair<TapeIndex, uint8_t> FindEndOfNextLeader(TapeIndex tapeIndex, int leaderByteCount) = 0;

	// Read a single byte - the returned TapeIndex is a potentially
	// virtual bit pointer - it may or may not point to an audio
	// signal transition.
	virtual std::pair<TapeIndex, uint8_t> ReadByte(TapeIndex index) = 0;

	// Read a single byte with detailed positional and confidence info.
	// Default implementation wraps ReadByte.
	virtual ByteReadResult ReadByteDetailed(TapeIndex index) {
		auto result = ReadByte(index);
		return { index, result.first, result.second, true };
	}

	// Read a single byte with per-bit positional data.
	// Default implementation wraps ReadByteDetailed with no bit info.
	virtual BitReadResult ReadByteWithBits(TapeIndex index) {
		auto r = ReadByteDetailed(index);
		return { r.startIndex, r.endIndex, r.value, r.confident, {} };
	}

	virtual TapeIndex Rewind() = 0;

	// Allow adjusting of bitrate, hysterisis values, and so on.
	virtual void TweakSettings()  = 0;

	virtual void SetDebugByte(bool d) = 0;
	virtual void SetDebugBit(bool d) = 0;
};

using DataInterfacePtr = std::shared_ptr<DataInterface>;
