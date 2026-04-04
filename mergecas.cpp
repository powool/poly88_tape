#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <string>

#include "File.hpp"

static void printUsage(const char *prog) {
	std::cerr << "Usage: " << prog << " [options] <file1.CAS> <file2.CAS>\n"
		<< "Options:\n"
		<< "  -h, --help          Show this help message\n"
		<< "  -f, --fix-headers   Fix broken checksums in merged output\n"
		<< "  -y, --yes           Automatically accept all merge replacements\n";
}

int main(int argc, char *argv[]) {
	bool fixHeaders = false;
	bool autoYes = false;

	static struct option longOpts[] = {
		{"help",        no_argument, nullptr, 'h'},
		{"fix-headers", no_argument, nullptr, 'f'},
		{"yes",         no_argument, nullptr, 'y'},
		{nullptr,       0,           nullptr,  0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "hfy", longOpts, nullptr)) != -1) {
		switch (opt) {
			case 'h':
				printUsage(argv[0]);
				return 0;
			case 'f':
				fixHeaders = true;
				break;
			case 'y':
				autoYes = true;
				break;
			default:
				printUsage(argv[0]);
				return 1;
		}
	}

	if (optind + 2 != argc) {
		printUsage(argv[0]);
		return 1;
	}

	std::string file1Path = argv[optind];
	std::string file2Path = argv[optind + 1];

	// Load both CAS files
	File file1, file2;

	if (!file1.ReadCas(file1Path)) {
		std::cerr << "Error: failed to read " << file1Path << "\n";
		return 1;
	}
	std::cout << "Loaded " << file1Path << ": "
		<< file1.GetRecords().size() << " records\n";

	if (!file2.ReadCas(file2Path)) {
		std::cerr << "Error: failed to read " << file2Path << "\n";
		return 1;
	}
	std::cout << "Loaded " << file2Path << ": "
		<< file2.GetRecords().size() << " records\n";

	// Show all differences between corresponding records
	auto &recs1 = file1.GetRecords();
	auto &recs2 = file2.GetRecords();
	size_t maxRecs = std::max(recs1.size(), recs2.size());

	std::cout << "\n--- Differences ---\n";
	bool anyDiffs = false;
	for (size_t i = 0; i < maxRecs; i++) {
		if (i >= recs1.size()) {
			std::cout << std::format("Record index {}: only in {}\n", i, file2Path);
			anyDiffs = true;
			continue;
		}
		if (i >= recs2.size()) {
			std::cout << std::format("Record index {}: only in {}\n", i, file1Path);
			anyDiffs = true;
			continue;
		}
		std::string diffs = recs1[i].Compare(recs2[i]);
		if (!diffs.empty()) {
			std::cout << std::format("Record {} (index {}):\n{}",
				recs1[i].GetRecordNumber(), i, diffs);
			anyDiffs = true;
		}
	}

	if (!anyDiffs) {
		std::cout << "No differences found.\n";
		return 0;
	}

	// Merge: for any bad record in file1, offer to replace with file2's copy
	std::cout << "\n--- Merge ---\n";
	std::string mergeResult = file1.Merge(file2,
		[autoYes](const std::string &prompt) -> bool {
			if (autoYes) {
				std::cout << "\n" << prompt << " -> auto-accepting\n";
				return true;
			}
			std::cout << "\n" << prompt << " [y/n] ";
			std::string response;
			std::getline(std::cin, response);
			return !response.empty() && (response[0] == 'y' || response[0] == 'Y');
		});

	if (mergeResult.empty()) {
		std::cout << "No records needed merging.\n";
	} else {
		std::cout << mergeResult;
	}

	// Fix checksums if requested
	if (fixHeaders) {
		std::cout << "\n--- Fixing checksums ---\n";
		for (auto &r : file1.GetRecords()) {
			if (!r.HeaderChecksumIsValid()) {
				std::cout << std::format("Record {}: fixing header checksum\n",
					r.GetRecordNumber());
				r.FixHeaderChecksum();
			}
			if (!r.DataChecksumIsValid()) {
				std::cout << std::format("Record {}: fixing data checksum\n",
					r.GetRecordNumber());
				r.FixDataChecksum();
			}
		}
	}

	// Write output: BASENAME-merged.CAS
	std::filesystem::path p(file1Path);
	std::string stem = p.stem().string();
	std::string outPath = (p.parent_path() / (stem + "-merged.CAS")).string();

	std::ofstream ofs(outPath, std::ios::binary);
	if (!ofs) {
		std::cerr << "Error: failed to create " << outPath << "\n";
		return 1;
	}

	int recordsWritten = 0;
	for (auto &r : file1.GetRecords()) {
		r.Write(ofs, true);
		recordsWritten++;
	}
	ofs.close();

	std::cout << std::format("\nWrote {} records to {}\n", recordsWritten, outPath);
	return 0;
}
