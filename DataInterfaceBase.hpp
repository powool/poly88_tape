#pragma once

#include "DataInterface.h"

class DataInterfaceBase : public DataInterface {
    protected:
	AudioPtr audio;
	int bitRate = 0;
	int samplesPerBit = 0;
	int hysterisis = 0;

    public:
	DataInterfaceBase(AudioPtr audio, int bitRate, int hysterisis) :
		audio(audio),
		hysterisis(hysterisis),
		bitRate(bitRate)
	{
		samplesPerBit = audio->SamplesPerBit(bitRate);
	}

	// Read and re-sync as needed until we read a series of 0xe6 bytes.
	// Return the tape index and first non-0xe6 byte. This is slow, so
	// make sure to find the carrier before calling this.
	std::pair<TapeIndex, uint8_t> FindEndOfNextLeader(TapeIndex tapeIndex, int leaderByteCount) {
		int rewindIndex = tapeIndex;
		std::pair<TapeIndex, uint8_t> readResult;

		// This loop will either throw a TapeEOF, or it
		// will find "leaderByteCount" 0xe6 bytes in a row.
		for(int i = 0; i < leaderByteCount ; i++) {
			readResult = ReadByte(tapeIndex);
			std::cout << std::format("{}: got byte {:02x} that ends at {}", tapeIndex, readResult.second, readResult.first) << std::endl;
			if (readResult.second != 0xe6) {
				tapeIndex = rewindIndex + samplesPerBit/4;
				rewindIndex = tapeIndex;

				// clear polyphase bit state:
				Rewind();
				i = -1;
				continue;
			}
			// next read location
			tapeIndex = readResult.first;
		}

		while(readResult.second == 0xe6) {
			readResult = ReadByte(tapeIndex);
			if (readResult.second != 0xe6) {
				break;
			}
			tapeIndex = readResult.first;
		}

		return readResult;
	}
};
