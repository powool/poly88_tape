#pragma once

#include "DataInterfaceBase.hpp"

class PolyPhase : public DataInterfaceBase {
	TapeIndex rewindIndex;
	int lastBit = 0;

	// See http://www.kazojc.com/elementy_czynne/IC/8T20.pdf
	//
	// On entry, we can be 100% in sync, but not pointing to a signal transition,
	// this is due to the fact that the signal can be out of phase with the synthetic clock.
	// The only time we know the next transition is exactly on a synthetic clock edge
	// is on the 1->0 transition.
	std::pair<TapeIndex, uint8_t> ReadBit(TapeIndex tapeIndex) {
		TapeIndex startingIndex = tapeIndex;
		// look ahead .75 waveform to see what the value is there and record it
		TapeIndex oneShotTriggerIndex = tapeIndex + .75 * samplesPerBit;

		uint8_t resultBit = audio->Value(oneShotTriggerIndex) > hysterisis;

		// see if we can re-sync exactly
		if (lastBit == 1 && resultBit == 0) {
			// Closed loop:
			//
			// Here, due to the encoding, we guarantee that the following transition will
			// be the beginning of a bit cell. Find it and reset our cell index to that transition.
			tapeIndex = audio->FindThisOrNextTransition(oneShotTriggerIndex, hysterisis);
		} else {
			// Open loop clocking
			tapeIndex += samplesPerBit;
		}

		if (debugBit) {
			std::cout << std::format("{}-{}: {} ({}/{} samples):",
				startingIndex,
				tapeIndex,
				resultBit, tapeIndex - startingIndex,
				samplesPerBit
				);
			for (int i=0; i < tapeIndex - startingIndex; i++) { std::cout << std::format(" {},", audio->Value(startingIndex + i)); }
			std::cout << std::endl;
		}

		lastBit = resultBit;
		return std::make_pair(tapeIndex, resultBit);
	}

    public:
	PolyPhase(AudioPtr audio, int bitrate, int hysterisis) : DataInterfaceBase(audio, bitrate, hysterisis)
	{
	}

#if 0
	// Convert a TapeIndex to a time in seconds
	double TimeIndex(TapeIndex index) = 0;
#endif
	
	// Read a single byte - the returned TapeIndex is a potentially
	// virtual bit pointer - it may or may not point to an audio
	// signal transition.
	//
	// Polyphase is 8 bits, no parity, no start/stop bits
	std::pair<TapeIndex, uint8_t> ReadByte(TapeIndex tapeIndex) {

		std::pair<TapeIndex, uint8_t> result;
		uint8_t resultByte = 0;

		for(auto i = 0 ; i < 8; i++) {
			result = ReadBit(tapeIndex);
			if (i == 1) {
				rewindIndex = tapeIndex;
			}
			if (result.second) {
				resultByte |= (1 << i);
			}
			tapeIndex = result.first;
		}
		result.second = resultByte;
		return result;
	}

	BitReadResult ReadByteWithBits(TapeIndex tapeIndex) override {
		BitReadResult result;
		result.startIndex = tapeIndex;
		uint8_t resultByte = 0;

		for (auto i = 0; i < 8; i++) {
			TapeIndex bitStart = tapeIndex;
			auto bit = ReadBit(tapeIndex);
			if (i == 1) {
				rewindIndex = tapeIndex;
			}
			if (bit.second) {
				resultByte |= (1 << i);
			}
			result.bits.push_back({ bitStart, bit.first, bit.second });
			tapeIndex = bit.first;
		}
		result.endIndex = tapeIndex;
		result.value = resultByte;
		result.confident = true;
		return result;
	}

	TapeIndex Rewind() {
		lastBit = 0;
		return rewindIndex;
	}

	// Allow adjusting of bitrate, hysterisis values, and so on.
	void TweakSettings() {
	}
};
