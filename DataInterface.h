#pragma once

#include <cstdint>
#include "audio.h"
#include <memory>

using TapeIndex = double;

class DataInterface {
    public:

	// Read and re-sync as needed until we read a series of 0xe6 bytes.
	// Return the tape index and first non-0xe6 byte.
	virtual std::pair<TapeIndex, uint8_t> FindEndOfNextLeader(TapeIndex tapeIndex, int leaderByteCount) = 0;

	// Read a single byte - the returned TapeIndex is a potentially
	// virtual bit pointer - it may or may not point to an audio
	// signal transition.
	virtual std::pair<TapeIndex, uint8_t> ReadByte(TapeIndex index) = 0;

	virtual void Rewind() = 0;

	// Allow adjusting of bitrate, hysterisis values, and so on.
	virtual void TweakSettings()  = 0;
};

using DataInterfacePtr = std::shared_ptr<DataInterface>;
