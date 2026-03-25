#include <array>
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

#include "audio.h"

const WaveHeader::ChunkHeader *WaveHeader::GetChunkHeader(const char *header) {
	int chunkHeaderOffset = sizeof(RIFFHeader);

	return reinterpret_cast<const ChunkHeader *>(header + chunkHeaderOffset);
}

int WaveHeader::Size() {
	// probably not safe/portable:
	return sizeof(RIFFHeader) + sizeof(ChunkHeader) + sizeof(DataHeader);
}

uint32_t WaveHeader::DataSize(const char *header) {
	int dataHeaderOffset = sizeof(RIFFHeader) + sizeof(ChunkHeader);

	const DataHeader *dataHeader = reinterpret_cast<const DataHeader *>(header + dataHeaderOffset);
	return dataHeader->size;
}

uint32_t WaveHeader::SampleSize(const char *header) {
	return GetChunkHeader(header)->sampleSize;
}

uint32_t WaveHeader::SamplesPerSecond(const char *header) {
	return GetChunkHeader(header)->samplesPerSecond;
}


Audio::Audio(const std::string &fileName) {
	struct stat b;

	invertPhase = false;

	if(stat(fileName.c_str(), &b)) {
		perror((std::string() + "can't stat " + fileName).c_str());
		exit(1);
	}

	if (b.st_size < WaveHeader::Size()) {
		throw(std::invalid_argument(std::string() + "can't open file: " + fileName + ": truncated header"));
	}

	std::ifstream inputStream(fileName);

	if(!inputStream.good()) {
		throw(std::invalid_argument(std::string() + "can't open file: " + fileName));
	}

	auto headerData = std::make_unique<char []>(WaveHeader::Size());

	inputStream.read(&(headerData[0]), WaveHeader::Size());

	auto dataSize = WaveHeader::DataSize(&(headerData[0]));

	if (dataSize + WaveHeader::Size() > b.st_size) {
		throw(std::invalid_argument(std::string() + "can't open file: " + fileName + ": truncated data"));
	}

	sampleCount = dataSize / WaveHeader::SampleSize(&(headerData[0]));
	samplesPerSecond = WaveHeader::SamplesPerSecond(&(headerData[0]));

	wavData = std::make_unique<int16_t []>(sampleCount);

	inputStream.read(reinterpret_cast<char *>(&(wavData[0])), dataSize);
}

void Audio::SetInvertPhase(bool invertPhase) {this->invertPhase = invertPhase;}

int Audio::Negative(int index) {
	return std::signbit(wavData[index]);
}

int16_t Audio::Value(int index) {
	if(invertPhase) return -wavData[index] - dcOffset;
	else return wavData[index] + dcOffset;
}

void Audio::SetValue(int index, int16_t value) {
	if (index < 0 || index >= sampleCount) return;
	if (invertPhase) {
		wavData[index] = -(value + dcOffset);
	} else {
		wavData[index] = value - dcOffset;
	}
	dirty = true;
}

int Audio::SampleRate() { return samplesPerSecond; }

int Audio::SampleCount() { return sampleCount; }

// samples per second / bits per second => samples per bit
int Audio::SamplesPerBit(int bitRate) { return SampleRate() / bitRate; }

double Audio::TimeOffset(int index) {
	return double(index) / SampleRate();
}

// This detects a negative to positive transition
int Audio::FindThisOrNextZeroCrossing(int index, int hysterisis) {

	// skip to next negative to positive signal transition
	while (index < SampleCount()) {
		if ((Value(index) - hysterisis < 0) && (Value(index + 1) - hysterisis >= 0)) {
			break;
		}
		index++;
	}

	if(index >= SampleCount()) {
		throw AudioEOF("ran out of data");
	}

	return index;
}

// This detects a negative to positive transition
int Audio::FindThisOrPreviousZeroCrossing(int index, int hysterisis) {

	// skip to next negative to positive signal transition
	while (index > 0) {
		if ((Value(index) - hysterisis < 0) && (Value(index + 1) - hysterisis >= 0)) {
			break;
		}
		index--;
	}

	if(index <= 0) {
		throw AudioEOF("ran out of data");
	}

	return index;
}

// This detects a positive to negative transition
int Audio::FindThisOrNextNegativeZeroCrossing(int index, int hysterisis) {

	// skip to next negative to positive signal transition
	while (index < SampleCount()) {
		if ((Value(index) + hysterisis >= 0) && (Value(index + 1) + hysterisis < 0)) {
			break;
		}
		index++;
	}

	if(index >= SampleCount()) {
		throw AudioEOF("ran out of data");
	}

	return index;
}

// This finds a nearby negative to positive transition.
// Apparently not terribly useful.
int Audio::FindNearestZeroCrossing(int index, int bitRate, int hysterisis) {
	auto lastIndex = SampleCount() - 2 * SamplesPerBit(bitRate);
	
	// find nearest negative to positive signal transition
	for (int distance = 0; distance < SamplesPerBit(bitRate); distance++) {
		if ((Value(index + distance) - hysterisis < 0) && (Value(index + distance + 1) - hysterisis >= 0)) {
			index += distance;
			break;
		}
		if ((Value(index - distance) - hysterisis < 0) && (Value(index - distance + 1) - hysterisis >= 0)) {
			index -= distance;
			break;
		}
	}

	return index;
}

// This detects any transition, with any polarity
int Audio::FindThisOrNextTransition(int index, int hysterisis) {
	// Skip to next negative to positive signal transition.
	// Caller needs to verify if this is a local transition or not
	while (index < SampleCount()) {
		if (((Value(index) - hysterisis < 0) && (Value(index + 1) - hysterisis >= 0)) ||
				((Value(index) + hysterisis >=0) && (Value(index + 1) + hysterisis < 0))) {
			break;
		}
		index++;
	}

	if(index >= SampleCount()) {
		throw AudioEOF("ran out of data");
	}

	return index;
}

// This detects any transition, with any polarity
int Audio::FindThisOrPreviousTransition(int index, int hysterisis) {
	// Skip to next negative to positive signal transition.
	// Caller needs to verify if this is a local transition or not
	while (index > 1) {
		if (((Value(index) - hysterisis < 0) && (Value(index + 1) - hysterisis >= 0)) ||
				((Value(index) + hysterisis >=0) && (Value(index + 1) + hysterisis < 0))) {
			break;
		}
		index--;
	}

	if(index <= 0) {
		throw AudioEOF("ran out of data");
	}

	return index;
}

// Detect if this is a regional high point.
// Due to noisy signals, the caller needs to see if this
// peak is unique.
// Example patterns seen near peak:  30 40 50 40 50 40 30
//                                   30 40 50 50 50 40 30
bool Audio::IsAPeak(int index) {
	if (index < 0 || index > sampleCount - 1) return false;
	return !Negative(index) && (Value(index - 1) <= Value(index)) && (Value(index) >= Value(index+1));
}

void Audio::Dump(std::ostream &stream, int index, int count) {
	stream << index << ", " << TimeOffset(index) << "s: ";
	for(auto i = index - 3; i < index; i++) {
		stream << " " << Value(i);
		if (i < index - 1) stream << ", ";
	}
	stream << " (";
	stream << " " << Value(index) << ") ";
	for(auto i = index + 1; i < index + 4 + count; i++) {
		stream << " " << Value(i);
		if (i < index + 3) stream << ", ";
	}
	stream << std::endl;
}

void Audio::WriteWAV(const std::string &fileName) {
	std::ofstream out(fileName, std::ios::binary);
	if (!out.good()) {
		throw std::runtime_error("Cannot open file for writing: " + fileName);
	}

	uint32_t dataSize = sampleCount * sizeof(int16_t);
	uint32_t fileSize = 36 + dataSize;  // 36 = header size minus 8

	// RIFF header
	out.write("RIFF", 4);
	out.write(reinterpret_cast<const char *>(&fileSize), 4);
	out.write("WAVE", 4);

	// fmt chunk
	out.write("fmt ", 4);
	uint32_t chunkSize = 16;
	out.write(reinterpret_cast<const char *>(&chunkSize), 4);
	uint16_t audioFormat = 1;  // PCM
	out.write(reinterpret_cast<const char *>(&audioFormat), 2);
	uint16_t numChannels = 1;
	out.write(reinterpret_cast<const char *>(&numChannels), 2);
	uint32_t sampleRate = samplesPerSecond;
	out.write(reinterpret_cast<const char *>(&sampleRate), 4);
	uint32_t byteRate = samplesPerSecond * sizeof(int16_t);
	out.write(reinterpret_cast<const char *>(&byteRate), 4);
	uint16_t blockAlign = sizeof(int16_t);
	out.write(reinterpret_cast<const char *>(&blockAlign), 2);
	uint16_t bitsPerSample = 16;
	out.write(reinterpret_cast<const char *>(&bitsPerSample), 2);

	// data chunk
	out.write("data", 4);
	out.write(reinterpret_cast<const char *>(&dataSize), 4);
	out.write(reinterpret_cast<const char *>(&wavData[0]), dataSize);

	dirty = false;
}

std::pair<int, int> Audio::ScanForCarrier(int waveIndex, int hysterisis, int &bitRate)
{
	int samplesPerBit = SamplesPerBit(bitRate);
	constexpr int size = 25;
	std::array<int, size> crossings;
	int index = 0;
	waveIndex = FindThisOrNextTransition(waveIndex, hysterisis);
	auto firstWaveIndex = waveIndex;
	while(waveIndex < SampleCount()) {
		auto lastCrossing = waveIndex;
		// The + .5 * samplesPerBit is because finding zero crossings is a little
		// error prone, depending on the input data, and it is useful to skip past
		// where we think we found the zero crossing to get a better idea of where
		// the next one really starts.
		waveIndex = FindThisOrNextZeroCrossing(waveIndex + .5 * samplesPerBit, hysterisis);
		crossings[index++] = waveIndex;
		if (index == size) {
			bool carrierFound = true;
			int target = crossings[1] - crossings[0];
			int variation = target * .10;
			for (auto i = 1; i < size-1; i++) {
				if ( abs((crossings[i+1] - crossings[i]) - target) > variation ) {
					carrierFound = false;
					break;
				}
			}
			if (carrierFound) {
				firstWaveIndex = waveIndex;
//				std::cout << "Carrier found at " << waveIndex << std::endl;
				// find where it stops:
				lastCrossing = waveIndex;
				while(waveIndex < SampleCount()) {
					waveIndex = FindThisOrNextZeroCrossing(waveIndex + .5 * samplesPerBit, hysterisis);
					if ( (abs(waveIndex - lastCrossing) - target) > variation ) {
						break;
					}
					lastCrossing = waveIndex;
				}
				std::cout << "carrier frequency: " << 192000.0 / target << " Hz" << std::endl;
				bitRate = 192000.0 / target;
				// the last waveIndex point is out of the carrier - back up a little so
				// that code can find this crossing
				return std::make_pair(firstWaveIndex, lastCrossing - .25 * samplesPerBit);
			}
			index = 0;
		}
	}
	// not found, bitCellStartIndex will be at end of file
	return std::make_pair(waveIndex, waveIndex);
}
