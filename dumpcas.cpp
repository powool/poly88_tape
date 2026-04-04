#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <string>

#include "File.hpp"

int main(int argc, char *argv[]) {
	if (argc != 2) {
		std::cerr << "Usage: dumpcas <file.CAS>\n";
		return 1;
	}

	File file;
	if (!file.ReadCas(argv[1])) {
		std::cerr << "Error: failed to read " << argv[1] << "\n";
		return 1;
	}

	auto &recs = file.GetRecords();
	std::cout << std::format("File: {}  Records: {}\n\n", argv[1], recs.size());

	for (size_t i = 0; i < recs.size(); i++) {
		auto &r = recs[i];
		std::cout << std::format("=== Record {} (index {}) ===\n", r.GetRecordNumber(), i);
		std::cout << std::format("  Name:     '{}'\n", r.GetName());
		std::cout << std::format("  Type:     {} (0x{:02x})\n", r.GetTypeName(),
			static_cast<uint8_t>(r.GetType()));
		std::cout << std::format("  Address:  {:04x}\n", r.GetAddress());
		std::cout << std::format("  Length:   {}\n", r.GetData().size());
		std::cout << std::format("  Header:   {}\n",
			r.HeaderChecksumIsValid() ? "valid" : "INVALID");
		std::cout << std::format("  Data CS:  {}\n",
			r.DataChecksumIsValid() ? "valid" : "INVALID");
		std::cout << "  Header hex: " << r.GetHeaderHexDump() << "\n";
		if (!r.GetData().empty()) {
			std::cout << "  Data:\n" << r.GetHexDump() << "\n";
		}
		std::cout << "\n";
	}

	return 0;
}
