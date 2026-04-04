#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "DataInterface.h"
#include "File.hpp"

class Tape {
	std::vector<File> tapeFiles;
	DataInterfacePtr scanDecoder;
	bool repairDataLength = false;

	void TruncateFromIndex(TapeIndex start) {
		// Compromise for unified full/partial scan:
		// we keep existing records before `start` and discard anything at/after.
		for (auto &file : tapeFiles) {
			file.RemoveRecord(start, true);
		}
		tapeFiles.erase(
			std::remove_if(tapeFiles.begin(), tapeFiles.end(),
				[](const File &f) { return f.GetRecords().empty(); }),
			tapeFiles.end());
	}

    public:
	std::vector<File> &GetFiles() { return tapeFiles; }
	const std::vector<File> &GetFiles() const { return tapeFiles; }

	void SetScanDecoder(DataInterfacePtr dec) { scanDecoder = std::move(dec); }
	void SetRepairDataLength(bool r) { repairDataLength = r; }

	void Scan(
		TapeIndex start,
		TapeIndex end,
		const std::function<void(const std::string &, uint16_t, int, int)> &updateCallback)
	{
		if (!scanDecoder) return;

		if (start <= 0) {
			tapeFiles.clear();
		} else {
			TruncateFromIndex(start);
		}

		TapeIndex idx = start;
		int recordCount = 0;
		int errorCount = 0;

		while (idx < end) {
			Record record;
			record.SetRepairDataLength(repairDataLength);
			auto [nextIdx, status] = record.ReadFromDecoder(scanDecoder, idx);

			if (status == ScanStatus::AudioEOF || status == ScanStatus::NoLeader) {
				break;
			}

			recordCount++;
			if (status != ScanStatus::Ok) {
				errorCount++;
			}

			std::string recName = record.GetName();
			uint16_t recNum = record.GetRecordNumber();

			// Try to append to the current file; start a new one if it doesn't fit
			if (tapeFiles.empty() || !tapeFiles.back().AppendRecord(record)) {
				tapeFiles.emplace_back();
				tapeFiles.back().AppendRecord(record);
			}

			if (updateCallback) {
				updateCallback(recName, recNum, recordCount, errorCount);
			}

			idx = nextIdx;
		}
	}
};
