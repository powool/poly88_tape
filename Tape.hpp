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

	struct ScanGroupingState {
		std::string currentFileName;
		File *currentFile = nullptr;
		int lastRecNum = -1;
		bool lastHdrOk = false;
	};

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

	void InitGroupingStateFromExistingTail(ScanGroupingState &state) {
		state.currentFile = tapeFiles.empty() ? nullptr : &tapeFiles.back();
		if (state.currentFile && !state.currentFile->GetRecords().empty()) {
			state.currentFileName = state.currentFile->GetRecords()[0].GetName();
			const auto &lastRec = state.currentFile->GetRecords().back();
			state.lastRecNum = lastRec.GetRecordNumber();
			state.lastHdrOk = lastRec.HeaderChecksumIsValid();
		}
	}

	File &EnsureTargetFileForRecord(const Record &record, ScanGroupingState &state) {
		std::string recName = record.GetName();
		uint16_t recNum = record.GetRecordNumber();
		bool hdrOk = record.HeaderChecksumIsValid();

		bool startNewFile = false;
		if (!state.currentFile || recName != state.currentFileName) {
			startNewFile = true;
		} else if (recName == state.currentFileName && hdrOk) {
			if (recNum == 0 && state.lastRecNum >= 0) {
				startNewFile = true;
			} else if (state.lastRecNum >= 0 && recNum < state.lastRecNum && state.lastHdrOk) {
				startNewFile = true;
			}
		}

		if (startNewFile) {
			tapeFiles.emplace_back();
			state.currentFile = &tapeFiles.back();
			state.currentFileName = recName;
		}

		state.lastRecNum = recNum;
		state.lastHdrOk = hdrOk;
		return *(state.currentFile);
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

		ScanGroupingState grouping;
		InitGroupingStateFromExistingTail(grouping);

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

			auto &targetFile = EnsureTargetFileForRecord(record, grouping);
			targetFile.GetRecords().push_back(std::move(record));

			if (updateCallback) {
				updateCallback(
					grouping.currentFileName,
					static_cast<uint16_t>(grouping.lastRecNum),
					recordCount,
					errorCount);
			}

			idx = nextIdx;
		}
	}
};
