#pragma once

#include "DataInterface.h"

class DataInterfaceBase : public DataInterface {
    protected:
	AudioPtr audio;
	int bitRate = 0;
	int samplesPerBit = 0;
	int hysterisis = 0;

	bool debugByte = false;
	bool debugBit = false;

    public:
	DataInterfaceBase(AudioPtr audio, int bitRate, int hysterisis) :
		audio(audio),
		hysterisis(hysterisis),
		bitRate(bitRate)
	{
		samplesPerBit = audio->SamplesPerBit(bitRate);
	}

	LeaderResult FindEndOfNextLeaderInternal(TapeIndex tapeIndex) {
		int e6Count = 0;
		TapeIndex initialTapeIndex = tapeIndex;
		while (tapeIndex < audio->SampleCount()) {
			auto readResult = ReadByte(tapeIndex);
			if(readResult.second == 0xe6) {
				e6Count ++;
				tapeIndex = readResult.first;
				continue;
			} else if(readResult.second == 0x01 && e6Count > 4) {
				return { initialTapeIndex, readResult.first, readResult.second };
			}
			// tells our caller to move to the next zero crossing
			return { 0, 0, 0};
		}
		throw AudioEOF("ran out of data");
	}

	// Read and re-sync as needed until we read a series of 0xe6 bytes.
	// Return the tape index and first non-0xe6 byte. This is slow, so
	// make sure to find the carrier before calling this.
	LeaderResult FindEndOfNextLeader(TapeIndex tapeIndex) {
		std::pair<TapeIndex, uint8_t> readResult;

		while (tapeIndex < audio->SampleCount()) {
			auto result = FindEndOfNextLeaderInternal(tapeIndex);
			if (result.sohStart && result.nextIndex && result.value == 0x01) {
				// tapeIndex is the position from which the non-0xe6 byte was read
				// (i.e. the SOH byte start), readResult.first is the next read position
				return result;
			}
			// we didn't find it, so skip to next zero
			// crossing (either direction)
			tapeIndex = audio->FindThisOrNextTransition(tapeIndex + 1, hysterisis);
			Rewind(); // resets last bit we saw to '0'
		};
		throw AudioEOF("ran out of data");
	}
	void SetDebugByte(bool d) { debugByte = d; }
	void SetDebugBit(bool d) { debugBit = d; }
};
