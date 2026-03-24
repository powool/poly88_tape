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
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
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
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include "audio.h"
#include "PolyAudioTapeDecoder.hpp"

// Wave file handler
// bit finder
// E6 finder
// record finder/verifier
// record spans a set of wave file indeces, allow interfactive editing or auto correction

// TapeIndex is a double to allow for the fact that bits
// won't always start at an integral sample index.
using TapeIndex = double;

struct TapeByte {
	// WAV file index and length, in units of samples.
	TapeIndex startIndex, length;
	// no value means exactly that - it is unknown
	std::optional<uint8_t> value;
	// override == true -> user or system overrode a value as
	// a placeholder. Note: we might want to automatically consider
	// them to be 0xE6, like the header.
	bool override;
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

	enum TapeType {
		AbsoluteBinary = 0x00,
		Comment = 0x01,
		End = 0x02,
		AutoExecute = 0x03,
		Data = 0x04
	};

    public:
	TapeIndex GetStartIndex() {
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
		sum += *(rcdL.value);
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
struct MainWindowSettings {
	bool booleanPlaceholder = false;
	bool invertSignal = false;
	uint32_t bitrate = 4800;
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

		booleanCheckBox = new QCheckBox(this);
		booleanCheckBox->setChecked(settingsRef.booleanPlaceholder);
		layout->addRow("Boolean", booleanCheckBox);

		invertSignalCheckBox = new QCheckBox(this);
		invertSignalCheckBox->setChecked(settingsRef.invertSignal);
		layout->addRow("Invert Signal", invertSignalCheckBox);

		bitrateSpin = new QSpinBox(this);
		bitrateSpin->setRange(300, 100000);
		bitrateSpin->setValue(static_cast<int>(settingsRef.bitrate));
		layout->addRow("Bitrate", bitrateSpin);

		auto *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		layout->addRow(buttons);

		connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
	}

	void accept() override {
		settingsRef.booleanPlaceholder = booleanCheckBox->isChecked();
		settingsRef.invertSignal = invertSignalCheckBox->isChecked();
		settingsRef.bitrate = static_cast<uint32_t>(bitrateSpin->value());
		QDialog::accept();
	}

private:
	MainWindowSettings &settingsRef;
	QCheckBox *booleanCheckBox;
	QCheckBox *invertSignalCheckBox;
	QSpinBox *bitrateSpin;
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

	void setDecoder(std::shared_ptr<PolyAudioTapeDecoder> decoder) {
		this->decoder = decoder;
	}

	void setAudio(AudioPtr a) {
		audio = a;
		scrollOffset = 0;
		update();
		emit scrollChanged();
	}

	void setTape(Tape *t) {
		tape = t;
		update();
	}

	AudioPtr getAudio() const { return audio; }

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
		p.setRenderHint(QPainter::Antialiasing, false);

		// Draw TapeByte tick marks on the X axis
		drawByteTickMarks(p, w, h);
	}

	void drawByteTickMarks(QPainter &p, int w, int h) {
		if (!tape) return;

		p.setPen(QColor(100, 100, 255));
		int tickTop = h - 20;
		int tickBot = h - 5;

		for (auto &file : tape->GetFiles()) {
			for (auto &record : file.GetRecords()) {
				auto allBytes = record.GetAllBytes();
				for (auto *tb : allBytes) {
					if (tb->length <= 0) continue;
					double px = sampleToPixel(tb->startIndex);
					if (px < -1 || px > w + 1) continue;
					int ipx = static_cast<int>(px);
					p.drawLine(ipx, tickTop, ipx, tickBot);
				}
			}
		}
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		if (dragging) {
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
			dragging = true;
			dragLastX = event->position().x();
			setCursor(Qt::ClosedHandCursor);
		}
		QWidget::mousePressEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		if (event->button() == Qt::LeftButton) {
			dragging = false;
			setCursor(Qt::ArrowCursor);
		}
		QWidget::mouseReleaseEvent(event);
	}

	void wheelEvent(QWheelEvent *event) override {
		double degrees = event->angleDelta().y() / 8.0;
		double steps = degrees / 15.0;

		if (event->modifiers() & Qt::ControlModifier) {
			// Vertical scale: linear with mouse wheel
			yScale = std::clamp(yScale + steps * 0.1, 0.1, 50.0);
		} else {
			// Horizontal scale: exponential / proportional
			// Larger wheel movement -> exponentially larger scale change
			double factor = std::pow(1.2, steps);
			double mouseX = event->position().x();
			double sampleAtMouse = pixelToSample(static_cast<int>(mouseX));

			xScale = std::clamp(xScale * factor, 0.0001, 200.0);

			// Keep the sample under the mouse cursor stationary
			scrollOffset = sampleAtMouse - mouseX / xScale;
			double maxOff = std::max(0.0, totalSamples() - visibleSamples());
			scrollOffset = std::clamp(scrollOffset, 0.0, maxOff);
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
		QAction *scanForCarrierAction = contextMenu.addAction("Scan For Carrier");
		QAction *dataAction = contextMenu.addAction("Data");
		QAction *waveformAction = contextMenu.addAction("Waveform");

		connect(scanForRecordAction, &QAction::triggered, this,
			[this, idx]() { ScanForRecord(idx); });
		connect(scanForCarrierAction, &QAction::triggered, this,
			[this, idx]() { ScanForCarrier(idx); });
		connect(dataAction, &QAction::triggered, this,
			[this, idx]() { onContextData(idx); });
		connect(waveformAction, &QAction::triggered, this,
			[this, idx]() { onContextWaveform(idx); });

		contextMenu.exec(mapToGlobal(pos));
	}

	void ScanForRecord(TapeIndex idx) {
		decoder->SetIndex(idx);
		decoder->SetBitRate(4800);
		try {
			decoder->ReadRecord(true);
		} catch (const ChecksumError &e) {
			std::cerr << "decoder::ReadRecord threw exception " << e.what() << std::endl;
		}

		setScrollOffset(decoder->GetIndex());
	}

	void ScanForCarrier(TapeIndex idx) {
		try {
			int bitRate = 4800;
			auto result = audio->ScanForCarrier(idx, 200, bitRate);
			// result.first is where we first found the carrier
			idx = result.second;
		} catch (const ChecksumError &e) {
			std::cerr << "decoder::ReadRecord threw exception " << e.what() << std::endl;
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
	std::shared_ptr<PolyAudioTapeDecoder> decoder;
	AudioPtr audio;
	Tape *tape = nullptr;

	double xScale = 0.05;   // pixels per sample (start zoomed out)
	double yScale = 1.0;
	double scrollOffset = 0; // first sample visible at left edge

	bool dragging = false;
	double dragLastX = 0;
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
	QLabel *indexLabel = nullptr;
	QLabel *widthLabel = nullptr;
	QLabel *tapeFileLabel = nullptr;
	QLabel *recordNumberLabel = nullptr;
	QLabel *byteLabel = nullptr;
	QLabel *validityLabel = nullptr;
	QWidget *bottomPlaceholder = nullptr;

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

		// --- Middle pane: status labels ---
		auto *middleWidget = new QWidget(splitter);
		auto *middleLayout = new QHBoxLayout(middleWidget);
		middleLayout->setContentsMargins(8, 4, 8, 4);

		auto makeStatusPair = [&](const QString &labelText) -> QLabel * {
			auto *nameLabel = new QLabel(labelText + ":", middleWidget);
			nameLabel->setStyleSheet("font-weight: bold;");
			auto *valueLabel = new QLabel("—", middleWidget);
			middleLayout->addWidget(nameLabel);
			middleLayout->addWidget(valueLabel);
			middleLayout->addSpacing(16);
			return valueLabel;
		};

		indexLabel = makeStatusPair("Index");
		widthLabel = makeStatusPair("Width");
		tapeFileLabel = makeStatusPair("Tape File");
		recordNumberLabel = makeStatusPair("Record Number");
		byteLabel = makeStatusPair("Byte");

		validityLabel = new QLabel("—", middleWidget);
		auto *validTitleLabel = new QLabel("Status:", middleWidget);
		validTitleLabel->setStyleSheet("font-weight: bold;");
		middleLayout->addWidget(validTitleLabel);
		middleLayout->addWidget(validityLabel);

		middleLayout->addStretch();
		middleWidget->setMaximumHeight(40);

		// --- Bottom pane: placeholder ---
		bottomPlaceholder = new QWidget(splitter);
		auto *bottomLayout = new QVBoxLayout(bottomPlaceholder);
		auto *placeholderLabel = new QLabel(
			"Tape files / records / byte layout will appear here",
			bottomPlaceholder);
		placeholderLabel->setAlignment(Qt::AlignCenter);
		placeholderLabel->setStyleSheet("color: gray;");
		bottomLayout->addWidget(placeholderLabel);
		bottomPlaceholder->setMinimumHeight(100);

		// Set splitter proportions
		splitter->addWidget(topWidget);
		splitter->addWidget(middleWidget);
		splitter->addWidget(bottomPlaceholder);
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

	void onLoad() {
		QString fileName = QFileDialog::getOpenFileName(
			this, "Open WAV File", QString(),
			"WAV files (*.wav);;All files (*)");
		if (fileName.isEmpty()) return;

		try {
			audioPtr = std::make_shared<Audio>(fileName.toStdString());
			waveformView->setAudio(audioPtr);
			auto decoder = std::make_shared<PolyAudioTapeDecoder>(audioPtr);
			waveformView->setDecoder(decoder);
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
		QMessageBox::information(this, "Save", "Save is not yet implemented.");
	}

	void onSettings() {
		SettingsDialog dlg(settings, this);
		dlg.exec();
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
