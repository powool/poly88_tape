#pragma once

#include <cctype>

#include "PolyPhase.hpp"
#include "TapeHeader.hpp"

class DataError : public std::runtime_error {
public:
        DataError(const std::string s) : std::runtime_error(s) {;}
};

class Record {
	std::string expectedName;
	uint16_t expectedRecordNumber = 0xffff;
	bool gotSOH = false;
	bool gotHeader = false;
	bool gotData = false;
	uint8_t headerChecksum;
	uint8_t dataChecksum;
    public:
	Record() {;}
	Record(std::string expectedName, uint16_t expectedRecordNumber) :
		expectedName(expectedName),
		expectedRecordNumber(expectedRecordNumber) {;}

	static const uint8_t	SOH = 1;

	uint8_t header[sizeof(TapeHeader)];
	uint8_t data[256];

	// We get called after FindEndOfNextLeader, which hands us
	// the soh character, which we need to check here.
	TapeIndex Read(DataInterfacePtr dataInterface, std::pair<TapeIndex, uint8_t> soh) {
		std::pair<TapeIndex, uint8_t> readResult;
		if (soh.second == SOH) {
			gotSOH = true;
		}

		auto tapeIndex  = soh.first;
		uint8_t headerSum = 0;
		for (int i = 0; i < sizeof(TapeHeader); i++) {
			readResult = dataInterface->ReadByte(tapeIndex);
			tapeIndex = readResult.first;
			headerSum += readResult.second;
			header[i] = readResult.second;
		}
		readResult = dataInterface->ReadByte(tapeIndex);
		tapeIndex = readResult.first;
		headerChecksum = readResult.second;
		headerSum += headerChecksum;
		if(headerSum == 0x00) {
			gotHeader = true;
		}

		TapeHeader *tapeHeader = static_cast<TapeHeader *>((void *) &header[0]);
		uint16_t dataLength = tapeHeader->len == 0 ? 256 : tapeHeader->len;

		uint8_t dataSum = 0;
		for (int i = 0; i < dataLength; i++) {
			auto readResult = dataInterface->ReadByte(tapeIndex);
			tapeIndex = readResult.first;
			dataSum += readResult.second;
			data[i] = readResult.second;
		}
		readResult = dataInterface->ReadByte(tapeIndex);
		tapeIndex = readResult.first;
		dataChecksum = readResult.second;
		dataSum += dataChecksum;
		if(dataSum == 0x00) {
			gotData = true;
		}
		return tapeIndex;
	}
	void Dump(bool showAll = false) {
		TapeHeader *tapeHeader = static_cast<TapeHeader *>((void *) &header[0]);
		if (showAll || gotSOH) {
			std::cout << std::format("{}SOH - ", (gotHeader ? "" : "* "));
		}
		int len = tapeHeader->len == 0 ? 256 : tapeHeader->len;
		if (showAll || gotHeader) {
			std::cout <<
				std::format("Name: {} Record {} Type {:1d} Addr {:04x} Length {}",
				tapeHeader->GetName(), tapeHeader->rn(), tapeHeader->type, tapeHeader->addr(), len) <<
				std::endl;
		}
		if (showAll || gotData) {
			std::string characters;
			int i;
			for (i = 0; i < len ; i++) {
				if ((i) % 16 == 0) {
					if (!gotHeader) {
						std::cout << "* ";
					}
				}
				std::cout << std::format("{:02x} ", data[i]);
				if ((i+1) % 8 == 0) {
					std::cout << " ";
				}
				if (std::isprint(data[i])) {
					characters += data[i];
				} else {
					characters += ".";
				}
				if ((i+1) % 16 == 0) {
					std::cout << " " << characters << std::endl;
					characters.clear();
				}
			}
			while(true) {
				if ((i+1) % 8 == 0) {
					std::cout << " ";
				}
				std::cout << "   ";
				if ((i+1) % 16 == 0) break;
				i++;
			}
			if (characters.size()) {
				std::cout << " " << characters;
			}
			std::cout << std::endl;
		}
	}
};
using RecordPtr = std::shared_ptr<Record>;
