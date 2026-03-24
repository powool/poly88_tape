#pragma once

// #pragma pack(push, 1)

#include <cstdint>

struct TapeHeader {
	uint8_t	name[8];
	uint8_t rnLow;
	uint8_t rnHigh;
	uint8_t len;
	uint8_t addrLow;
	uint8_t addrHigh;
	uint8_t type;

	uint16_t rn() { return rnHigh << 8 | rnLow; }
	uint16_t addr() { return addrHigh << 8 | addrLow; }
	std::string GetName() {
		return std::string(&name[0], &name[8]);
	}
};

// #pragma pack(pop)
