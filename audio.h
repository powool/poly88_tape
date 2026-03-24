#pragma once

#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

class AudioEOF : public std::runtime_error {
public:
	AudioEOF(const char *s) : std::runtime_error(s) {;}
};

class WaveHeader {
	struct RIFFHeader {
		char	RIFF[4];	// 'RIFF'
		uint32_t	size;	//
		char	WAVE[4];	// 'WAVE'
	};

	struct ChunkHeader {
		char		format[4];	// 'fmt '
		uint32_t	chunkSize;	// sizeof(ChunkHeader)
		uint16_t	dataFormat;	// 0x0001
		uint16_t	channels;	// 0x0001
		uint32_t	samplesPerSecond;	// 44100
		uint32_t	bytesPerSecond;	// 0x00015888
		uint16_t	sampleSize;	// 0x0002 (16-bit mono)
		uint16_t	bitsPerSample;	// 0x0010
	};

	struct DataHeader {
		char		ID[4];	// 'data'
		uint32_t	size;	//
	};

	static const ChunkHeader *GetChunkHeader(const char *header);

public:
	static int Size();
	static uint32_t DataSize(const char *header);
	static uint32_t SampleSize(const char *header);
	static uint32_t SamplesPerSecond(const char *header);
};

class Audio {
	std::unique_ptr<int16_t []>	wavData;
	int sampleCount;
	int samplesPerSecond;
	bool invertPhase;
	int dcOffset = 0;
public:
	Audio(const std::string &fileName);
	void SetInvertPhase(bool invertPhase);
	void SetDCOffset(int dcOffset) { this->dcOffset = dcOffset; }

	int Negative(int index);

	int16_t Value(int index);

	int SampleRate();
	int SampleCount();

	// samples per second / bits per second => samples per bit
	int SamplesPerBit(int bitRate);

	double TimeOffset(int index);

	// This detects a negative to positive transition
	int FindThisOrNextZeroCrossing(int index, int hysterisis = 0);

	// This detects a positive to negative transition
	int FindThisOrNextNegativeZeroCrossing(int index, int hysterisis = 0);

	// This finds a nearby negative to positive transition.
	// Apparently not terribly useful.
	int FindNearestZeroCrossing(int index, int bitRate, int hysterisis = 0);

	// This detects any transition, with any polarity
	int FindThisOrNextTransition(int index, int hysterisis = 0);

	// Detect if this is a regional high point.
	// Due to noisy signals, the caller needs to see if this
	// peak is unique.
	// Example patterns seen near peak:  30 40 50 40 50 40 30
	//                                   30 40 50 50 50 40 30
	bool IsAPeak(int index);

	void Dump(std::ostream &stream, int index, int count = 0);
	std::pair<int, int> ScanForCarrier(int index, int hysterisis, int &bitRate);
};

using AudioPtr = std::shared_ptr<Audio>;
