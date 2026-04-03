#pragma once

#include <cctype>
#include <cstdint>
#include <format>
#include <vector>

#include "audio.h"
#include "Record.hpp"

class File {
	std::vector<Record> records;
    public:
	TapeIndex GetStartIndex() {
		if (records.size()) {
			return records[0].GetStartIndex();
		}
		return 0;
	}

	TapeIndex GetEndIndex() {
		if (records.size()) {
			return records.back().GetEndIndex();
		}
		return 0;
	}

	std::vector<Record> &GetRecords() { return records; }
	const std::vector<Record> &GetRecords() const { return records; }

	bool ContainsIndex(TapeIndex idx) {
		return idx >= GetStartIndex() && idx < GetEndIndex();
	}
};
