#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Record.hpp"

static TapeByte makeTapeByte(uint8_t val, FieldType ft) {
	TapeByte tb;
	tb.value = val;
	tb.fieldType = ft;
	return tb;
}

static uint8_t readByte(std::ifstream &ifs, const char *context) {
	uint8_t b;
	if (!ifs.read(reinterpret_cast<char *>(&b), 1))
		throw std::runtime_error(std::format("Unexpected end of file reading {}", context));
	return b;
}

Record::Record(std::ifstream &ifs) {
	// Skip leader bytes (0xe6)
	uint8_t b;
	bool foundLeader = false;
	while (ifs.read(reinterpret_cast<char *>(&b), 1)) {
		if (b == 0xe6) {
			foundLeader = true;
			TapeByte lb;
			lb.value = 0xe6;
			lb.fieldType = FieldType::Leader;
			leader.push_back(lb);
			continue;
		}
		if (b == 0x01) {
			// SOH found
			soh = makeTapeByte(0x01, FieldType::SOH);
			break;
		}
		throw std::runtime_error(
			std::format("Expected leader (0xe6) or SOH (0x01), got 0x{:02x}", b));
	}

	if (!soh.value || *(soh.value) != 0x01) {
		if (ifs.eof() && !foundLeader)
			throw std::runtime_error("End of file before any record data");
		throw std::runtime_error("SOH byte not found");
	}

	// Read 8 name bytes
	for (int i = 0; i < 8; i++)
		name[i] = makeTapeByte(readByte(ifs, "name"), FieldType::Name);

	// Read header fields: rcdL, rcdH, ln, addrL, addrH, type
	rcdL = makeTapeByte(readByte(ifs, "rcdL"), FieldType::HeaderField);
	rcdH = makeTapeByte(readByte(ifs, "rcdH"), FieldType::HeaderField);
	ln = makeTapeByte(readByte(ifs, "ln"), FieldType::HeaderField);
	addrL = makeTapeByte(readByte(ifs, "addrL"), FieldType::HeaderField);
	addrH = makeTapeByte(readByte(ifs, "addrH"), FieldType::HeaderField);
	type = makeTapeByte(readByte(ifs, "type"), FieldType::HeaderField);

	// Read header checksum
	csHeader = makeTapeByte(readByte(ifs, "header checksum"), FieldType::HeaderChecksum);

	// Read data bytes (length from header)
	uint16_t dataLength = *(ln.value) == 0 ? 256 : *(ln.value);
	data.resize(dataLength);
	for (uint16_t i = 0; i < dataLength; i++)
		data[i] = makeTapeByte(readByte(ifs, "data"), FieldType::Data);

	// Read data checksum
	csData = makeTapeByte(readByte(ifs, "data checksum"), FieldType::DataChecksum);

	// Set scan status based on checksum validity
	if (!HeaderChecksumIsValid())
		scanStatus = ScanStatus::HeaderChecksumFail;
	else if (!DataChecksumIsValid())
		scanStatus = ScanStatus::DataChecksumFail;
	else
		scanStatus = ScanStatus::Ok;
}

std::string Record::Compare(const Record &other) const {
	std::string diffs;

	// Compare name
	if (GetName() != other.GetName())
		diffs += std::format("Name: '{}' vs '{}'\n", GetName(), other.GetName());

	// Compare record number
	if (GetRecordNumber() != other.GetRecordNumber())
		diffs += std::format("Record#: {} vs {}\n", GetRecordNumber(), other.GetRecordNumber());

	// Compare type
	if (GetType() != other.GetType())
		diffs += std::format("Type: {} vs {}\n", GetTypeName(), other.GetTypeName());

	// Compare address
	if (GetAddress() != other.GetAddress())
		diffs += std::format("Address: {:04x} vs {:04x}\n", GetAddress(), other.GetAddress());

	// Compare data length
	if (data.size() != other.data.size())
		diffs += std::format("Data length: {} vs {}\n", data.size(), other.data.size());

	// Compare header checksum validity
	if (HeaderChecksumIsValid() != other.HeaderChecksumIsValid())
		diffs += std::format("Header checksum: {} vs {}\n",
			HeaderChecksumIsValid() ? "valid" : "INVALID",
			other.HeaderChecksumIsValid() ? "valid" : "INVALID");

	// Compare data checksum validity
	if (DataChecksumIsValid() != other.DataChecksumIsValid())
		diffs += std::format("Data checksum: {} vs {}\n",
			DataChecksumIsValid() ? "valid" : "INVALID",
			other.DataChecksumIsValid() ? "valid" : "INVALID");

	// Compare data bytes
	size_t minLen = std::min(data.size(), other.data.size());
	int diffCount = 0;
	for (size_t i = 0; i < minLen; i++) {
		uint8_t a = data[i].value ? *(data[i].value) : 0;
		uint8_t b = other.data[i].value ? *(other.data[i].value) : 0;
		if (a != b) diffCount++;
	}
	diffCount += static_cast<int>(std::abs(
		static_cast<int>(data.size()) - static_cast<int>(other.data.size())));
	if (diffCount > 0)
		diffs += std::format("Data bytes differ: {} byte(s)\n", diffCount);

	return diffs;
}

std::string Record::GetTypeName() const {
	switch (GetType()) {
		case Type::AbsoluteBinary: return "Binary";
		case Type::Comment:        return "Comment";
		case Type::End:            return "End";
		case Type::AutoExecute:    return "AutoExec";
		case Type::Data:           return "Data";
		default:                   return "Unknown";
	}
}

uint16_t Record::GetAddress() const {
	if (addrL.value && addrH.value)
		return static_cast<uint16_t>(*(addrL.value)) |
		       (static_cast<uint16_t>(*(addrH.value)) << 8);
	return 0;
}

uint16_t Record::GetDataLength() const {
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

Record::Type Record::GetType() const {
	if (!type.value) return Type::Unknown;
	switch (static_cast<Type>(*(type.value))) {
		case Type::AbsoluteBinary:
		case Type::Comment:
		case Type::End:
		case Type::AutoExecute:
		case Type::Data:
			return static_cast<Type>(*(type.value));
		default:
			return Type::Unknown;
	}
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

// Check if record type has data content (Binary, Data, or Comment)
bool Record::HasDataContent() const {
	switch (GetType()) {
		case Type::AbsoluteBinary:
		case Type::Data:
		case Type::Comment:
			return true;
		default:
			return false;
	}
}

TapeIndex Record::GetSOHIndex() const {
	return soh.startIndex;
}

TapeIndex Record::GetStartIndex() const {
	if (soh.startIndex > 0) {
		return soh.startIndex;
	}
	if (leader.size() && leader[0].value) {
		return leader[0].startIndex;
	}
	return 0;
}

TapeIndex Record::GetEndIndex() const {
	if (data.size() && csData.length > 0) {
		return csData.startIndex + csData.length;
	}
	if (csHeader.length > 0) {
		return csHeader.startIndex + csHeader.length;
	}
	return GetStartIndex();
}

uint16_t Record::GetRecordNumber() const {
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
	if (GetType() == Type::Unknown) return false;
	switch(GetType()) {
		case Type::AbsoluteBinary:
		case Type::Data:
			return DataChecksumIsValid();
		case Type::Comment:
		case Type::End:
		case Type::AutoExecute:
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

void Record::FixHeaderChecksum() {
	uint8_t sum = 0;
	for (int i = 0; i < 8; i++)
		if (name[i].value) sum += *(name[i].value);
	if (rcdL.value) sum += *(rcdL.value);
	if (rcdH.value) sum += *(rcdH.value);
	if (ln.value) sum += *(ln.value);
	if (addrL.value) sum += *(addrL.value);
	if (addrH.value) sum += *(addrH.value);
	if (type.value) sum += *(type.value);
	csHeader.value = static_cast<uint8_t>(-sum);
}

void Record::FixDataChecksum() {
	uint8_t sum = 0;
	for (size_t i = 0; i < data.size(); i++)
		if (data[i].value) sum += *(data[i].value);
	csData.value = static_cast<uint8_t>(-sum);
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
bool Record::ContainsIndex(TapeIndex idx) const {
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

int Record::Write(std::ofstream &ofs, bool writeCasFormat) const {
	if (writeCasFormat) {
		// Write 16 bytes of 0xe6 leader
		uint8_t leaderByte = 0xe6;
		for (int i = 0; i < 16; i++)
			ofs.write(reinterpret_cast<const char *>(&leaderByte), 1);

		// Write SOH byte
		uint8_t sohByte = 0x01;
		ofs.write(reinterpret_cast<const char *>(&sohByte), 1);

		// Write 14 header bytes: name[8] rcdL rcdH ln addrL addrH type
		auto writeByte = [&](const TapeByte &tb) {
			uint8_t b = tb.value ? *(tb.value) : 0;
			ofs.write(reinterpret_cast<const char *>(&b), 1);
		};
		for (int i = 0; i < 8; i++) writeByte(name[i]);
		writeByte(rcdL);
		writeByte(rcdH);
		writeByte(ln);
		writeByte(addrL);
		writeByte(addrH);
		writeByte(type);

		// Write header checksum
		writeByte(csHeader);
	}

	// Write data bytes
	int dataLen = static_cast<int>(data.size());
	for (int i = 0; i < dataLen; i++) {
		uint8_t b = data[i].value ? *(data[i].value) : 0;
		ofs.write(reinterpret_cast<const char *>(&b), 1);
	}

	if (writeCasFormat) {
		// Write data checksum
		uint8_t b = csData.value ? *(csData.value) : 0;
		ofs.write(reinterpret_cast<const char *>(&b), 1);
	}

	return dataLen;
}

void Record::WriteEndRecord(std::ofstream &ofs, const std::string &tapeFileName, uint16_t recordNumber) {
	// Write 16 bytes of 0xe6 leader
	uint8_t leaderByte = 0xe6;
	for (int i = 0; i < 16; i++)
		ofs.write(reinterpret_cast<const char *>(&leaderByte), 1);

	// Write SOH
	uint8_t sohByte = 0x01;
	ofs.write(reinterpret_cast<const char *>(&sohByte), 1);

	// Build a 14-byte header for End record
	uint8_t endHeader[14] = {};
	// Copy the tape file name (8 bytes, space-padded)
	for (int i = 0; i < 8; i++) {
		endHeader[i] = (i < static_cast<int>(tapeFileName.size()))
			? static_cast<uint8_t>(tapeFileName[i]) : ' ';
	}
	// Record number
	endHeader[8] = static_cast<uint8_t>(recordNumber & 0xff);
	endHeader[9] = static_cast<uint8_t>((recordNumber >> 8) & 0xff);
	// Length = 0
	endHeader[10] = 0;
	// Address = 0
	endHeader[11] = 0;
	endHeader[12] = 0;
	// Type = End
	endHeader[13] = static_cast<uint8_t>(Type::End);

	// Compute header checksum (two's complement so sum of all + checksum = 0)
	uint8_t hdrSum = 0;
	for (int i = 0; i < 14; i++) hdrSum += endHeader[i];
	uint8_t hdrCS = static_cast<uint8_t>(-hdrSum);

	ofs.write(reinterpret_cast<const char *>(endHeader), 14);
	ofs.write(reinterpret_cast<const char *>(&hdrCS), 1);

	// End records have no data, but write a zero data checksum
	uint8_t dataCS = 0;
	ofs.write(reinterpret_cast<const char *>(&dataCS), 1);
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
