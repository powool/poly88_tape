#pragma once

#include <format>

#include "DataInterfaceBase.hpp"

// #define DEBUG

class PolyPhase : public DataInterfaceBase {
	TapeIndex lastReturnedIndex = 0;
	TapeIndex rewindIndex;
	bool syncIfPossible = false;
	int lastBit = 0;

	int adaptiveBitCount = 0;
	TapeIndex adaptiveBitStart = 0;
	double adaptiveSamplesPerBit = 0;

	// See http://www.kazojc.com/elementy_czynne/IC/8T20.pdf
	//
	// On entry, we can be 100% in sync, but not pointing to a signal transition,
	// this is due to the fact that the signal can be out of phase with the synthetic clock.
	// The only time we know the next transition is exactly on a synthetic clock edge
	// is on the 1->0 transition.
	std::pair<TapeIndex, uint8_t> ReadBit(TapeIndex tapeIndex) {
		TapeIndex startingIndex = tapeIndex;
		if (adaptiveSamplesPerBit == 0.0) {
			adaptiveSamplesPerBit = samplesPerBit;
		}
		// look ahead .75 adaptiveSamplesPerBit to see what the value is there and record it
		TapeIndex oneShotTriggerIndex = tapeIndex + .75 * adaptiveSamplesPerBit;

		// if we rewind or seek to a new location, we have
		// to set our previous bit to 1
		if (startingIndex != lastReturnedIndex) {
			syncIfPossible = false;
		}

		uint8_t resultBit = audio->Value(oneShotTriggerIndex) > hysterisis;

#ifdef DEBUG
		std::cout << std::format("ReadBit: samplesPerBit: {} oneshot: {}, value: {}, adaptiveSamplesPerBit: {:.7f}",
				samplesPerBit,
				oneShotTriggerIndex,
				audio->Value(oneShotTriggerIndex),
				adaptiveSamplesPerBit) << std::endl;
#endif

		// see if we can re-sync exactly
		if (syncIfPossible && lastBit == 0 && resultBit == 0) {
			tapeIndex = audio->FindNearestZeroCrossing(oneShotTriggerIndex, bitRate, hysterisis);
		} else if (syncIfPossible && lastBit == 1 && resultBit == 0) {
			// Closed loop:
			//
			// Here, due to the encoding, we guarantee that the following transition will
			// be the beginning of a bit cell. Find it and reset our cell index to that transition.
			// This might be the previous transition, not the next one
			tapeIndex = audio->FindThisOrNextTransition(oneShotTriggerIndex, hysterisis);
//			tapeIndex += adaptiveSamplesPerBit;
#if 0
			std::cout << std::format("ReadBit: sync from one shot: {} to new tape index: {}",
				oneShotTriggerIndex,tapeIndex) << std::endl;
#endif

			if (adaptiveBitStart != 0) {
				adaptiveSamplesPerBit = (tapeIndex - adaptiveBitStart) / adaptiveBitCount;
#ifdef DEBUG
				std::cout << std::format("tapeIndex: {}", tapeIndex) << std::endl;
				std::cout << std::format("adaptiveBitStart: {}", adaptiveBitStart) << std::endl;
				std::cout << std::format("adaptiveSamplesPerBit: {}", adaptiveSamplesPerBit) << std::endl;
#endif
			} else {
				adaptiveBitStart = tapeIndex;
				adaptiveBitCount = 0;
			}
		} else {
			// Open loop clocking
			tapeIndex += adaptiveSamplesPerBit;
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
		lastReturnedIndex = tapeIndex;
		adaptiveBitCount++;
		syncIfPossible = true;
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
#ifdef DEBUG
		std::cout << std::format("ReadByte: last bit: {}", (uint16_t) lastBit) << std::endl;
#endif
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

#ifdef DEBUG
		std::cout << std::format("ReadByteWithBits: last bit: {}", (uint16_t) lastBit) << std::endl;
		std::cout << std::format("ReadByteWithBits: hysterisis: {}", hysterisis) << std::endl;
#endif

		for (auto i = 0; i < 8; i++) {
			TapeIndex bitStart = tapeIndex;
			auto bit = ReadBit(tapeIndex);
#ifdef DEBUG
			std::cout << std::format("ReadByteWithBits: bit: {}, got index: {}, value: {}", i, bit.first, (uint16_t) bit.second) << std::endl;
#endif
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
		return result;
	}

	TapeIndex Rewind() {
		adaptiveBitCount = 0;
		adaptiveBitStart = 0;
		adaptiveSamplesPerBit = samplesPerBit;

		lastBit = 0;
		syncIfPossible = false;
		return rewindIndex;
	}

	// Allow adjusting of bitrate, hysterisis values, and so on.
	void TweakSettings() {
	}

	double GetBitrateEstimate(TapeIndex tapeIndex, int wavesToSample) override {
		double meanBitrate = 0.0;
		for (auto i = 0; i < wavesToSample ; i++) {
			tapeIndex = audio->FindThisOrNextZeroCrossingDouble(tapeIndex, hysterisis);
			auto tapeIndex1 = audio->FindThisOrNextZeroCrossingDouble(tapeIndex+1, hysterisis);
			auto resultBitrate = static_cast<double>(audio->SampleRate()) / (tapeIndex1 - tapeIndex);
			meanBitrate += resultBitrate;

#if 0
			std::cout << std::format("tapeIndex: {} value[index]: {}  value[index+1]: {}",
					tapeIndex, audio->Value(tapeIndex), audio->Value(tapeIndex+1)) << std::endl;
#endif
			tapeIndex = audio->FindThisOrNextZeroCrossingDouble(tapeIndex1 + 1, hysterisis);
		}

		return meanBitrate / wavesToSample;
	}
};
