#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include "audio.h"
#include "DataInterface.h"
#include "PolyPhase.hpp"
#include "KansasCity.hpp"

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
	bool confident = true;  // false if decoder reported low confidence
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

	enum TapeType {
		AbsoluteBinary = 0x00,
		Comment = 0x01,
		End = 0x02,
		AutoExecute = 0x03,
		Data = 0x04
	};

    public:
	ScanStatus GetScanStatus() const { return scanStatus; }

	void SetRepairDataLength(bool r) { repairDataLength = r; }

	std::string GetTypeName() const {
		if (!type.value) return "?";
		switch (*(type.value)) {
			case AbsoluteBinary: return "Binary";
			case Comment:        return "Comment";
			case End:            return "End";
			case AutoExecute:    return "AutoExec";
			case Data:           return "Data";
			default:             return "Unknown";
		}
	}

	uint16_t GetAddress() const {
		if (addrL.value && addrH.value)
			return static_cast<uint16_t>(*(addrL.value)) |
			       (static_cast<uint16_t>(*(addrH.value)) << 8);
		return 0;
	}

	uint16_t GetDataLength() {
		if (repairDataLength && !HeaderChecksumIsValid()) {
			return 256;
		}
		if (!ln.value) return 0;
		return *(ln.value) == 0 ? 256 : *(ln.value);
	}

	std::string GetStatusString() const {
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

	uint8_t GetActualHeaderSum() const {
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

	uint8_t GetActualDataSum() const {
		uint8_t sum = 0;
		for (size_t i = 0; i < data.size(); i++)
			if (data[i].value) sum += *(data[i].value);
		if (csData.value) sum += *(csData.value);
		return sum;
	}

	// Generate a hex dump of the header bytes (name[8] + rcdL rcdH ln addrL addrH type + csHeader)
	std::string GetHeaderHexDump() const {
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
				char buf[4];
				snprintf(buf, sizeof(buf), "%02x ", *(hdrBytes[i]->value));
				result += buf;
				uint8_t ch = *(hdrBytes[i]->value);
				ascii += (ch >= 0x20 && ch <= 0x7e) ? static_cast<char>(ch) : '.';
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
	std::string GetHexDump() const {
		std::string result;
		uint16_t len = static_cast<uint16_t>(data.size());

		for (uint16_t i = 0; i < len; i += 16) {
			if (i > 0) result += "\n";

			// Hex portion
			uint16_t lineEnd = std::min(static_cast<uint16_t>(i + 16), len);
			for (uint16_t j = i; j < lineEnd; j++) {
				if (data[j].value) {
					char buf[4];
					snprintf(buf, sizeof(buf), "%02x ", *(data[j].value));
					result += buf;
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

	TapeIndex GetSOHIndex() const {
		return soh.startIndex;
	}

	TapeIndex GetStartIndex() {
		if (soh.startIndex > 0) {
			return soh.startIndex;
		}
		if (leader.size() && leader[0].value) {
			return leader[0].startIndex;
		}
		return 0;
	}

	TapeIndex GetEndIndex() {
		if (data.size() && csData.length > 0) {
			return csData.startIndex + csData.length;
		}
		if (csHeader.length > 0) {
			return csHeader.startIndex + csHeader.length;
		}
		return GetStartIndex();
	}

	uint16_t GetRecordNumber() {
		if (rcdL.value && rcdH.value)
			return static_cast<uint16_t>(*(rcdL.value)) |
			       (static_cast<uint16_t>(*(rcdH.value)) << 8);
		return 0;
	}

	bool RecordIsValid() {
		if (leader.size() == 0) return false;
		if (!soh.value) return false;
		if (*(soh.value) != 0x01) return false;
		if (!HeaderChecksumIsValid()) {
			return false;
		}
		if (!type.value) return false;
		switch(*(type.value)) {
			case AbsoluteBinary:
			case Data:
				return DataChecksumIsValid();
			case Comment:
			case End:
			case AutoExecute:
				return true;
			default:
				return false;
		}
		// not reached:
		return true;
	}

	std::string GetName() {
		if (!NameIsValid()) return "";
		std::string result;
		for (int i = 0; i < 8; i++)
			result += *(name[i].value);
		return result;
	}

	bool NameIsValid() {
		for (int i = 0; i < 8; i++)
			if (!name[i].value) return false;
		return true;
	}

	bool HeaderChecksumIsValid() {
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

	bool DataChecksumIsValid() {
		for (int i = 0; i < data.size(); i++)
			if (!data[i].value) return false;;
		uint8_t sum = 0;
		for (int i = 0; i < data.size(); i++)
			sum += *(data[i].value);
		sum += *(csData.value);
		return sum == 0;
	}

	// Return all TapeBytes in order for tick-mark rendering
	std::vector<const TapeByte *> GetAllBytes() const {
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
	bool ContainsIndex(TapeIndex idx) {
		return idx >= GetStartIndex() && idx < GetEndIndex();
	}

	// Find which TapeByte (field name) a sample index corresponds to
	std::string FieldNameAtIndex(TapeIndex idx) {
		for (auto &b : leader)
			if (idx >= b.startIndex && idx < b.startIndex + b.length) return "leader";
		if (soh.length > 0 && idx >= soh.startIndex && idx < soh.startIndex + soh.length) return "soh";
		for (int i = 0; i < 8; i++)
			if (name[i].length > 0 && idx >= name[i].startIndex && idx < name[i].startIndex + name[i].length)
				return std::string("name[") + std::to_string(i) + "]";
		if (rcdL.length > 0 && idx >= rcdL.startIndex && idx < rcdL.startIndex + rcdL.length) return "rcdL";
		if (rcdH.length > 0 && idx >= rcdH.startIndex && idx < rcdH.startIndex + rcdH.length) return "rcdH";
		if (ln.length > 0 && idx >= ln.startIndex && idx < ln.startIndex + ln.length) return "ln";
		if (addrL.length > 0 && idx >= addrL.startIndex && idx < addrL.startIndex + addrL.length) return "addrL";
		if (addrH.length > 0 && idx >= addrH.startIndex && idx < addrH.startIndex + addrH.length) return "addrH";
		if (type.length > 0 && idx >= type.startIndex && idx < type.startIndex + type.length) return "type";
		if (csHeader.length > 0 && idx >= csHeader.startIndex && idx < csHeader.startIndex + csHeader.length) return "csHeader";
		for (size_t i = 0; i < data.size(); i++)
			if (data[i].length > 0 && idx >= data[i].startIndex && idx < data[i].startIndex + data[i].length)
				return std::string("data[") + std::to_string(i) + "]";
		if (csData.length > 0 && idx >= csData.startIndex && idx < csData.startIndex + csData.length) return "csData";
		return "";
	}

	// Read one byte from the decoder into a TapeByte
	static TapeByte readOneByte(DataInterfacePtr dec, TapeIndex idx, FieldType ft) {
		TapeByte tb;
		tb.fieldType = ft;
		auto result = dec->ReadByteWithBits(idx);
		tb.startIndex = result.startIndex;
		tb.length = result.endIndex - result.startIndex;
		tb.value = result.value;
		tb.confident = result.confident;
		tb.bits = std::move(result.bits);
		return tb;
	}

	// Populate this Record by decoding from the given DataInterface.
	// leaderStart is the index at which to begin scanning for leader bytes.
	// Returns {nextTapeIndex, status}.
	std::pair<TapeIndex, ScanStatus> ReadFromDecoder(DataInterfacePtr dec, TapeIndex leaderStart) {
		TapeIndex idx = leaderStart;

		// --- Find leader and SOH ---
		LeaderResult leaderResult;
		try {
			leaderResult = dec->FindEndOfNextLeader(idx, 10);
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
};

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

	bool ContainsIndex(TapeIndex idx) {
		return idx >= GetStartIndex() && idx < GetEndIndex();
	}
};

class Tape {
	std::vector<File> tapeFiles;
    public:
	std::vector<File> &GetFiles() { return tapeFiles; }
	const std::vector<File> &GetFiles() const { return tapeFiles; }
};

// ---------------------------------------------------------------------------
// MainWindowSettings - holds user-configurable settings
// ---------------------------------------------------------------------------
enum class TapeFormat {
	PolyPhase = 0,
	KansasCity = 1
};

struct MainWindowSettings {
	bool booleanPlaceholder = false;
	bool invertSignal = false;
	uint32_t bitrate = 4800;
	TapeFormat tapeFormat = TapeFormat::PolyPhase;
	// Curve drag interpolation range, as a fraction of one bit-cell cycle.
	// 0.25 = 1/4 cycle.  Valid range roughly 0.1 .. 1.0.
	double curveDragRange = 0.25;
	bool invertMouseWheelScroll = false;
	bool autoRepairHeaderLength = false;
};

// ---------------------------------------------------------------------------
// SettingsDialog - modal dialog bound to a MainWindowSettings object
// ---------------------------------------------------------------------------
class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	SettingsDialog(MainWindowSettings &settings, QWidget *parent = nullptr)
		: QDialog(parent), settingsRef(settings)
	{
		setWindowTitle("Settings");
		auto *layout = new QFormLayout(this);

		invertSignalCheckBox = new QCheckBox(this);
		invertSignalCheckBox->setChecked(settingsRef.invertSignal);
		layout->addRow("Invert Signal", invertSignalCheckBox);

		bitrateSpin = new QSpinBox(this);
		bitrateSpin->setRange(300, 100000);
		bitrateSpin->setValue(static_cast<int>(settingsRef.bitrate));
		layout->addRow("Bitrate", bitrateSpin);

		tapeFormatCombo = new QComboBox(this);
		tapeFormatCombo->addItem("Poly-88 Phase Encoding", static_cast<int>(TapeFormat::PolyPhase));
		tapeFormatCombo->addItem("Kansas City Standard", static_cast<int>(TapeFormat::KansasCity));
		tapeFormatCombo->setCurrentIndex(static_cast<int>(settingsRef.tapeFormat));
		layout->addRow("Tape Format", tapeFormatCombo);

		curveDragRangeSpin = new QDoubleSpinBox(this);
		curveDragRangeSpin->setRange(0.05, 1.0);
		curveDragRangeSpin->setSingleStep(0.05);
		curveDragRangeSpin->setDecimals(2);
		curveDragRangeSpin->setValue(settingsRef.curveDragRange);
		curveDragRangeSpin->setSuffix(" cycles");
		layout->addRow("Curve Drag Range", curveDragRangeSpin);

		invertMouseWheelScrollCheckBox = new QCheckBox(this);
		invertMouseWheelScrollCheckBox->setChecked(settingsRef.invertMouseWheelScroll);
		layout->addRow("Invert Mouse Wheel Scroll", invertMouseWheelScrollCheckBox);

		autoRepairHeaderLengthCheckBox = new QCheckBox(this);
		autoRepairHeaderLengthCheckBox->setChecked(settingsRef.autoRepairHeaderLength);
		layout->addRow("Auto Repair Header Length", autoRepairHeaderLengthCheckBox);

		auto *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		layout->addRow(buttons);

		connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
	}

	void accept() override {
		settingsRef.invertSignal = invertSignalCheckBox->isChecked();
		settingsRef.bitrate = static_cast<uint32_t>(bitrateSpin->value());
		settingsRef.tapeFormat = static_cast<TapeFormat>(tapeFormatCombo->currentIndex());
		settingsRef.curveDragRange = curveDragRangeSpin->value();
		settingsRef.invertMouseWheelScroll = invertMouseWheelScrollCheckBox->isChecked();
		settingsRef.autoRepairHeaderLength = autoRepairHeaderLengthCheckBox->isChecked();
		QDialog::accept();
	}

private:
	MainWindowSettings &settingsRef;
	QCheckBox *invertSignalCheckBox;
	QSpinBox *bitrateSpin;
	QComboBox *tapeFormatCombo;
	QDoubleSpinBox *curveDragRangeSpin;
	QCheckBox *invertMouseWheelScrollCheckBox;
	QCheckBox *autoRepairHeaderLengthCheckBox;
};

// ---------------------------------------------------------------------------
// WaveformSelection - data resulting from a Ctrl+click selection
// ---------------------------------------------------------------------------
struct WaveformSelection {
	bool active = false;
	TapeIndex startIndex = 0;
	TapeIndex endIndex = 0;   // end of second byte's last bit
	BitReadResult byte1;      // first decoded byte
	BitReadResult byte2;      // second decoded byte (starts where byte1 ends)
};

// ---------------------------------------------------------------------------
// WaveformView - custom widget for rendering audio waveform
// ---------------------------------------------------------------------------
class WaveformView : public QWidget {
	Q_OBJECT
public:
	WaveformView(QWidget *parent = nullptr)
		: QWidget(parent)
	{
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		setContextMenuPolicy(Qt::CustomContextMenu);

		connect(this, &QWidget::customContextMenuRequested,
			this, &WaveformView::showContextMenu);
	}

	void setDecoder(DataInterfacePtr decoder) {
		this->decoder = decoder;
	}

	void setAudio(AudioPtr a) {
		audio = a;
		scrollOffset = 0;
		clearSelection();
		update();
		emit scrollChanged();
	}

	void setTape(Tape *t) {
		tape = t;
		update();
	}

	void setSettings(const MainWindowSettings *s) {
		settings = s;
	}

	AudioPtr getAudio() const { return audio; }

	void clearSelection() {
		selection = WaveformSelection();
		update();
		emit selectionChanged(selection);
	}

	void computeSelection(TapeIndex startIdx) {
		if (!decoder) return;
		selection = WaveformSelection();
		try {
			selection.byte1 = decoder->ReadByteWithBits(startIdx);
			selection.byte2 = decoder->ReadByteWithBits(selection.byte1.endIndex);
			selection.startIndex = startIdx;
			// Set end to the end of the last bit of byte2
			if (!selection.byte2.bits.empty()) {
				selection.endIndex = selection.byte2.bits.back().endIndex;
			} else {
				selection.endIndex = selection.byte2.endIndex;
			}
			selection.active = true;
		} catch (...) {
			selection = WaveformSelection();
		}
		update();
		emit selectionChanged(selection);
	}

	void refreshSelection() {
		if (selection.active) {
			computeSelection(selection.startIndex);
		}
	}

	// Convert a widget-local X pixel to a sample index
	double pixelToSample(int px) const {
		return scrollOffset + static_cast<double>(px) / xScale;
	}

	// Convert a sample index to a widget-local X pixel
	double sampleToPixel(double sampleIdx) const {
		return (sampleIdx - scrollOffset) * xScale;
	}

	double getScrollOffset() const { return scrollOffset; }
	void setScrollOffset(double offset) {
		if (!audio) return;
		double maxOff = std::max(0.0, static_cast<double>(audio->SampleCount()) -
			width() / xScale);
		scrollOffset = std::clamp(offset, 0.0, maxOff);
		update();
		emit scrollChanged();
	}

	// Number of samples visible in the viewport
	double visibleSamples() const {
		return width() / xScale;
	}

	double totalSamples() const {
		if (!audio) return 1.0;
		return static_cast<double>(audio->SampleCount());
	}

	double getXScale() const { return xScale; }
	double getYScale() const { return yScale; }

signals:
	void mouseSampleChanged(double sampleIndex);
	void scrollChanged();
	void tapeDataChanged();
	void recordClicked(double sampleIndex);
	void selectionChanged(const WaveformSelection &sel);
	void statusMessage(const QString &msg);

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.fillRect(rect(), Qt::black);

		if (!audio || audio->SampleCount() == 0) {
			p.setPen(Qt::gray);
			p.drawText(rect(), Qt::AlignCenter, "No audio loaded");
			return;
		}

		int w = width();
		int h = height();
		int midY = h / 2;

		// Draw zero line
		p.setPen(QColor(60, 60, 60));
		p.drawLine(0, midY, w, midY);

		// Draw waveform - linear interpolation between actual sample values
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(QPen(QColor(0, 200, 0), 1));

		auto sampleToY = [&](double val) -> int {
			int y = midY - static_cast<int>(val * yScale * midY / 32768.0);
			return std::clamp(y, 0, h - 1);
		};

		int count = audio->SampleCount();
		bool first = true;
		int prevPx = 0, prevY = midY;

		for (int px = 0; px < w; px++) {
			double sampleIdx = pixelToSample(px);
			int idx = static_cast<int>(sampleIdx);
			if (idx < 0 || idx >= count - 1) continue;

			// Linear interpolation between adjacent samples
			double frac = sampleIdx - idx;
			double val = audio->Value(idx) * (1.0 - frac) + audio->Value(idx + 1) * frac;
			int screenY = sampleToY(val);

			if (!first) {
				p.drawLine(prevPx, prevY, px, screenY);
			}
			prevPx = px;
			prevY = screenY;
			first = false;
		}
		// Draw sample points when zoomed in enough for editing
		if (xScale >= 2.0) {
			p.setBrush(QColor(255, 255, 100));
			p.setPen(Qt::NoPen);
			for (int px = 0; px < w; px++) {
				double sampleIdx = pixelToSample(px);
				int idx = static_cast<int>(std::round(sampleIdx));
				if (idx < 0 || idx >= count) continue;
				// Only draw if this sample maps near this pixel
				double expectedPx = sampleToPixel(idx);
				if (std::abs(expectedPx - px) > 0.5 * xScale) continue;
				int sy = sampleToY(audio->Value(idx));
				int dotR = (xScale >= 5.0) ? 3 : 2;
				p.drawEllipse(QPointF(expectedPx, sy), dotR, dotR);
			}
		}
		p.setRenderHint(QPainter::Antialiasing, false);

		// Draw waveform selection highlight
		drawSelectionHighlight(p, w, h);

		// Draw TapeByte tick marks on the X axis
		drawByteTickMarks(p, w, h);

		// Draw per-bit tick marks when zoomed in enough
		drawBitTickMarks(p, w, h);
	}

	static QColor colorForFieldType(FieldType ft) {
		switch (ft) {
			case FieldType::Leader:         return QColor(128, 128, 128); // gray
			case FieldType::SOH:            return QColor(255, 255, 0);   // yellow
			case FieldType::Name:           return QColor(0, 200, 255);   // cyan
			case FieldType::HeaderField:    return QColor(100, 100, 255); // blue
			case FieldType::HeaderChecksum: return QColor(255, 165, 0);   // orange
			case FieldType::Data:           return QColor(0, 200, 0);     // green
			case FieldType::DataChecksum:   return QColor(255, 165, 0);   // orange
			default:                        return QColor(200, 200, 200);
		}
	}

	void drawByteTickMarks(QPainter &p, int w, int h) {
		if (!tape) return;

		int tickTopNormal = h - 16;
		int tickTopTall = h - 28;
		int tickBot = h - 2;

		for (auto &file : tape->GetFiles()) {
			for (auto &record : file.GetRecords()) {
				// Draw a tall white tick at the record start
				double recStartPx = sampleToPixel(record.GetStartIndex());
				if (recStartPx >= -1 && recStartPx <= w + 1) {
					p.setPen(QPen(Qt::white, 2));
					int ipx = static_cast<int>(recStartPx);
					p.drawLine(ipx, tickTopTall, ipx, tickBot);
				}

				auto allBytes = record.GetAllBytes();
				for (auto *tb : allBytes) {
					if (tb->length <= 0) continue;
					double px = sampleToPixel(tb->startIndex);
					if (px < -1 || px > w + 1) continue;
					int ipx = static_cast<int>(px);

					// Red override for low-confidence bytes
					QColor color = tb->confident
						? colorForFieldType(tb->fieldType)
						: QColor(255, 0, 0);
					p.setPen(color);

					// Taller ticks for SOH, header checksum, data checksum,
					// or low-confidence bytes
					bool isBoundary = (!tb->confident ||
					                   tb->fieldType == FieldType::SOH ||
					                   tb->fieldType == FieldType::HeaderChecksum ||
					                   tb->fieldType == FieldType::DataChecksum);
					int top = isBoundary ? tickTopTall : tickTopNormal;
					p.drawLine(ipx, top, ipx, tickBot);
				}
			}
		}
	}

	void drawBitTickMarks(QPainter &p, int w, int h) {
		if (!tape || !settings) return;

		// Only draw bit ticks when zoomed in enough that individual bits
		// span at least ~1.5 pixels
		double samplesPerBit = static_cast<double>(
			settings->bitrate > 0 ? (192000.0 / settings->bitrate) : 40);
		if (audio) {
			samplesPerBit = static_cast<double>(audio->SampleRate()) / settings->bitrate;
		}
		if (xScale * samplesPerBit < 1.5) return;

		int bitTickTop = h - 8;
		int bitTickBot = h - 2;

		for (auto &file : tape->GetFiles()) {
			for (auto &record : file.GetRecords()) {
				auto allBytes = record.GetAllBytes();
				for (auto *tb : allBytes) {
					if (tb->bits.empty()) continue;

					QColor color = tb->confident
						? colorForFieldType(tb->fieldType)
						: QColor(255, 0, 0);
					color.setAlpha(120);
					p.setPen(color);

					// Skip the first bit (its boundary = the byte boundary already drawn)
					for (size_t b = 1; b < tb->bits.size(); b++) {
						double px = sampleToPixel(tb->bits[b].startIndex);
						if (px < -1 || px > w + 1) continue;
						int ipx = static_cast<int>(px);
						p.drawLine(ipx, bitTickTop, ipx, bitTickBot);
					}
				}
			}
		}
	}

	void drawSelectionHighlight(QPainter &p, int w, int h) {
		if (!selection.active) return;

		// Collect all bit boundary indices from both decoded bytes
		std::vector<TapeIndex> bitBoundaries;
		for (auto &bi : selection.byte1.bits) {
			bitBoundaries.push_back(bi.startIndex);
			bitBoundaries.push_back(bi.endIndex);
		}
		for (auto &bi : selection.byte2.bits) {
			bitBoundaries.push_back(bi.startIndex);
			bitBoundaries.push_back(bi.endIndex);
		}
		std::sort(bitBoundaries.begin(), bitBoundaries.end());
		// Remove duplicates
		bitBoundaries.erase(
			std::unique(bitBoundaries.begin(), bitBoundaries.end()),
			bitBoundaries.end());

		QColor hlColor(0, 200, 0, 40);  // faint transparent green
		p.setPen(Qt::NoPen);
		p.setBrush(hlColor);

		int pxStart = static_cast<int>(sampleToPixel(selection.startIndex));
		int pxEnd = static_cast<int>(sampleToPixel(selection.endIndex));
		pxStart = std::max(pxStart, 0);
		pxEnd = std::min(pxEnd, w);

		if (bitBoundaries.size() < 2) {
			// No bit info — draw one solid rectangle
			if (pxEnd > pxStart) {
				p.drawRect(pxStart, 0, pxEnd - pxStart, h);
			}
		} else {
			// Draw highlight between consecutive bit boundaries,
			// leaving a 1-pixel gap at each boundary
			for (size_t i = 0; i + 1 < bitBoundaries.size(); i++) {
				int segStart = static_cast<int>(
					std::ceil(sampleToPixel(bitBoundaries[i]))) + 1;
				int segEnd = static_cast<int>(
					sampleToPixel(bitBoundaries[i + 1]));
				segStart = std::max(segStart, pxStart);
				segEnd = std::min(segEnd, pxEnd);
				if (segEnd > segStart) {
					p.drawRect(segStart, 0, segEnd - segStart, h);
				}
			}
		}
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		if (curveDragging && audio) {
			// Curve drag: vertical mouse delta applies a smoothed offset
			// to a range of samples centered on the click point
			double dy = event->position().y() - curveDragStartY;
			double deltaVal = -dy / (yScale * (height() / 2.0) / 32768.0);

			int radius = curveRangeEnd - curveRangeStart;
			int center = curveCenterSample - curveRangeStart;

			for (int i = 0; i < radius; i++) {
				int sIdx = curveRangeStart + i;
				if (sIdx < 0 || sIdx >= audio->SampleCount()) continue;

				// Raised-cosine (Hann) window centered on the click point
				double t = static_cast<double>(i - center) / (radius / 2.0);
				double weight = (std::abs(t) <= 1.0)
					? 0.5 * (1.0 + std::cos(M_PI * t))
					: 0.0;

				double newVal = curveOriginalValues[i] + deltaVal * weight;
				newVal = std::clamp(newVal, -32768.0, 32767.0);
				audio->SetValue(sIdx, static_cast<int16_t>(newVal));
			}
			update();
		} else if (dragging) {
			double dx = event->position().x() - dragLastX;
			setScrollOffset(scrollOffset - dx / xScale);
			dragLastX = event->position().x();
		}

		double sampleIdx = pixelToSample(static_cast<int>(event->position().x()));
		emit mouseSampleChanged(sampleIdx);

		QWidget::mouseMoveEvent(event);
	}

	void mousePressEvent(QMouseEvent *event) override {
		if (event->button() == Qt::LeftButton) {
			bool ctrl = event->modifiers() & Qt::ControlModifier;
			bool shift = event->modifiers() & Qt::ShiftModifier;

			if (ctrl && audio) {
				// Ctrl+click: set waveform selection start
				double clickSample = pixelToSample(
					static_cast<int>(event->position().x()));
				computeSelection(clickSample);
			} else if (shift && audio && xScale >= 2.0) {
				// Curve drag mode: Shift+click when zoomed in enough
				curveDragging = true;
				curveCenterSample = static_cast<int>(std::round(
					pixelToSample(static_cast<int>(event->position().x()))));
				curveDragStartY = event->position().y();

				int radius = curveDragRadius();
				curveRangeStart = std::max(0, curveCenterSample - radius);
				curveRangeEnd = std::min(audio->SampleCount(),
					curveCenterSample + radius);

				// Snapshot original values in the range
				int rangeLen = curveRangeEnd - curveRangeStart;
				curveOriginalValues.resize(rangeLen);
				for (int i = 0; i < rangeLen; i++) {
					curveOriginalValues[i] = audio->Value(curveRangeStart + i);
				}
				setCursor(Qt::SizeVerCursor);
			} else {
				dragging = true;
				dragLastX = event->position().x();
				dragStartX = event->position().x();
				dragStartY = event->position().y();
				setCursor(Qt::ClosedHandCursor);
			}
		}
		QWidget::mousePressEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		if (event->button() == Qt::LeftButton) {
			if (dragging) {
				// Detect click vs drag: if mouse barely moved, treat as click
				double dx = event->position().x() - dragStartX;
				double dy = event->position().y() - dragStartY;
				if (dx * dx + dy * dy < 9.0) {
					double sampleIdx = pixelToSample(
						static_cast<int>(event->position().x()));
					emit recordClicked(sampleIdx);
				}
			}
			if (curveDragging) {
				curveDragging = false;
				curveCenterSample = -1;
				curveOriginalValues.clear();
				refreshSelection();
			}
			dragging = false;
			setCursor(Qt::ArrowCursor);
		}
		QWidget::mouseReleaseEvent(event);
	}

	void wheelEvent(QWheelEvent *event) override {
		double degrees = event->angleDelta().y() / 8.0;
		double steps = degrees / 15.0;

		bool ctrl = event->modifiers() & Qt::ControlModifier;
		bool shift = event->modifiers() & Qt::ShiftModifier;

		if (ctrl && shift) {
			// Ctrl+Shift+wheel: vertical scale
			yScale = std::clamp(yScale + steps * 0.1, 0.1, 50.0);
		} else if (ctrl) {
			// Ctrl+wheel: horizontal scale (zoom) centered on mouse
			double factor = std::pow(1.2, steps);
			double mouseX = event->position().x();
			double sampleAtMouse = pixelToSample(static_cast<int>(mouseX));

			xScale = std::clamp(xScale * factor, 0.0001, 200.0);

			// Keep the sample under the mouse cursor stationary
			scrollOffset = sampleAtMouse - mouseX / xScale;
			double maxOff = std::max(0.0, totalSamples() - visibleSamples());
			scrollOffset = std::clamp(scrollOffset, 0.0, maxOff);
		} else if (shift) {
			// Shift+wheel: scroll waveform left/right
			double scrollSteps = steps;
			if (settings && settings->invertMouseWheelScroll)
				scrollSteps = -scrollSteps;
			double scrollAmount = visibleSamples() * 0.1 * scrollSteps;
			setScrollOffset(scrollOffset - scrollAmount);
		} else {
			// Plain wheel: no action
			event->ignore();
			return;
		}
		update();
		emit scrollChanged();
		event->accept();
	}

private slots:
	void showContextMenu(const QPoint &pos) {
		TapeIndex idx = pixelToSample(pos.x());

		QMenu contextMenu(this);

		QAction *scanForRecordAction = contextMenu.addAction("Scan For Record");
		QAction *scanAllFromHereAction = contextMenu.addAction("Scan All From Here");
		QAction *scanForCarrierAction = contextMenu.addAction("Scan For Carrier");
		contextMenu.addSeparator();
		QAction *dataAction = contextMenu.addAction("Data");
		QAction *waveformAction = contextMenu.addAction("Waveform");

		connect(scanForRecordAction, &QAction::triggered, this,
			[this, idx]() { ScanForRecord(idx); });
		connect(scanAllFromHereAction, &QAction::triggered, this,
			[this, idx]() { ScanAllFromHere(idx); });
		connect(scanForCarrierAction, &QAction::triggered, this,
			[this, idx]() { ScanForCarrier(idx); });
		connect(dataAction, &QAction::triggered, this,
			[this, idx]() { onContextData(idx); });
		connect(waveformAction, &QAction::triggered, this,
			[this, idx]() { onContextWaveform(idx); });

		contextMenu.exec(mapToGlobal(pos));
	}

	void ScanForRecord(TapeIndex idx) {
		if (!decoder || !tape) return;

		Record record;
		if (settings) record.SetRepairDataLength(settings->autoRepairHeaderLength);
		auto [nextIdx, status] = record.ReadFromDecoder(decoder, idx);

		if (status == ScanStatus::AudioEOF || status == ScanStatus::NoLeader ||
			status == ScanStatus::NoSOH) {
			emit statusMessage(QString("No new record found starting at %1")
				.arg(static_cast<qint64>(idx)));
			return;
		}

		TapeIndex sohIdx = record.GetSOHIndex();
		TapeIndex endIdx = record.GetEndIndex();
		qint64 width = static_cast<qint64>(endIdx - sohIdx);

		// Check for duplicate: existing record with same SOH index
		for (auto &file : tape->GetFiles()) {
			for (auto &existing : file.GetRecords()) {
				if (existing.GetSOHIndex() == sohIdx) {
					emit statusMessage(QString(
						"Duplicate record found at %1 (width %2) — "
						"already exists in file %3")
						.arg(static_cast<qint64>(sohIdx))
						.arg(width)
						.arg(QString::fromStdString(existing.GetName()).trimmed()));
					return;
				}
			}
		}

		// Insert the record into the tape, grouped by file name
		std::string recName = record.GetName();
		File *targetFile = nullptr;
		for (auto &file : tape->GetFiles()) {
			if (file.GetRecords().size() &&
				file.GetRecords()[0].GetName() == recName) {
				targetFile = &file;
				break;
			}
		}
		if (!targetFile) {
			tape->GetFiles().emplace_back();
			targetFile = &tape->GetFiles().back();
		}

		// Insert in tape index order
		auto &recs = targetFile->GetRecords();
		auto it = std::lower_bound(recs.begin(), recs.end(), record,
			[](const Record &a, const Record &b) {
				return a.GetSOHIndex() < b.GetSOHIndex();
			});
		recs.insert(it, std::move(record));

		// Scroll to the start of the record
		setScrollOffset(sohIdx > 0 ? sohIdx : idx);
		update();
		emit tapeDataChanged();
		emit statusMessage(QString("New tape record found at %1 (width %2)")
			.arg(static_cast<qint64>(sohIdx)).arg(width));
	}

	void ScanAllFromHere(TapeIndex idx) {
		if (!decoder || !tape || !audio) return;

		// Remove existing records whose start index is >= idx
		for (auto &file : tape->GetFiles()) {
			auto &recs = file.GetRecords();
			recs.erase(
				std::remove_if(recs.begin(), recs.end(),
					[idx](Record &r) { return r.GetStartIndex() >= idx; }),
				recs.end());
		}
		// Remove empty files
		auto &files = tape->GetFiles();
		files.erase(
			std::remove_if(files.begin(), files.end(),
				[](File &f) { return f.GetRecords().empty(); }),
			files.end());

		// Scan from idx until EOF
		std::string currentFileName;
		File *currentFile = files.empty() ? nullptr : &files.back();
		if (currentFile && currentFile->GetRecords().size()) {
			currentFileName = currentFile->GetRecords()[0].GetName();
		}

		while (idx < audio->SampleCount()) {
			Record record;
			if (settings) record.SetRepairDataLength(settings->autoRepairHeaderLength);
			auto [nextIdx, status] = record.ReadFromDecoder(decoder, idx);

			if (status == ScanStatus::AudioEOF || status == ScanStatus::NoLeader) {
				break;
			}

			std::string recName = record.GetName();
			if (!currentFile || recName != currentFileName) {
				files.emplace_back();
				currentFile = &files.back();
				currentFileName = recName;
			}
			currentFile->GetRecords().push_back(std::move(record));
			idx = nextIdx;
		}

		update();
		emit tapeDataChanged();
	}

	void ScanForCarrier(TapeIndex idx) {
		try {
			int bitRate = 4800;
			auto result = audio->ScanForCarrier(idx, 200, bitRate);
			// result.first is where we first found the carrier
			idx = result.second;
		} catch (const std::exception &e) {
			std::cerr << "ScanForCarrier threw exception " << e.what() << std::endl;
		}

		setScrollOffset(idx);
	}

	void onContextData(TapeIndex idx) {
		// placeholder - will be implemented later
	}

	void onContextWaveform(TapeIndex idx) {
		// placeholder - will be implemented later
	}

private:
	DataInterfacePtr decoder;
	AudioPtr audio;
	Tape *tape = nullptr;
	const MainWindowSettings *settings = nullptr;

	double xScale = 0.05;   // pixels per sample (start zoomed out)
	double yScale = 1.0;
	double scrollOffset = 0; // first sample visible at left edge

	bool dragging = false;
	double dragLastX = 0;
	double dragStartX = 0;
	double dragStartY = 0;

	// Waveform selection state (Ctrl+click)
	WaveformSelection selection;

	// Curve drag editing state (Shift+click)
	bool curveDragging = false;
	int curveCenterSample = -1;
	double curveDragStartY = 0;      // mouse Y at drag start
	std::vector<int16_t> curveOriginalValues;  // original sample values in the affected range
	int curveRangeStart = 0;          // first sample index in the range
	int curveRangeEnd = 0;            // one past last sample index

	// Compute the interpolation radius in samples from settings
	int curveDragRadius() const {
		if (!settings || !audio) return 5;
		double samplesPerBit = static_cast<double>(audio->SampleRate()) / settings->bitrate;
		double radius = samplesPerBit * settings->curveDragRange;
		return std::max(1, static_cast<int>(std::round(radius)));
	}
};

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
		setWindowTitle("PolyWaveToImage");
		resize(1200, 800);

		scrollTimer = new QTimer(this);
		scrollTimer->setInterval(50);
		connect(scrollTimer, &QTimer::timeout, this, &MainWindow::onScrollTimerTick);

		buildMenus();
		buildUI();

		statusBar()->showMessage("Ready");
	}

	AudioPtr audioPtr;

private:
	MainWindowSettings settings;
	Tape tape;

	// UI components
	WaveformView *waveformView = nullptr;
	QScrollBar *hScrollBar = nullptr;
	QPushButton *scrollLeftBtn = nullptr;
	QPushButton *scrollRightBtn = nullptr;
	// Selected Waveform row
	QLabel *selWaveIndexLabel = nullptr;
	QLabel *selWaveWidthLabel = nullptr;
	QLabel *selWaveByte1BinLabel = nullptr;
	QLabel *selWaveByte1HexLabel = nullptr;
	QLabel *selWaveByte2BinLabel = nullptr;
	QLabel *selWaveByte2HexLabel = nullptr;

	// Selected Record row
	QLabel *indexLabel = nullptr;
	QLabel *widthLabel = nullptr;
	QLabel *tapeFileLabel = nullptr;
	QLabel *recordNumberLabel = nullptr;
	QLabel *byteLabel = nullptr;
	QLabel *validityLabel = nullptr;
	QTableWidget *recordTable = nullptr;
	QTextEdit *hexDetailView = nullptr;

	bool updatingScrollBar = false;

	// Scroll button acceleration state
	QTimer *scrollTimer = nullptr;
	int scrollDirection = 0;    // -1 = left, +1 = right, 0 = stopped
	int scrollTickCount = 0;

	void buildMenus() {
		// --- File menu ---
		QMenu *fileMenu = menuBar()->addMenu("&File");

		QAction *loadAction = fileMenu->addAction("&Load");
		loadAction->setShortcut(QKeySequence("Ctrl+L"));
		connect(loadAction, &QAction::triggered, this, &MainWindow::onLoad);

		QAction *saveAction = fileMenu->addAction("&Save");
		saveAction->setShortcut(QKeySequence("Ctrl+S"));
		connect(saveAction, &QAction::triggered, this, &MainWindow::onSave);

		fileMenu->addSeparator();

		QAction *settingsAction = fileMenu->addAction("Edit Se&ttings");
		settingsAction->setShortcut(QKeySequence("Ctrl+E"));
		connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

		fileMenu->addSeparator();

		QAction *quitAction = fileMenu->addAction("E&xit");
		quitAction->setShortcut(QKeySequence("Ctrl+Q"));
		connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);

		// --- Scan menu ---
		QMenu *scanMenu = menuBar()->addMenu("&Scan");

		QAction *scanAllAction = scanMenu->addAction("Scan &All Records");
		scanAllAction->setShortcut(QKeySequence("Ctrl+A"));
		connect(scanAllAction, &QAction::triggered, this, &MainWindow::onScanAll);

		// --- Help menu ---
		QMenu *helpMenu = menuBar()->addMenu("&Help");

		QAction *quickHelpAction = helpMenu->addAction("Quick &Help");
		connect(quickHelpAction, &QAction::triggered, this, &MainWindow::onQuickHelp);

		QAction *documentationAction = helpMenu->addAction("&Documentation");
		connect(documentationAction, &QAction::triggered, this, &MainWindow::onDocumentation);

		helpMenu->addSeparator();

		QAction *aboutAction = helpMenu->addAction("&About");
		connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
	}

	void buildUI() {
		auto *centralWidget = new QWidget(this);
		auto *mainLayout = new QVBoxLayout(centralWidget);
		mainLayout->setContentsMargins(0, 0, 0, 0);
		mainLayout->setSpacing(0);

		auto *splitter = new QSplitter(Qt::Vertical, centralWidget);

		// --- Top pane: waveform + scrollbar with buttons ---
		auto *topWidget = new QWidget(splitter);
		auto *topLayout = new QVBoxLayout(topWidget);
		topLayout->setContentsMargins(0, 0, 0, 0);
		topLayout->setSpacing(0);

		waveformView = new WaveformView(topWidget);
		waveformView->setMinimumHeight(200);
		waveformView->setSettings(&settings);
		topLayout->addWidget(waveformView, 1);

		// Scrollbar row with left/right buttons
		auto *scrollRow = new QHBoxLayout();
		scrollRow->setContentsMargins(0, 0, 0, 0);
		scrollRow->setSpacing(0);

		scrollLeftBtn = new QPushButton("<", topWidget);
		scrollLeftBtn->setFixedWidth(24);
		scrollLeftBtn->setAutoRepeat(false);
		scrollRow->addWidget(scrollLeftBtn);

		hScrollBar = new QScrollBar(Qt::Horizontal, topWidget);
		hScrollBar->setStyleSheet(
			"QScrollBar:horizontal { background: #2a2a2a; height: 16px; }"
			"QScrollBar::handle:horizontal { background: #888888; min-width: 20px; border-radius: 3px; }"
			"QScrollBar::handle:horizontal:hover { background: #aaaaaa; }"
			"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
		);
		scrollRow->addWidget(hScrollBar, 1);

		scrollRightBtn = new QPushButton(">", topWidget);
		scrollRightBtn->setFixedWidth(24);
		scrollRightBtn->setAutoRepeat(false);
		scrollRow->addWidget(scrollRightBtn);

		topLayout->addLayout(scrollRow);

		connect(scrollLeftBtn, &QPushButton::pressed, this, [this]() {
			scrollDirection = -1;
			scrollTickCount = 0;
			onScrollTimerTick();
			scrollTimer->start();
		});
		connect(scrollLeftBtn, &QPushButton::released, this, [this]() {
			scrollDirection = 0;
			scrollTimer->stop();
		});
		connect(scrollRightBtn, &QPushButton::pressed, this, [this]() {
			scrollDirection = 1;
			scrollTickCount = 0;
			onScrollTimerTick();
			scrollTimer->start();
		});
		connect(scrollRightBtn, &QPushButton::released, this, [this]() {
			scrollDirection = 0;
			scrollTimer->stop();
		});

		connect(waveformView, &WaveformView::scrollChanged,
			this, &MainWindow::syncScrollBar);
		connect(hScrollBar, &QScrollBar::valueChanged,
			this, &MainWindow::onScrollBarChanged);
		connect(waveformView, &WaveformView::mouseSampleChanged,
			this, &MainWindow::onMouseSampleChanged);
		connect(waveformView, &WaveformView::tapeDataChanged,
			this, &MainWindow::refreshRecordTable);
		connect(waveformView, &WaveformView::recordClicked,
			this, &MainWindow::onWaveformRecordClicked);
		connect(waveformView, &WaveformView::selectionChanged,
			this, &MainWindow::onSelectionChanged);
		connect(waveformView, &WaveformView::statusMessage,
			this, [this](const QString &msg) { statusBar()->showMessage(msg); });

		// --- Middle pane: two rows of status labels ---
		auto *middleWidget = new QWidget(splitter);
		auto *middleVLayout = new QVBoxLayout(middleWidget);
		middleVLayout->setContentsMargins(0, 0, 0, 0);
		middleVLayout->setSpacing(0);

		// Row 1: Selected Waveform
		auto *selWaveRow = new QHBoxLayout();
		selWaveRow->setContentsMargins(8, 2, 8, 2);

		auto makeStatusPairIn = [](QHBoxLayout *row, QWidget *parent, const QString &labelText) -> QLabel * {
			auto *nameLabel = new QLabel(labelText + ":", parent);
			nameLabel->setStyleSheet("font-weight: bold;");
			auto *valueLabel = new QLabel("\u2014", parent);
			row->addWidget(nameLabel);
			row->addWidget(valueLabel);
			row->addSpacing(16);
			return valueLabel;
		};

		auto *selWaveTitleLabel = new QLabel("Selected Waveform", middleWidget);
		selWaveTitleLabel->setStyleSheet("font-weight: bold; color: #88cc88;");
		selWaveRow->addWidget(selWaveTitleLabel);
		selWaveRow->addSpacing(16);

		selWaveIndexLabel = makeStatusPairIn(selWaveRow, middleWidget, "Index");
		selWaveWidthLabel = makeStatusPairIn(selWaveRow, middleWidget, "Width");
		selWaveByte1BinLabel = makeStatusPairIn(selWaveRow, middleWidget, "Byte 1 Bin");
		selWaveByte1HexLabel = makeStatusPairIn(selWaveRow, middleWidget, "Hex");
		selWaveByte2BinLabel = makeStatusPairIn(selWaveRow, middleWidget, "Byte 2 Bin");
		selWaveByte2HexLabel = makeStatusPairIn(selWaveRow, middleWidget, "Hex");
		selWaveRow->addStretch();
		middleVLayout->addLayout(selWaveRow);

		// Row 2: Selected Record
		auto *selRecRow = new QHBoxLayout();
		selRecRow->setContentsMargins(8, 2, 8, 2);

		auto *selRecTitleLabel = new QLabel("Selected Record", middleWidget);
		selRecTitleLabel->setStyleSheet("font-weight: bold; color: #88aacc;");
		selRecRow->addWidget(selRecTitleLabel);
		selRecRow->addSpacing(16);

		indexLabel = makeStatusPairIn(selRecRow, middleWidget, "Index");
		widthLabel = makeStatusPairIn(selRecRow, middleWidget, "Width");
		tapeFileLabel = makeStatusPairIn(selRecRow, middleWidget, "Tape File");
		recordNumberLabel = makeStatusPairIn(selRecRow, middleWidget, "Record Number");
		byteLabel = makeStatusPairIn(selRecRow, middleWidget, "Byte");

		validityLabel = new QLabel("\u2014", middleWidget);
		auto *validTitleLabel = new QLabel("Status:", middleWidget);
		validTitleLabel->setStyleSheet("font-weight: bold;");
		selRecRow->addWidget(validTitleLabel);
		selRecRow->addWidget(validityLabel);

		selRecRow->addStretch();
		middleVLayout->addLayout(selRecRow);

		middleWidget->setMaximumHeight(60);

		// --- Bottom pane: record table + hex detail ---
		auto *bottomWidget = new QWidget(splitter);
		auto *bottomOuterLayout = new QVBoxLayout(bottomWidget);
		bottomOuterLayout->setContentsMargins(0, 0, 0, 0);
		bottomOuterLayout->setSpacing(2);

		auto *bottomSplitter = new QSplitter(Qt::Horizontal, bottomWidget);

		// Record table
		recordTable = new QTableWidget(0, 9, bottomSplitter);
		recordTable->setHorizontalHeaderLabels({
			"File", "Rec#", "Type", "Addr", "Length",
			"Hdr CS", "Data CS", "Start", "Status"
		});
		recordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		recordTable->setSelectionMode(QAbstractItemView::SingleSelection);
		recordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		recordTable->horizontalHeader()->setStretchLastSection(true);
		recordTable->verticalHeader()->setDefaultSectionSize(20);
		recordTable->setAlternatingRowColors(true);
		recordTable->setStyleSheet(
			"QTableWidget { background-color: #1e1e1e; color: #d4d4d4; "
			"  alternate-background-color: #252525; gridline-color: #333; }"
			"QTableWidget::item:selected { background-color: #264f78; }"
			"QHeaderView::section { background-color: #2d2d2d; color: #d4d4d4; "
			"  border: 1px solid #333; padding: 2px; }"
		);

		connect(recordTable, &QTableWidget::cellClicked,
			this, &MainWindow::onRecordTableClicked);

		// Hex detail view
		hexDetailView = new QTextEdit(bottomSplitter);
		hexDetailView->setReadOnly(true);
		hexDetailView->setFont(QFont("Monospace", 9));
		hexDetailView->setStyleSheet(
			"QTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"
		);

		bottomSplitter->addWidget(recordTable);
		bottomSplitter->addWidget(hexDetailView);
		bottomSplitter->setStretchFactor(0, 3);
		bottomSplitter->setStretchFactor(1, 2);

		bottomOuterLayout->addWidget(bottomSplitter);
		bottomWidget->setMinimumHeight(100);

		// Set splitter proportions
		splitter->addWidget(topWidget);
		splitter->addWidget(middleWidget);
		splitter->addWidget(bottomWidget);
		splitter->setStretchFactor(0, 5);
		splitter->setStretchFactor(1, 0);
		splitter->setStretchFactor(2, 2);

		mainLayout->addWidget(splitter);
		setCentralWidget(centralWidget);
	}

	// Sync the horizontal scrollbar to match the WaveformView state.
	void syncScrollBar() {
		if (!waveformView || updatingScrollBar) return;
		updatingScrollBar = true;

		double total = waveformView->totalSamples();
		double visible = waveformView->visibleSamples();
		double offset = waveformView->getScrollOffset();

		// Linear scrollbar mapping
		const int scrollRange = 100000;

		if (total <= visible) {
			hScrollBar->setRange(0, 0);
			hScrollBar->setValue(0);
		} else {
			double maxOffset = total - visible;
			int pos = static_cast<int>(offset / maxOffset * scrollRange);
			int pageStep = static_cast<int>(visible / total * scrollRange);
			pageStep = std::max(pageStep, 1);

			hScrollBar->setRange(0, scrollRange);
			hScrollBar->setPageStep(pageStep);
			hScrollBar->setValue(pos);
		}
		updatingScrollBar = false;

		// Update Index and Width labels
		indexLabel->setText(QString::number(static_cast<qint64>(offset)));
		widthLabel->setText(QString::number(static_cast<qint64>(visible)));
	}

private slots:
	void onScrollTimerTick() {
		if (!waveformView || scrollDirection == 0) return;
		scrollTickCount++;
		// Accelerate: base step * (1 + tickCount), so speed grows the longer held
		double baseStep = waveformView->visibleSamples() * 0.02;
		double step = baseStep * (1.0 + scrollTickCount * 0.5);
		waveformView->setScrollOffset(
			waveformView->getScrollOffset() + scrollDirection * step);
	}

	void onScrollBarChanged(int value) {
		if (updatingScrollBar || !waveformView) return;
		updatingScrollBar = true;

		double total = waveformView->totalSamples();
		double visible = waveformView->visibleSamples();
		const int scrollRange = 100000;

		// Linear inverse mapping
		double maxOffset = total - visible;
		double offset = static_cast<double>(value) / scrollRange * maxOffset;

		waveformView->setScrollOffset(offset);
		updatingScrollBar = false;
	}

	void onMouseSampleChanged(double sampleIndex) {
		if (!audioPtr || sampleIndex < 0 ||
			sampleIndex >= audioPtr->SampleCount()) {
			tapeFileLabel->setText("—");
			recordNumberLabel->setText("—");
			byteLabel->setText("—");
			validityLabel->setText("—");
			validityLabel->setStyleSheet("");
			return;
		}

		bool found = false;
		int fileIdx = 0;
		for (auto &file : tape.GetFiles()) {
			int recIdx = 0;
			for (auto &record : file.GetRecords()) {
				if (record.ContainsIndex(sampleIndex)) {
					tapeFileLabel->setText(
						QString::fromStdString(record.GetName()).trimmed());
					recordNumberLabel->setText(
						QString::number(record.GetRecordNumber()));
					std::string fieldName = record.FieldNameAtIndex(sampleIndex);
					byteLabel->setText(QString::fromStdString(fieldName));

					if (record.RecordIsValid()) {
						validityLabel->setText("valid");
						validityLabel->setStyleSheet(
							"color: green; font-weight: bold;");
					} else {
						validityLabel->setText("invalid");
						validityLabel->setStyleSheet(
							"color: red; font-weight: bold;");
					}
					found = true;
					break;
				}
				recIdx++;
			}
			if (found) break;
			fileIdx++;
		}

		if (!found) {
			tapeFileLabel->setText("—");
			recordNumberLabel->setText("—");
			byteLabel->setText("—");
			validityLabel->setText("—");
			validityLabel->setStyleSheet("");
		}
	}

	DataInterfacePtr createDecoder() {
		if (!audioPtr) return nullptr;
		int hysterisis = 200;
		int bitrate = static_cast<int>(settings.bitrate);
		if (settings.tapeFormat == TapeFormat::KansasCity) {
			hysterisis = 0;
			return std::make_shared<KansasCity>(audioPtr, bitrate, hysterisis);
		} else {
			return std::make_shared<PolyPhase>(audioPtr, bitrate, hysterisis);
		}
	}

	void refreshRecordTable() {
		if (!recordTable) return;
		recordTable->setRowCount(0);

		int row = 0;
		for (auto &file : tape.GetFiles()) {
			for (auto &record : file.GetRecords()) {
				recordTable->insertRow(row);

				auto setItem = [&](int col, const QString &text) {
					auto *item = new QTableWidgetItem(text);
					item->setTextAlignment(Qt::AlignCenter);
					recordTable->setItem(row, col, item);
				};

				setItem(0, QString::fromStdString(record.GetName()));
				setItem(1, QString::number(record.GetRecordNumber()));
				setItem(2, QString::fromStdString(record.GetTypeName()));
				setItem(3, QString("0x%1").arg(record.GetAddress(), 4, 16, QChar('0')));
				setItem(4, QString::number(record.GetDataLength()));

				bool hdrOk = record.HeaderChecksumIsValid();
				bool dataOk = record.DataChecksumIsValid();
				ScanStatus st = record.GetScanStatus();

				auto *hdrItem = new QTableWidgetItem(hdrOk ? "✓" : "✗");
				hdrItem->setTextAlignment(Qt::AlignCenter);
				hdrItem->setForeground(hdrOk ? QColor(0, 200, 0) : QColor(255, 80, 80));
				recordTable->setItem(row, 5, hdrItem);

				auto *dataItem = new QTableWidgetItem(dataOk ? "✓" : "✗");
				dataItem->setTextAlignment(Qt::AlignCenter);
				dataItem->setForeground(dataOk ? QColor(0, 200, 0) : QColor(255, 80, 80));
				recordTable->setItem(row, 6, dataItem);

				setItem(7, QString::number(static_cast<qint64>(record.GetStartIndex())));

				auto *statusItem = new QTableWidgetItem(
					QString::fromStdString(record.GetStatusString()));
				statusItem->setTextAlignment(Qt::AlignCenter);
				if (st == ScanStatus::Ok) {
					statusItem->setForeground(QColor(0, 200, 0));
				} else {
					statusItem->setForeground(QColor(255, 80, 80));
				}
				recordTable->setItem(row, 8, statusItem);

				row++;
			}
		}
		recordTable->resizeColumnsToContents();
	}

	void showRecordDetail(Record &record) {
		if (!hexDetailView) return;
		QString detail;
		detail += QString("<b>%1</b> Record %2  Type: %3  Addr: 0x%4  Len: %5  Status: %6<br>")
			.arg(QString::fromStdString(record.GetName()).trimmed())
			.arg(record.GetRecordNumber())
			.arg(QString::fromStdString(record.GetTypeName()))
			.arg(record.GetAddress(), 4, 16, QChar('0'))
			.arg(record.GetDataLength())
			.arg(QString::fromStdString(record.GetStatusString()));
		detail += QString("  Header Sum: 0x%1  Data Sum: 0x%2<br>")
			.arg(record.GetActualHeaderSum(), 2, 16, QChar('0'))
			.arg(record.GetActualDataSum(), 2, 16, QChar('0'));
		detail += "<pre>" + QString::fromStdString(record.GetHeaderHexDump()) + "\n\n";
		detail += QString::fromStdString(record.GetHexDump()) + "</pre>";
		hexDetailView->setHtml(detail);
	}

	void onRecordTableClicked(int row, int /*col*/) {
		// Find the record at this row index
		int idx = 0;
		for (auto &file : tape.GetFiles()) {
			for (auto &record : file.GetRecords()) {
				if (idx == row) {
					waveformView->setScrollOffset(record.GetStartIndex());
					showRecordDetail(record);
					return;
				}
				idx++;
			}
		}
	}

	void onWaveformRecordClicked(double sampleIndex) {
		// Find which record contains this sample and select it
		int row = 0;
		for (auto &file : tape.GetFiles()) {
			for (auto &record : file.GetRecords()) {
				if (record.ContainsIndex(static_cast<TapeIndex>(sampleIndex))) {
					recordTable->selectRow(row);
					showRecordDetail(record);
					return;
				}
				row++;
			}
		}
	}

	void onSelectionChanged(const WaveformSelection &sel) {
		if (!sel.active) {
			selWaveIndexLabel->setText("\u2014");
			selWaveWidthLabel->setText("\u2014");
			selWaveByte1BinLabel->setText("\u2014");
			selWaveByte1HexLabel->setText("\u2014");
			selWaveByte2BinLabel->setText("\u2014");
			selWaveByte2HexLabel->setText("\u2014");
			return;
		}

		selWaveIndexLabel->setText(QString::number(
			static_cast<qint64>(sel.startIndex)));
		selWaveWidthLabel->setText(QString::number(
			static_cast<qint64>(sel.endIndex - sel.startIndex)));

		auto toBin = [](uint8_t v) -> QString {
			QString s = "0b";
			for (int i = 7; i >= 0; i--)
				s += (v & (1 << i)) ? '1' : '0';
			return s;
		};
		auto toHex = [](uint8_t v) -> QString {
			return QString("0x%1").arg(v, 2, 16, QChar('0'));
		};

		selWaveByte1BinLabel->setText(toBin(sel.byte1.value));
		selWaveByte1HexLabel->setText(toHex(sel.byte1.value));
		selWaveByte2BinLabel->setText(toBin(sel.byte2.value));
		selWaveByte2HexLabel->setText(toHex(sel.byte2.value));
	}

	void onLoad() {
		QString fileName = QFileDialog::getOpenFileName(
			this, "Open WAV File", QString(),
			"WAV files (*.wav);;All files (*)");
		if (fileName.isEmpty()) return;

		try {
			audioPtr = std::make_shared<Audio>(fileName.toStdString());
			waveformView->setAudio(audioPtr);
			waveformView->setDecoder(createDecoder());
			waveformView->setTape(&tape);
			statusBar()->showMessage(
				"Loaded: " + fileName +
				" (" + QString::number(audioPtr->SampleCount()) + " samples)");
		} catch (const std::exception &e) {
			QMessageBox::critical(this, "Error loading file",
				QString::fromStdString(e.what()));
		}
	}

	void onSave() {
		Save();
	}

	void Save() {
		if (!audioPtr) {
			QMessageBox::warning(this, "Save", "No audio file loaded.");
			return;
		}
		if (!audioPtr->IsDirty()) {
			QMessageBox::information(this, "Save", "No changes to save.");
			return;
		}
		QString fileName = QFileDialog::getSaveFileName(
			this, "Save WAV File", QString(),
			"WAV files (*.wav);;All files (*)");
		if (fileName.isEmpty()) return;

		try {
			audioPtr->WriteWAV(fileName.toStdString());
			statusBar()->showMessage("Saved: " + fileName);
		} catch (const std::exception &e) {
			QMessageBox::critical(this, "Error saving file",
				QString::fromStdString(e.what()));
		}
	}

	void onSettings() {
		TapeFormat previousFormat = settings.tapeFormat;
		SettingsDialog dlg(settings, this);
		if (dlg.exec() == QDialog::Accepted) {
			if (audioPtr && settings.tapeFormat != previousFormat) {
				waveformView->setDecoder(createDecoder());
				statusBar()->showMessage(
					QString("Tape format changed to %1")
					.arg(settings.tapeFormat == TapeFormat::KansasCity
						? "Kansas City Standard" : "Poly-88 Phase Encoding"));
			}
		}
	}

	void onScanAll() {
		if (!audioPtr) {
			QMessageBox::warning(this, "Scan", "No audio file loaded.");
			return;
		}

		// Clear existing tape data
		tape.GetFiles().clear();

		DataInterfacePtr dec = createDecoder();
		TapeIndex idx = 0;
		int recordCount = 0;
		int errorCount = 0;
		std::string currentFileName;
		File *currentFile = nullptr;

		while (idx < audioPtr->SampleCount()) {
			Record record;
			record.SetRepairDataLength(settings.autoRepairHeaderLength);
			auto [nextIdx, status] = record.ReadFromDecoder(dec, idx);

			if (status == ScanStatus::AudioEOF) {
				break;
			}

			if (status == ScanStatus::NoLeader) {
				// Could not find leader at all — we're done
				break;
			}

			recordCount++;

			// Group records into Files by name continuity
			std::string recName = record.GetName();
			if (!currentFile || recName != currentFileName) {
				tape.GetFiles().emplace_back();
				currentFile = &tape.GetFiles().back();
				currentFileName = recName;
			}
			currentFile->GetRecords().push_back(std::move(record));

			if (status != ScanStatus::Ok) {
				errorCount++;
			}

			idx = nextIdx;
		}

		waveformView->setTape(&tape);
		waveformView->update();
		refreshRecordTable();
		statusBar()->showMessage(
			QString("Scan complete: %1 records found, %2 errors")
			.arg(recordCount).arg(errorCount));
	}

	void onQuit() {
		auto reply = QMessageBox::question(
			this, "Quit", "Are you sure you want to quit?",
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			QApplication::quit();
		}
	}

	void onQuickHelp() {
		QDialog helpDlg(this);
		helpDlg.setWindowTitle("Quick Help");
		helpDlg.resize(500, 400);
		auto *layout = new QVBoxLayout(&helpDlg);
		auto *browser = new QTextBrowser(&helpDlg);
		browser->setMarkdown("insert help here");
		layout->addWidget(browser);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &helpDlg);
		layout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &helpDlg, &QDialog::accept);
		helpDlg.exec();
	}

	void onDocumentation() {
		QDesktopServices::openUrl(QUrl("http://help.me/fast"));
	}

	void onAbout() {
		QMessageBox::about(this, "About PolyWaveToImage",
			"Written by Paul Anderson");
	}

protected:
	void closeEvent(QCloseEvent *event) override {
		auto reply = QMessageBox::question(
			this, "Quit", "Are you sure you want to quit?",
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			event->accept();
		} else {
			event->ignore();
		}
	}
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	MainWindow win;
	win.show();

	return app.exec();
}

#include "PolyWaveToImage.moc"
