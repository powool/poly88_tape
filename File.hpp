#pragma once

#include <algorithm>
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

	bool RemoveRecord(TapeIndex idx, bool removeAfter) {
		auto oldSize = records.size();
		records.erase(
			std::remove_if(records.begin(), records.end(),
				[idx, removeAfter](const Record &r) {
					return removeAfter ? (r.GetStartIndex() >= idx) : r.ContainsIndex(idx);
				}),
			records.end());
		return records.size() != oldSize;
	}

	bool ContainsIndex(TapeIndex idx) {
		return idx >= GetStartIndex() && idx < GetEndIndex();
	}
};
