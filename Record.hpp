#pragma once

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "DataInterface.h"

// Wave file handler
// bit finder
// E6 finder
// record finder/verifier
// record spans a set of wave file indeces, allow interfactive editing or auto correction

enum class FieldType {
	Leader,
	SOH,
	Name,
	HeaderField,  // rcdL, rcdH, ln, addrL, addrH, type
	HeaderChecksum,
	Data,
	DataChecksum
};

struct TapeByte {
	// WAV file index and length, in units of samples.
	TapeIndex startIndex = 0, length = 0;
	// no value means exactly that - it is unknown
	std::optional<uint8_t> value;
	// override == true -> user or system overrode a value as
	// a placeholder. Note: we might want to automatically consider
	// them to be 0xE6, like the header.
	bool override = false;
	FieldType fieldType = FieldType::Data;
	std::vector<BitInfo> bits;  // per-bit positional data from decoder
};

enum class ScanStatus {
	Ok,
	HeaderChecksumFail,
	DataChecksumFail,
	NoLeader,
	NoSOH,
	AudioEOF
};

class Record {
	enum Type {
		AbsoluteBinary = 0x00,
		Comment = 0x01,
		End = 0x02,
		AutoExecute = 0x03,
		Data = 0x04
	};

	// defined in Poly_88_Operation_Software.pdf page 85
	std::vector<TapeByte> leader;
	TapeByte soh;
	std::array<TapeByte, 8> name;
	TapeByte rcdL;
	TapeByte rcdH;
	TapeByte ln;
	TapeByte addrL;
	TapeByte addrH;
	TapeByte type;
	TapeByte csHeader;

	std::vector<TapeByte> data;
	TapeByte csData;

	ScanStatus scanStatus = ScanStatus::NoLeader;
	bool repairDataLength = false;

    public:
	ScanStatus GetScanStatus() const { return scanStatus; }

	void SetRepairDataLength(bool r) { repairDataLength = r; }

	std::string GetTypeName() const;

	uint16_t GetAddress() const;

	uint16_t GetDataLength();

	std::string GetStatusString() const;

	uint8_t GetActualHeaderSum() const;

	uint8_t GetActualDataSum() const;

	// Generate a hex dump of the header bytes (name[8] + rcdL rcdH ln addrL addrH type + csHeader)
	std::string GetHeaderHexDump() const;

	// Generate a hex dump of the data bytes, 16 bytes per line with ASCII
	std::string GetHexDump() const;

	// Return the raw type byte value (or 0xff if unknown)
	uint8_t GetTypeValue() const;

	// Return an ASCII representation of the header as a single line
	std::string GetHeaderAsAscii() const;

	// Return a reference to the data vector for direct access
	const std::vector<TapeByte> &GetData() const { return data; }

	// Check if record type has data content (Binary, Data, End, or Comment)
	bool HasDataContent() const;

	TapeIndex GetSOHIndex() const;

	TapeIndex GetStartIndex() const;

	TapeIndex GetEndIndex() const;

	uint16_t GetRecordNumber() const;

	bool RecordIsValid();

	std::string GetName() const;

	bool NameIsValid() const;

	bool HeaderChecksumIsValid() const;

	bool DataChecksumIsValid() const;

	// Return all TapeBytes in order for tick-mark rendering
	std::vector<const TapeByte *> GetAllBytes() const;

	// Check if a sample index falls within this record
	bool ContainsIndex(TapeIndex idx) const;

	// Find which TapeByte (field name) a sample index corresponds to
	std::string FieldNameAtIndex(TapeIndex idx);

	// Read one byte from the decoder into a TapeByte
	static TapeByte readOneByte(DataInterfacePtr dec, TapeIndex idx, FieldType ft);

	// Populate this Record by decoding from the given DataInterface.
	// leaderStart is the index at which to begin scanning for leader bytes.
	// Returns {nextTapeIndex, status}.
	std::pair<TapeIndex, ScanStatus> ReadFromDecoder(DataInterfacePtr dec, TapeIndex leaderStart);
};
