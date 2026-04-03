#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Record.hpp"

std::string Record::GetTypeName() const {
	if (!type.value) return "?";
	switch (*(type.value)) {
		case TapeByte::AbsoluteBinary: return "Binary";
		case TapeByte::Comment:        return "Comment";
		case TapeByte::End:            return "End";
		case TapeByte::AutoExecute:    return "AutoExec";
		case TapeByte::Data:           return "Data";
		default:             return "Unknown";
	}
}

uint16_t Record::GetAddress() const {
	if (addrL.value && addrH.value)
		return static_cast<uint16_t>(*(addrL.value)) |
		       (static_cast<uint16_t>(*(addrH.value)) << 8);
	return 0;
}

uint16_t Record::GetDataLength() {
	if (repairDataLength && !HeaderChecksumIsValid()) {
		return 256;
	}
	if (!ln.value) return 0;
	return *(ln.value) == 0 ? 256 : *(ln.value);
}

std::string Record::GetStatusString() const {
	switch (scanStatus) {
		case ScanStatus::Ok:                 return "OK";
		case ScanStatus::HeaderChecksumFail: return "Hdr CS Fail";
		case ScanStatus::DataChecksumFail:   return "Data CS Fail";
		case ScanStatus::NoLeader:           return "No Leader";
		case ScanStatus::NoSOH:              return "No SOH";
		case ScanStatus::AudioEOF:           return "Audio EOF";
		default:                             return "?";
	}
}

uint8_t Record::GetActualHeaderSum() const {
	uint8_t sum = 0;
	for (int i = 0; i < 8; i++)
		if (name[i].value) sum += *(name[i].value);
	if (rcdL.value) sum += *(rcdL.value);
	if (rcdH.value) sum += *(rcdH.value);
	if (ln.value) sum += *(ln.value);
	if (addrL.value) sum += *(addrL.value);
	if (addrH.value) sum += *(addrH.value);
	if (type.value) sum += *(type.value);
	if (csHeader.value) sum += *(csHeader.value);
	return sum;
}

uint8_t Record::GetActualDataSum() const {
	uint8_t sum = 0;
	for (size_t i = 0; i < data.size(); i++)
		if (data[i].value) sum += *(data[i].value);
	if (csData.value) sum += *(csData.value);
	return sum;
}

// Generate a hex dump of the header bytes (name[8] + rcdL rcdH ln addrL addrH type + csHeader)
std::string Record::GetHeaderHexDump() const {
	std::string result;
	std::string ascii;

	// Collect all header TapeBytes in order
	std::vector<const TapeByte *> hdrBytes;
	for (auto &b : name) hdrBytes.push_back(&b);
	hdrBytes.push_back(&rcdL);
	hdrBytes.push_back(&rcdH);
	hdrBytes.push_back(&ln);
	hdrBytes.push_back(&addrL);
	hdrBytes.push_back(&addrH);
	hdrBytes.push_back(&type);
	hdrBytes.push_back(&csHeader);

	for (size_t i = 0; i < hdrBytes.size(); i++) {
		if (hdrBytes[i]->value) {
			result += std::format("{:02x} ", *(hdrBytes[i]->value));
			char ch = *(hdrBytes[i]->value);
			ascii += std::isprint(ch) ? ch : '.';
		} else {
			result += "?? ";
			ascii += '.';
		}
		if (i == 7) result += " ";  // gap after 8 name bytes
	}

	result += "    |" + ascii + "|";
	return result;
}

// Generate a hex dump of the data bytes, 16 bytes per line with ASCII
std::string Record::GetHexDump() const {
	std::string result;
	uint16_t len = static_cast<uint16_t>(data.size());

	for (uint16_t i = 0; i < len; i += 16) {
		if (i > 0) result += "\n";

		// Hex portion
		uint16_t lineEnd = std::min(static_cast<uint16_t>(i + 16), len);
		for (uint16_t j = i; j < lineEnd; j++) {
			if (data[j].value) {
				result += std::format("{:02x} ", *(data[j].value));
			} else {
				result += "?? ";
			}
			if ((j - i + 1) == 8 && (j - i + 1) != 16) result += " ";
		}

		// Pad short last line so ASCII column aligns
		for (uint16_t j = lineEnd; j < i + 16; j++) {
			result += "   ";
			if ((j - i + 1) == 8) result += " ";
		}

		// ASCII portion
		result += " |";
		for (uint16_t j = i; j < lineEnd; j++) {
			if (data[j].value) {
				uint8_t ch = *(data[j].value);
				result += (ch >= 0x20 && ch <= 0x7e)
					? static_cast<char>(ch) : '.';
			} else {
				result += '.';
			}
		}
		result += '|';
	}
	return result;
}

// Return the raw type byte value (or 0xff if unknown)
uint8_t Record::GetTypeValue() const {
	return type.value ? *(type.value) : 0xff;
}


// Return an ASCII representation of the header as a single line
std::string Record::GetHeaderAsAscii() const {
	std::string n;
	n = GetName();
#if 0
	for (int i = 0; i < 8; i++)
		n += name[i].value ? static_cast<char>(*(name[i].value)) : '?';
#endif
	uint16_t rn = 0;
	if (rcdL.value && rcdH.value)
		rn = static_cast<uint16_t>(*(rcdL.value)) |
		     (static_cast<uint16_t>(*(rcdH.value)) << 8);
	uint16_t length = 0;
	if (ln.value) length = *(ln.value) == 0 ? 256 : *(ln.value);
	uint16_t address = 0;
	if (addrL.value && addrH.value)
		address = static_cast<uint16_t>(*(addrL.value)) |
			  (static_cast<uint16_t>(*(addrH.value)) << 8);
	auto typeName = GetTypeName();

	return std::format("Name: {} Record: {} Type: {} Addr: {:04x} Length: {}",
		n, rn, typeName, address, length);
}

// Check if record type has data content (Binary, Data, End, or Comment)
bool Record::HasDataContent() const {
	if (!type.value) return false;
	switch (*(type.value)) {
		case TapeByte::AbsoluteBinary:
		case TapeByte::Data:
		case TapeByte::End:
		case TapeByte::Comment:
			return true;
		default:
			return false;
	}
}

TapeIndex Record::GetSOHIndex() const {
	return soh.startIndex;
}

TapeIndex Record::GetStartIndex() {
	if (soh.startIndex > 0) {
		return soh.startIndex;
	}
	if (leader.size() && leader[0].value) {
		return leader[0].startIndex;
	}
	return 0;
}

TapeIndex Record::GetEndIndex() {
	if (data.size() && csData.length > 0) {
		return csData.startIndex + csData.length;
	}
	if (csHeader.length > 0) {
		return csHeader.startIndex + csHeader.length;
	}
	return GetStartIndex();
}

uint16_t Record::GetRecordNumber() {
	if (rcdL.value && rcdH.value)
		return static_cast<uint16_t>(*(rcdL.value)) |
		       (static_cast<uint16_t>(*(rcdH.value)) << 8);
	return 0;
}

bool Record::RecordIsValid() {
	if (leader.size() == 0) return false;
	if (!soh.value) return false;
	if (*(soh.value) != 0x01) return false;
	if (!HeaderChecksumIsValid()) {
		return false;
	}
	if (!type.value) return false;
	switch(*(type.value)) {
		case TapeByte::AbsoluteBinary:
		case TapeByte::Data:
			return DataChecksumIsValid();
		case TapeByte::Comment:
		case TapeByte::End:
		case TapeByte::AutoExecute:
			return true;
		default:
			return false;
	}
	// not reached:
	return true;
}

std::string Record::GetName() const {
	if (!NameIsValid()) return "";
	std::string result;
	for (int i = 0; i < 8; i++)
		result += *(name[i].value);
	return result;
}

bool Record::NameIsValid() const {
	for (int i = 0; i < 8; i++)
		if (!name[i].value) return false;
	return true;
}

bool Record::HeaderChecksumIsValid() const {
	if (!NameIsValid()) return false;
	if (!rcdL.value) return false;
	if (!rcdL.value) return false;
	if (!ln.value) return false;
	if (!addrL.value) return false;
	if (!addrH.value) return false;
	if (!type.value) return false;
	if (!csHeader.value) return false;

	uint8_t sum = 0;
	for (int i = 0; i < 8; i++)
		sum += *(name[i].value);
	sum += *(rcdL.value);
	sum += *(rcdH.value);
	sum += *(ln.value);
	sum += *(addrL.value);
	sum += *(addrH.value);
	sum += *(type.value);
	sum += *(csHeader.value);
	return sum == 0;
}

bool Record::DataChecksumIsValid() const {
	for (int i = 0; i < data.size(); i++)
		if (!data[i].value) return false;;
	uint8_t sum = 0;
	for (int i = 0; i < data.size(); i++)
		sum += *(data[i].value);
	sum += *(csData.value);
	return sum == 0;
}

// Return all TapeBytes in order for tick-mark rendering
std::vector<const TapeByte *> Record::GetAllBytes() const {
	std::vector<const TapeByte *> result;
	for (auto &b : leader) result.push_back(&b);
	result.push_back(&soh);
	for (auto &b : name) result.push_back(&b);
	result.push_back(&rcdL);
	result.push_back(&rcdH);
	result.push_back(&ln);
	result.push_back(&addrL);
	result.push_back(&addrH);
	result.push_back(&type);
	result.push_back(&csHeader);
	for (auto &b : data) result.push_back(&b);
	result.push_back(&csData);
	return result;
}

// Check if a sample index falls within this record
bool Record::ContainsIndex(TapeIndex idx) {
	return idx >= GetStartIndex() && idx < GetEndIndex();
}

// Find which TapeByte (field name) a sample index corresponds to
std::string Record::FieldNameAtIndex(TapeIndex idx) {
	auto fmt = [](const std::string &label, const TapeByte &b ) {
		return std::format("{}:{:02x}  {}/{}", label, *(b.value), b.startIndex, b.length);
	};
	for (auto &b : leader)
		if (idx >= b.startIndex && idx < b.startIndex + b.length) return "leader";
	if (soh.length > 0 && idx >= soh.startIndex && idx < soh.startIndex + soh.length) return fmt("soh", soh);
	for (int i = 0; i < 8; i++)
		if (name[i].length > 0 && idx >= name[i].startIndex && idx < name[i].startIndex + name[i].length)
			return fmt(std::format("name[{}]", i), name[i]);
	if (rcdL.length > 0 && idx >= rcdL.startIndex && idx < rcdL.startIndex + rcdL.length) return fmt("rcdL", rcdL);
	if (rcdH.length > 0 && idx >= rcdH.startIndex && idx < rcdH.startIndex + rcdH.length) return fmt("rcdH", rcdH);
	if (ln.length > 0 && idx >= ln.startIndex && idx < ln.startIndex + ln.length) return fmt("ln", ln);
	if (addrL.length > 0 && idx >= addrL.startIndex && idx < addrL.startIndex + addrL.length) return fmt("addrL", addrL);
	if (addrH.length > 0 && idx >= addrH.startIndex && idx < addrH.startIndex + addrH.length) return fmt("addrH", addrH);
	if (type.length > 0 && idx >= type.startIndex && idx < type.startIndex + type.length) return fmt("type", type);
	if (csHeader.length > 0 && idx >= csHeader.startIndex && idx < csHeader.startIndex + csHeader.length) return fmt("csHeader", csHeader);
	for (size_t i = 0; i < data.size(); i++)
		if (data[i].length > 0 && idx >= data[i].startIndex && idx < data[i].startIndex + data[i].length)
			return fmt(std::format("data[{}]", i), data[i]);
	if (csData.length > 0 && idx >= csData.startIndex && idx < csData.startIndex + csData.length) return fmt("csData", csData);
	return "";
}

// Read one byte from the decoder into a TapeByte
TapeByte Record::readOneByte(DataInterfacePtr dec, TapeIndex idx, FieldType ft) {
	TapeByte tb;
	tb.fieldType = ft;
	auto result = dec->ReadByteWithBits(idx);
	tb.startIndex = result.startIndex;
	tb.length = result.endIndex - result.startIndex;
	tb.value = result.value;
	tb.bits = std::move(result.bits);
	return tb;
}

// Populate this Record by decoding from the given DataInterface.
// leaderStart is the index at which to begin scanning for leader bytes.
// Returns {nextTapeIndex, status}.
std::pair<TapeIndex, ScanStatus> Record::ReadFromDecoder(DataInterfacePtr dec, TapeIndex leaderStart) {
	TapeIndex idx = leaderStart;

	// --- Find leader and SOH ---
	LeaderResult leaderResult;
	try {
		leaderResult = dec->FindEndOfNextLeader(idx);
	} catch (const AudioEOF &) {
		scanStatus = ScanStatus::AudioEOF;
		return {idx, ScanStatus::AudioEOF};
	} catch (const std::exception &) {
		scanStatus = ScanStatus::NoLeader;
		return {idx, ScanStatus::NoLeader};
	}

	// Record the leader region (0xe6 bytes) up to the SOH byte start.
	{
		TapeByte leaderByte;
		leaderByte.fieldType = FieldType::Leader;
		leaderByte.startIndex = leaderStart;
		leaderByte.length = leaderResult.sohStart - leaderStart;
		leaderByte.value = 0xe6;
		leader.push_back(leaderByte);
	}

	// SOH byte: starts at sohStart, ends at nextIndex
	soh.fieldType = FieldType::SOH;
	soh.startIndex = leaderResult.sohStart;
	soh.length = leaderResult.nextIndex - leaderResult.sohStart;
	soh.value = leaderResult.value;
	idx = leaderResult.nextIndex;

	if (leaderResult.value != 0x01) {
		scanStatus = ScanStatus::NoSOH;
		return {idx, ScanStatus::NoSOH};
	}

	// --- Read header fields ---
	try {
		for (int i = 0; i < 8; i++) {
			name[i] = readOneByte(dec, idx, FieldType::Name);
			idx = name[i].startIndex + name[i].length;
		}
		rcdL = readOneByte(dec, idx, FieldType::HeaderField);
		idx = rcdL.startIndex + rcdL.length;
		rcdH = readOneByte(dec, idx, FieldType::HeaderField);
		idx = rcdH.startIndex + rcdH.length;
		ln = readOneByte(dec, idx, FieldType::HeaderField);
		idx = ln.startIndex + ln.length;
		addrL = readOneByte(dec, idx, FieldType::HeaderField);
		idx = addrL.startIndex + addrL.length;
		addrH = readOneByte(dec, idx, FieldType::HeaderField);
		idx = addrH.startIndex + addrH.length;
		type = readOneByte(dec, idx, FieldType::HeaderField);
		idx = type.startIndex + type.length;
		csHeader = readOneByte(dec, idx, FieldType::HeaderChecksum);
		idx = csHeader.startIndex + csHeader.length;
	} catch (const AudioEOF &) {
		scanStatus = ScanStatus::AudioEOF;
		return {idx, ScanStatus::AudioEOF};
	}

	bool headerCsFailed = !HeaderChecksumIsValid();

	// --- Read data bytes ---
	uint16_t dataLength = GetDataLength();
	try {
		data.resize(dataLength);
		for (uint16_t i = 0; i < dataLength; i++) {
			data[i] = readOneByte(dec, idx, FieldType::Data);
			idx = data[i].startIndex + data[i].length;
		}
		csData = readOneByte(dec, idx, FieldType::DataChecksum);
		idx = csData.startIndex + csData.length;
	} catch (const AudioEOF &) {
		scanStatus = ScanStatus::AudioEOF;
		return {idx, ScanStatus::AudioEOF};
	}

	if (headerCsFailed) {
		scanStatus = ScanStatus::HeaderChecksumFail;
		return {idx, ScanStatus::HeaderChecksumFail};
	}

	if (!DataChecksumIsValid()) {
		scanStatus = ScanStatus::DataChecksumFail;
		return {idx, ScanStatus::DataChecksumFail};
	}

	scanStatus = ScanStatus::Ok;
	return {idx, ScanStatus::Ok};
}
