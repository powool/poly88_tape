#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <string>
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

	std::string GetName() const {
		if (records.empty()) return "";
		return records[0].GetName();
	}

	// Validate the file's record set.
	// Returns an empty string if all checks pass, otherwise a
	// newline-separated list of warning messages.
	std::string Validate() const {
		if (records.empty()) return "No records\n";

		std::string warnings;
		std::string fileName = records[0].GetName();
		std::string trimmedName = fileName;
		while (!trimmedName.empty() && trimmedName.back() == ' ')
			trimmedName.pop_back();

		bool hasEndRecord = false;
		int expectedRecNum = 0;

		for (size_t i = 0; i < records.size(); i++) {
			const auto &r = records[i];
			int recNum = r.GetRecordNumber();

			if (recNum != expectedRecNum) {
				warnings += std::format(
					"Record number {} found, expected {}\n",
					recNum, expectedRecNum);
			}
			expectedRecNum = recNum + 1;

			if (r.GetName() != fileName) {
				warnings += std::format(
					"Record {} has name '{}', expected '{}'\n",
					recNum, r.GetName(), trimmedName);
			}

			if (r.GetType() == Record::Type::End) {
				hasEndRecord = true;
				if (i != records.size() - 1) {
					warnings += std::format(
						"End record at position {} is not the last record\n",
						static_cast<int>(i));
				}
			}

			auto rtype = r.GetType();
			bool lengthExempt = (rtype == Record::Type::Comment ||
				rtype == Record::Type::End ||
				rtype == Record::Type::AutoExecute);
			if (!lengthExempt && i < records.size() - 1) {
				if (r.GetDataLength() != 256) {
					warnings += std::format(
						"Record {} has length {}, expected 256\n",
						recNum, r.GetDataLength());
				}
			}

			if (!r.HeaderChecksumIsValid()) {
				warnings += std::format(
					"Record {} has header checksum error\n", recNum);
			}
			if (!r.DataChecksumIsValid()) {
				warnings += std::format(
					"Record {} has data checksum error\n", recNum);
			}
		}

		if (!hasEndRecord) {
			warnings += "No End record found\n";
		}

		return warnings;
	}

	// Try to append a record to this file.
	// Returns true if the record belongs in this file.
	// A new tape file starts when we see record number 0
	// with a good header checksum — that always begins a new file.
	bool AppendRecord(Record &record) {
		if (records.empty()) {
			records.push_back(std::move(record));
			return true;
		}

		bool hdrOk = record.HeaderChecksumIsValid();
		uint16_t recNum = record.GetRecordNumber();

		// Record number 0 with a good header always starts a new file
		if (recNum == 0 && hdrOk) {
			return false;
		}

		// Different name starts a new file
		if (record.GetName() != GetName()) {
			return false;
		}

		records.push_back(std::move(record));
		return true;
	}

	// Read all records from a CAS file. Returns true on success.
	bool ReadCas(const std::string &fileName) {
		std::ifstream ifs(fileName, std::ios::binary);
		if (!ifs) return false;

		records.clear();
		try {
			while (ifs.peek() != EOF) {
				records.emplace_back(ifs);
			}
		} catch (const std::runtime_error &) {
			return false;
		}
		return !records.empty();
	}

	// Merge records from otherFile into *this.
	// For each record in *this that has checksum errors, compare it
	// with the corresponding record in otherFile and call acceptOrReject
	// with a description of differences. If accepted, replace the record.
	// Returns a summary of actions taken.
	std::string Merge(const File &otherFile,
		std::function<bool(const std::string &prompt)> acceptOrReject)
	{
		std::string summary;
		const auto &otherRecs = otherFile.GetRecords();

		for (size_t i = 0; i < records.size(); i++) {
			bool hdrBad = !records[i].HeaderChecksumIsValid();
			bool dataBad = !records[i].DataChecksumIsValid();
			if (!hdrBad && !dataBad) continue;

			if (i >= otherRecs.size()) {
				summary += std::format("Record {}: bad but no corresponding record in other file\n",
					records[i].GetRecordNumber());
				continue;
			}

			std::string diffs = records[i].Compare(otherRecs[i]);
			if (diffs.empty()) continue;

			std::string prompt = std::format(
				"Record {} ({}):\n{}Replace with other file's copy?",
				records[i].GetRecordNumber(),
				records[i].GetStatusString(),
				diffs);

			if (acceptOrReject(prompt)) {
				records[i] = otherRecs[i];
				summary += std::format("Record {}: replaced\n",
					records[i].GetRecordNumber());
			} else {
				summary += std::format("Record {}: kept original\n",
					records[i].GetRecordNumber());
			}
		}

		return summary;
	}

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
