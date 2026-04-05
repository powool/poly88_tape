#pragma once

#include "audio.h"
#include "DataInterfaceBase.hpp"

// In this format, an E6 looks like this:
//
// 0 0110 0111 1 1
//

class KansasCity : public DataInterfaceBase {
    public:
	// Decode 300 baud byte format data, which is a two tone encoding (AKA
	// frequency shift key - FSK), where 1200HZ represents a 0, and 2400HZ
	// represents a 1.
	//
	// To my knowledge, 300 bits per second is the only speed byte encoded tape I have.
	//
	// Attempt to count accurately by measuring wavelength between
	// each full wave.
	std::pair<TapeIndex, int> ReadBit(TapeIndex index) {
		TapeIndex allowedSampleSkew = .12 * samplesPerBit;

		// ensure we are at a zero crossing
		index = audio->FindThisOrNextZeroCrossing(index, hysterisis);

		TapeIndex startingIndex = index;

		// Skip 4 full cycles - if it is a 1200Hz tone, we'll be
		// seeing the full samplesPerBit number of samples.
		TapeIndex startOfBitIndex = index;
		for (int i = 0; i < 4; i++) {
			// find next zero crossing
			index = audio->FindThisOrNextZeroCrossing(index + 1, hysterisis);
		}

		int resultBit;
		// if that happened over a samplesPerBit time span, then we have a 0 bit:
		if (std::abs(index - startOfBitIndex - samplesPerBit) < allowedSampleSkew) {
			resultBit = 0;
		} else {
			// consume another 4 full cycles
			for (int i = 0; i < 4; i++) {
				index = audio->FindThisOrNextZeroCrossing(index + 1, hysterisis);
			}

			// if the 8 full cycles happen over a samplesPerBit time span, then we have a 1 bit
			if (std::abs(index - startOfBitIndex - samplesPerBit) < allowedSampleSkew) {
				resultBit = 1;
			} else {
				// garbage data - let caller figure it out
				resultBit = 2;
			}
		}

		// notice if we got a short read (this happens with noisy signal
		// at start of tape)
		if (index - startingIndex < samplesPerBit / 2) {
			resultBit = 2;
		}

		// notice if we got a long read (this happens with misaligned reads,
		// but we let caller sort it out)
		if (index - startingIndex > samplesPerBit * 2) {
			resultBit = 2;
		}

		if (debugBit) {
			std::cout << std::format("{}-{}: {} ({}/{} samples):",
				startingIndex,
				index,
				resultBit, index - startingIndex,
				samplesPerBit
				);
			for (int i=0; i < index - startingIndex; i++) { std::cout << std::format(" {},", audio->Value(startingIndex + i)); }
			std::cout << std::endl;
		}

#if 0
		std::cout << initialIndex << ", " << audio->TimeOffset(initialIndex) << "s: fullWaveCount = " << fullWaveCount << " lost sync" << std::endl;
#endif
		return std::make_pair(index, resultBit);
	}

    public:
	KansasCity(AudioPtr audio, int bitRate, int hysterisis) :
		DataInterfaceBase(audio, bitRate, hysterisis)
	{
	}

	// Read a single byte - we will return a byte when we
	// correctly read a start bit (0), 8 data bits, then
	// two stop bits (1).
	std::pair<TapeIndex, uint8_t> ReadByte(TapeIndex index) {
		TapeIndex byteStartIndex;

		TapeIndex ourIndex = index;
		std::pair<int, TapeIndex> bit;

		byteStartIndex = index;

		// check start bit (expect 0)
		bit = ReadBit(ourIndex);
		ourIndex = bit.first;

		uint8_t resultByte = 0;
		// in theory, we have a stop bit, now get 8 data bits
		for(auto bitIndex = 0; bitIndex < 8; bitIndex++) {
			bit = ReadBit(ourIndex);
			ourIndex = bit.first;

			if(bit.second == 1) {
				resultByte |= 1 << bitIndex;
			}
		}

		// check first stop bit (expect 1)
		bit = ReadBit(ourIndex);
		ourIndex = bit.first;

		// check second stop bit (expect 1)
		bit = ReadBit(ourIndex);
		ourIndex = bit.first;

		return std::make_pair(ourIndex, resultByte);
	}

	BitReadResult ReadByteWithBits(TapeIndex index) override {
		BitReadResult result;
		result.startIndex = index;

		TapeIndex ourIndex = index;

		// Start bit (expect 0)
		TapeIndex bitStart = ourIndex;
		auto bit = ReadBit(ourIndex);
		ourIndex = bit.first;

		result.bits.clear();
		result.startIndex = bitStart;
		result.bits.push_back({ bitStart, bit.first, static_cast<uint8_t>(bit.second) });

		uint8_t resultByte = 0;
		// 8 data bits
		for (auto bitIndex = 0; bitIndex < 8; bitIndex++) {
			bitStart = ourIndex;
			bit = ReadBit(ourIndex);
			ourIndex = bit.first;
			if (bit.second == 1) {
				resultByte |= 1 << bitIndex;
			}
			result.bits.push_back({ bitStart, bit.first, static_cast<uint8_t>(bit.second) });
		}

		// First stop bit - although we expect 1 here,
		// 2 (no bit found) can be returned. We don't fix
		// sync problems here, we let our caller do that for us.
		// The caller can see if any of the bit values are 2 if
		// it care, but more likely, it will simply advance a
		// few waveforms and retry.

		bitStart = ourIndex;
		bit = ReadBit(ourIndex);
		ourIndex = bit.first;
		result.bits.push_back({ bitStart, bit.first, static_cast<uint8_t>(bit.second) });

		// Second stop bit (expect 1)
		bitStart = ourIndex;
		bit = ReadBit(ourIndex);
		ourIndex = bit.first;
		result.bits.push_back({ bitStart, bit.first, static_cast<uint8_t>(bit.second) });

		result.endIndex = ourIndex;
		result.value = resultByte;
		return result;
	}

	TapeIndex Rewind() {
		// This returns the zero crossing at the end of what
		// we thought was the stop bit.
		//
		// This is exposed to the callers because higher than
		// byte level syncronization occurs. That is, we could
		// be reading bytes, but not getting 0xe6 record
		// leader bytes. When that happens, we need a place to
		// retry from.
		return rewindIndex;
	}

	// Allow adjusting of bitrate, hysterisis values, and so on.
	void TweakSettings() { ; }
	double GetBitrateEstimate(TapeIndex tapeIndex, int waveformsToSample) override {
		return 300;
	}
};
