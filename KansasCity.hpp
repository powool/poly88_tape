#pragma once

#include "audio.h"
#include "DataInterfaceBase.hpp"

class KansasCity : public DataInterfaceBase {
	bool debug = false;

	// samples per second / cycles per second => samples per full wave cycle:
	int fullWave2400HZSampleCount;
	int fullWave1200HZSampleCount;

	// Decode 300 baud byte format data, which is a two tone encoding (AKA
	// frequency shift key - FSK), where 1200HZ represents a 0, and 2400HZ
	// represents a 1.
	//
	// To my knowledge, 300 bits per second is the only speed byte encoded tape I have.
	//
	// Attempt to count accurately by measuring wavelength between
	// each full wave.
	std::pair<TapeIndex, int> ReadBit(TapeIndex index) {

		// ensure we are at a zero crossing
		index = audio->FindThisOrNextZeroCrossing(index, hysterisis);

		auto startIndex = index;

		int cycles2400 = 0;
		int cycles1200 = 0;
		while (index < startIndex + samplesPerBit) {
			auto nextRiseIndex = audio->FindThisOrNextZeroCrossing(index + 1, hysterisis);
			if (std::abs((nextRiseIndex - index) - fullWave2400HZSampleCount) < 3) cycles2400++;
			if (std::abs((nextRiseIndex - index) - fullWave1200HZSampleCount) < 3) cycles1200++;
			index = nextRiseIndex;
		}

		// 2 means undetermined
		// allow mixed counts
		int resultBit = 2;
#if 1
		if ((cycles2400 == 7 || cycles2400 == 8 || cycles2400 == 9) &&
			(cycles1200 == 0 || cycles1200 == 1)) {
			resultBit = 1;
		} else if ((cycles2400 == 0 || cycles2400 == 1) &&
			(cycles1200 == 4 || cycles1200 == 3)) {
			resultBit = 0;
		};
#else
		if ((cycles2400 == 7 || cycles2400 == 8 || cycles2400 == 9) ) {
			resultBit = 1;
		} else if ((cycles2400 == 0 || cycles2400 == 1) ) {
			resultBit = 0;
		};
#endif
#if 0
		std::cout << initialIndex << ", " << audio->TimeOffset(initialIndex) << "s: fullWaveCount = " << fullWaveCount << " lost sync" << std::endl;
#endif
#if 0
		std::cout << index << ", " << audio->TimeOffset(index) << "s: start reading bit" << std::endl;
#endif
		return std::make_pair(audio->FindThisOrNextZeroCrossing(index), resultBit);
	}

    public:
	KansasCity(AudioPtr audio, int bitRate, int hysterisis) :
		DataInterfaceBase(audio, bitRate, hysterisis)
	{
		fullWave2400HZSampleCount = audio->SampleRate() / 2400;
		fullWave1200HZSampleCount = audio->SampleRate() / 1200;
	}

	// Read a single byte - we will return a byte when we
	// correctly read a start bit (0), 8 data bits, then
	// two stop bits (1).
	std::pair<TapeIndex, uint8_t> ReadByte(TapeIndex index) {
		for ( ; index < audio->SampleCount() ; index += samplesPerBit / 4) {
			TapeIndex ourIndex = index;
			std::pair<int, TapeIndex> bit;

			// check start bit (expect 0)
			bit = ReadBit(ourIndex);
			ourIndex = bit.first;

			if (bit.second != 0) continue;

			uint8_t resultByte = 0;
			// in theory, we have a stop bit, now get 8 data bits
			for(auto bitIndex = 0; bitIndex < 8; bitIndex++) {
				bit = ReadBit(ourIndex);
				ourIndex = bit.first;

				if(bit.second == 1) {
					resultByte |= 1 << bitIndex;
				}

				if(bit.second == 2) {
					continue;
				}
			}

			// check first stop bit (expect 1)
			bit = ReadBit(ourIndex);
			ourIndex = bit.first;

			if (bit.second != 1) {
				continue;
			}

			// check second stop bit (expect 1)
			bit = ReadBit(ourIndex);
			ourIndex = bit.first;

			if (bit.second != 1) {
				continue;
			}

			return std::make_pair(ourIndex, resultByte);
		}
		return std::make_pair(index, 0);
	}

	void Rewind() { ; }

	// Allow adjusting of bitrate, hysterisis values, and so on.
	void TweakSettings() { ; }
};
