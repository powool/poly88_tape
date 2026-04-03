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

		TapeIndex idx = start;
		int recordCount = 0;
		int errorCount = 0;

		std::string currentFileName;
		File *currentFile = nullptr;
		int lastRecNum = -1;
		bool lastHdrOk = false;

		if (start <= 0) {
			tapeFiles.clear();
		} else {
			// Compromise for unified full/partial scan:
			// we keep existing records before `start` and discard anything at/after.
			for (auto &file : tapeFiles) {
				file.RemoveRecord(start, true);
			}
			tapeFiles.erase(
				std::remove_if(tapeFiles.begin(), tapeFiles.end(),
					[](const File &f) { return f.GetRecords().empty(); }),
				tapeFiles.end());

			currentFile = tapeFiles.empty() ? nullptr : &tapeFiles.back();
			if (currentFile && !currentFile->GetRecords().empty()) {
				currentFileName = currentFile->GetRecords()[0].GetName();
				lastRecNum = currentFile->GetRecords().back().GetRecordNumber();
				lastHdrOk = currentFile->GetRecords().back().HeaderChecksumIsValid();
			}
		}

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
			bool hdrOk = record.HeaderChecksumIsValid();

			bool startNewFile = false;
			if (!currentFile || recName != currentFileName) {
				startNewFile = true;
			} else if (recName == currentFileName && hdrOk) {
				if (recNum == 0 && lastRecNum >= 0) {
					startNewFile = true;
				} else if (lastRecNum >= 0 && recNum < lastRecNum && lastHdrOk) {
					startNewFile = true;
				}
			}

			if (startNewFile) {
				tapeFiles.emplace_back();
				currentFile = &tapeFiles.back();
				currentFileName = recName;
			}

			lastRecNum = recNum;
			lastHdrOk = hdrOk;
			currentFile->GetRecords().push_back(std::move(record));

			if (updateCallback) {
				updateCallback(recName, recNum, recordCount, errorCount);
			}

			idx = nextIdx;
		}
	}
};
