#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
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
#include <QLineEdit>
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
#include "KansasCity.hpp"
#include "PolyPhase.hpp"
#include "Tape.hpp"

// Wave file handler
// bit finder
// E6 finder
// record finder/verifier
// record spans a set of wave file indeces, allow interfactive editing or auto correction

// ---------------------------------------------------------------------------
// MainWindowSettings - holds user-configurable settings
// ---------------------------------------------------------------------------
enum class TapeFormat {
	PolyPhase = 0,
	KansasCity = 1
};

struct MainWindowSettings {
	bool invertSignal = false;
	int bitrate = 2400;
	int hysterisis = 200;	// used by polyphase, not Byte
	TapeFormat tapeFormat = TapeFormat::PolyPhase;
	// Curve drag interpolation range, as a fraction of one bit-cell cycle.
	// 0.25 = 1/4 cycle.  Valid range roughly 0.1 .. 1.0.
	double curveDragRange = 0.25;
	bool invertMouseWheelScroll = false;
	bool autoRepairHeaderLength = true;
	int dcOffset = 0;
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
		bitrateSpin->setRange(200, 10000);
		bitrateSpin->setValue(static_cast<int>(settingsRef.bitrate));
		layout->addRow("Bitrate", bitrateSpin);

		hysterisisSpin = new QSpinBox(this);
		hysterisisSpin->setRange(0, 16384);
		hysterisisSpin->setValue(static_cast<int>(settingsRef.hysterisis));
		layout->addRow("Hysterisis", hysterisisSpin);

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

		dcOffsetSpin = new QSpinBox(this);
		dcOffsetSpin->setRange(-32768, 32767);
		dcOffsetSpin->setValue(settingsRef.dcOffset);
		layout->addRow("DC Offset", dcOffsetSpin);

		auto *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		layout->addRow(buttons);

		connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
	}

	void accept() override {
		settingsRef.invertSignal = invertSignalCheckBox->isChecked();
		settingsRef.bitrate = static_cast<int>(bitrateSpin->value());
		settingsRef.hysterisis = static_cast<int>(hysterisisSpin->value());
		settingsRef.tapeFormat = static_cast<TapeFormat>(tapeFormatCombo->currentIndex());
		settingsRef.curveDragRange = curveDragRangeSpin->value();
		settingsRef.invertMouseWheelScroll = invertMouseWheelScrollCheckBox->isChecked();
		settingsRef.autoRepairHeaderLength = autoRepairHeaderLengthCheckBox->isChecked();
		settingsRef.dcOffset = dcOffsetSpin->value();
		QDialog::accept();
	}

private:
	MainWindowSettings &settingsRef;
	QCheckBox *invertSignalCheckBox;
	QSpinBox *bitrateSpin;
	QSpinBox *hysterisisSpin;
	QComboBox *tapeFormatCombo;
	QDoubleSpinBox *curveDragRangeSpin;
	QCheckBox *invertMouseWheelScrollCheckBox;
	QCheckBox *autoRepairHeaderLengthCheckBox;
	QSpinBox *dcOffsetSpin;
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

	void SetDataInterface(DataInterfacePtr dataInterface) {
		this->dataInterface = dataInterface;
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
	DataInterfacePtr getDecoder() const { return dataInterface; }
	const WaveformSelection &getSelection() const { return selection; }

	void clearSelection() {
		selection = WaveformSelection();
		update();
		emit selectionChanged(selection);
	}

	void computeSelection(TapeIndex startIdx, bool rewind = true) {
		if (!dataInterface) return;
		selection = WaveformSelection();
		try {
			if (rewind) {
				// reset "last bit" to zero (required if
				// we randomly changed startindex).
				dataInterface->Rewind();
			}
			selection.byte1 = dataInterface->ReadByteWithBits(startIdx);
			selection.byte2 = dataInterface->ReadByteWithBits(selection.byte1.endIndex);
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
	void findRequested();
	void findRepeatRequested(int direction);

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
					QColor color =  colorForFieldType(tb->fieldType);
					p.setPen(color);

					// Taller ticks for SOH, header checksum, data checksum,
					// or low-confidence bytes
					bool isBoundary = (tb->fieldType == FieldType::SOH ||
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

					QColor color = colorForFieldType(tb->fieldType);
					color.setAlpha(120);
					QPen pen(color, 2);
					p.setPen(pen);

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
		if (selectionDragging && audio) {
			TapeIndex samp = pixelToSample(event->position().x());
			computeSelection(samp);
		} else if (curveDragging && audio) {
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
				// Ctrl+drag: set waveform selection start and begin drag
				TapeIndex clickSample = pixelToSample(
					event->position().x());
				computeSelection(clickSample);
				selectionDragging = true;
				setCursor(Qt::SizeHorCursor);
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
			if (selectionDragging) {
				selectionDragging = false;
			}
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

	void keyPressEvent(QKeyEvent *event) override {
		Qt::KeyboardModifiers mods = event->modifiers() &
			(Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
		bool ctrlOnly = (mods == Qt::ControlModifier);

		if (event->key() == Qt::Key_F && ctrlOnly) {
			emit findRequested();
			event->accept();
			return;
		} else if (event->key() == Qt::Key_R && ctrlOnly) {
			emit findRepeatRequested(1); // repeat forward
			event->accept();
			return;
		}

		if (!audio || !selection.active) {
			QWidget::keyPressEvent(event);
			return;
		}

		bool noMods = (mods == Qt::NoModifier);

		auto scrollToFollow = [&](double idx) {
			double px = sampleToPixel(idx);
			if (px < 0 || px > width()) {
				setScrollOffset(idx - visibleSamples() * 0.25);
			}
		};

		if (event->key() == Qt::Key_Right && noMods) {
			// Move selection to next zero crossing
			try {
				TapeIndex newIdx = audio->FindThisOrNextTransition(
					selection.startIndex + 1);
				computeSelection(newIdx);
				scrollToFollow(newIdx);
			} catch (...) {}
			event->accept();
			return;
		} else if (event->key() == Qt::Key_Left && noMods) {
			// Move selection to previous zero crossing
			if (selection.startIndex < 1) {
				event->accept();
				return;
			}
			try {
				TapeIndex newIdx = audio->FindThisOrPreviousTransition(
					selection.startIndex - 1);
				computeSelection(newIdx);
				scrollToFollow(newIdx);
			} catch (...) {}
			event->accept();
			return;
		} else if (event->key() == Qt::Key_Right && ctrlOnly) {
			// Move selection right by one byte boundary
			TapeIndex newIdx = selection.byte1.endIndex;
			computeSelection(newIdx);
			scrollToFollow(newIdx);
			event->accept();
			return;
		} else if (event->key() == Qt::Key_Left && ctrlOnly) {
			// Move selection left by approximately one byte (samplesPerBit heuristic)
			double samplesPerBit = (settings && settings->bitrate > 0)
				? static_cast<double>(audio->SampleRate()) / settings->bitrate
				: 80.0;
			TapeIndex delta = samplesPerBit * 8;
			TapeIndex newIdx = std::max(0.0, selection.startIndex - delta);
			try {
				computeSelection(newIdx);
				scrollToFollow(newIdx);
			} catch (...) {}
			event->accept();
			return;
		}

		QWidget::keyPressEvent(event);
	}

private slots:
	void showContextMenu(const QPoint &pos) {
		TapeIndex idx = pixelToSample(pos.x());

		QMenu contextMenu(this);

		QAction *scanForRecordAction = contextMenu.addAction("Scan For Record");
		QAction *scanAllFromHereAction = contextMenu.addAction("Scan All From Here");
		QAction *getBitrateEstimate = contextMenu.addAction("Get Bitrate Estimate");

		connect(scanForRecordAction, &QAction::triggered, this,
			[this, idx]() { ScanForRecord(idx); });
		connect(scanAllFromHereAction, &QAction::triggered, this,
			[this, idx]() { ScanAllFromHere(idx); });
		connect(getBitrateEstimate, &QAction::triggered, this,
			[this, idx]() { GetBitrateEstimate(idx); });

		contextMenu.exec(mapToGlobal(pos));
	}

	void ScanForRecord(TapeIndex idx) {
		if (!dataInterface || !tape) return;

		Record record;
		if (settings) record.SetRepairDataLength(settings->autoRepairHeaderLength);
		auto [nextIdx, status] = record.ReadFromDecoder(dataInterface, idx);

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
					// Compare byte-by-byte to see if the rescan differs
					auto oldBytes = existing.GetAllBytes();
					auto newBytes = record.GetAllBytes();
					int diffCount = 0;
					size_t minLen = std::min(oldBytes.size(), newBytes.size());
					for (size_t i = 0; i < minLen; i++) {
						if (oldBytes[i]->value != newBytes[i]->value)
							diffCount++;
					}
					diffCount += static_cast<int>(
						std::max(oldBytes.size(), newBytes.size()) - minLen);

					if (diffCount > 0) {
						existing = std::move(record);
						update();
						emit tapeDataChanged();
						emit statusMessage(QString(
							"Rescan updated record %1 at %2 (width %3) in file %4 — "
							"%5 byte(s) changed")
							.arg(static_cast<qint64>(existing.GetRecordNumber()))
							.arg(static_cast<qint64>(sohIdx))
							.arg(width)
							.arg(QString::fromStdString(existing.GetName()).trimmed())
							.arg(diffCount));
					} else {
						emit statusMessage(QString(
							"Rescan of record %1 at %2 (width %3) in file %4 — "
							"rescanned record is identical")
							.arg(static_cast<qint64>(record.GetRecordNumber()))
							.arg(static_cast<qint64>(sohIdx))
							.arg(width)
							.arg(QString::fromStdString(existing.GetName()).trimmed()));
					}
					return;
				}
			}
		}

		// Insert the record into the tape, grouped by file name.
		// A tape may have multiple File objects with the same name
		// (duplicate copies), so find the best-fit File by tape position.
		std::string recName = record.GetName();
		uint16_t recNum = record.GetRecordNumber();
		bool hdrOk = record.HeaderChecksumIsValid();
		File *targetFile = nullptr;
		for (auto &file : tape->GetFiles()) {
			auto &recs = file.GetRecords();
			if (recs.empty() || recs[0].GetName() != recName) continue;

			// Check if this record would naturally follow the last
			// record in this file (by tape position)
			auto &lastRec = recs.back();
			if (sohIdx > lastRec.GetSOHIndex()) {
				// Would append after the last record — but check for
				// record number reset/decrease indicating a new copy
				if (hdrOk && recNum == 0 && lastRec.GetRecordNumber() > 0) {
					continue; // skip, needs a new File
				}
				if (hdrOk && lastRec.HeaderChecksumIsValid() &&
					recNum < lastRec.GetRecordNumber()) {
					continue; // skip, needs a new File
				}
			}
			targetFile = &file;
			break;
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
		emit statusMessage(QString("New %1 tape record %2 found at %3 (width %4)")
			.arg(QString::fromStdString(record.GetName()).trimmed())
			.arg(static_cast<qint64>(record.GetRecordNumber()))
			.arg(static_cast<qint64>(sohIdx)).arg(width));
	}

	void ScanAllFromHere(TapeIndex idx) {
		if (!dataInterface || !tape || !audio) return;

		QApplication::setOverrideCursor(Qt::WaitCursor);
		tape->SetScanDecoder(dataInterface);
		tape->SetRepairDataLength(settings && settings->autoRepairHeaderLength);
		tape->Scan(idx, audio->SampleCount(),
			[this](const std::string &fileName, uint16_t recNum, int found, int errors) {
				emit statusMessage(QString("Scanning: %1 record %2 (%3 found, %4 errors)")
					.arg(QString::fromStdString(fileName).trimmed())
					.arg(recNum)
					.arg(found)
					.arg(errors));
				QApplication::processEvents();
			});

		QApplication::restoreOverrideCursor();
		update();
		emit tapeDataChanged();
	}

	void GetBitrateEstimate(TapeIndex tapeIndex) {
		if (!dataInterface || !tape || !audio) return;

		auto bitRate = dataInterface->GetBitrateEstimate(tapeIndex, 10);

		emit statusMessage(QString("Estimated Bitrate: %1 (from 10 sequential waveform samples)").arg(bitRate));
	}

private:
	DataInterfacePtr dataInterface;
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

	// Waveform selection state (Ctrl+drag)
	WaveformSelection selection;
	bool selectionDragging = false;

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
// FindDialog - modeless search dialog for binary byte patterns
// ---------------------------------------------------------------------------
class FindDialog : public QDialog {
	Q_OBJECT
public:
	FindDialog(QWidget *parent = nullptr)
		: QDialog(parent)
	{
		setWindowTitle("Find");
		setMinimumWidth(420);
		auto *layout = new QVBoxLayout(this);

		// Search string input (accepts binary/ASCII characters)
		layout->addWidget(new QLabel("Search string (binary-safe):"));
		searchEdit = new QLineEdit(this);
		searchEdit->setFont(QFont("Monospace", 10));
		layout->addWidget(searchEdit);

		// Hex display (read-only)
		layout->addWidget(new QLabel("Hex:"));
		hexDisplay = new QLineEdit(this);
		hexDisplay->setReadOnly(true);
		hexDisplay->setFont(QFont("Monospace", 10));
		hexDisplay->setStyleSheet("background-color: #2a2a2a; color: #88cc88;");
		layout->addWidget(hexDisplay);

		connect(searchEdit, &QLineEdit::textChanged, this, &FindDialog::updateHexDisplay);

		// Special byte buttons
		auto *btnRow = new QHBoxLayout();
		auto *leaderBtn = new QPushButton("Leader byte: 0xE6", this);
		auto *sohBtn = new QPushButton("Start of Header byte: 0x01", this);
		btnRow->addWidget(leaderBtn);
		btnRow->addWidget(sohBtn);
		layout->addLayout(btnRow);

		connect(leaderBtn, &QPushButton::clicked, this, [this]() {
			appendByte(0xE6);
		});
		connect(sohBtn, &QPushButton::clicked, this, [this]() {
			appendByte(0x01);
		});

		// Search width
		auto *widthRow = new QHBoxLayout();
		widthRow->addWidget(new QLabel("Search width (samples):"));
		searchWidthSpin = new QSpinBox(this);
		searchWidthSpin->setRange(100, 10000000);
		searchWidthSpin->setValue(10000);
		searchWidthSpin->setSingleStep(1000);
		widthRow->addWidget(searchWidthSpin);
		layout->addLayout(widthRow);

		// Search direction buttons
		auto *searchRow = new QHBoxLayout();
		auto *searchLeftBtn = new QPushButton("\u25C0 Search Left", this);
		auto *searchRightBtn = new QPushButton("Search Right \u25B6", this);
		searchLeftBtn->setMinimumHeight(32);
		searchRightBtn->setMinimumHeight(32);
		searchRow->addWidget(searchLeftBtn);
		searchRow->addWidget(searchRightBtn);
		layout->addLayout(searchRow);

		connect(searchLeftBtn, &QPushButton::clicked, this, [this]() {
			emit searchTriggered(-1);
		});
		connect(searchRightBtn, &QPushButton::clicked, this, [this]() {
			emit searchTriggered(1);
		});
	}

	// Get the search pattern as raw bytes
	QByteArray getSearchBytes() const {
		QString text = searchEdit->text();
		QByteArray result;
		for (int i = 0; i < text.size(); i++) {
			result.append(static_cast<char>(text[i].unicode() & 0xFF));
		}
		return result;
	}

	int getSearchWidth() const {
		return searchWidthSpin->value();
	}

	// Set the search string (used to persist across popups)
	void setSearchString(const QString &s) {
		searchEdit->setText(s);
	}

	QString getSearchString() const {
		return searchEdit->text();
	}

signals:
	void searchTriggered(int direction); // -1 = left, +1 = right

private:
	QLineEdit *searchEdit;
	QLineEdit *hexDisplay;
	QSpinBox *searchWidthSpin;

	void appendByte(uint8_t byte) {
		QString text = searchEdit->text();
		text.append(QChar(byte));
		searchEdit->setText(text);
	}

	void updateHexDisplay() {
		QByteArray bytes = getSearchBytes();
		QString hex;
		for (int i = 0; i < bytes.size(); i++) {
			if (i > 0) hex += ' ';
			hex += QString("%1").arg(
				static_cast<uint8_t>(bytes[i]), 2, 16, QChar('0')).toUpper();
		}
		hexDisplay->setText(hex);
	}
};

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow(QWidget *parent = nullptr, QString fileName = "") : QMainWindow(parent) {
		setWindowTitle("PolyWaveToImage");
		resize(1200, 800);

		scrollTimer = new QTimer(this);
		scrollTimer->setInterval(50);
		connect(scrollTimer, &QTimer::timeout, this, &MainWindow::onScrollTimerTick);

		buildMenus();
		buildUI();

		statusBar()->showMessage("Ready");

		if (fileName.size()) {
			LoadAudioFile(fileName);
		}
	}

	AudioPtr audioPtr;
	DataInterfacePtr dataInterface;

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
	QDialog *quickHelpDialog = nullptr;
	int scrollDirection = 0;    // -1 = left, +1 = right, 0 = stopped
	int scrollTickCount = 0;

	// Find dialog state
	FindDialog *findDialog = nullptr;
	QString persistentSearchString;
	int lastSearchDirection = 1;
	int lastSearchWidth = 10000;

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
		QMenu *tapeMenu = menuBar()->addMenu("&Tape");

		QAction *scanAllAction = tapeMenu->addAction("Scan &All Records");
		scanAllAction->setShortcut(QKeySequence("Ctrl+A"));
		connect(scanAllAction, &QAction::triggered, this, &MainWindow::onScanAll);

		QAction *findAction = tapeMenu->addAction("&Find");
		findAction->setShortcut(QKeySequence("Ctrl+F"));
		connect(findAction, &QAction::triggered, this, &MainWindow::onFind);

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
		connect(waveformView, &WaveformView::findRequested,
			this, &MainWindow::onFind);
		connect(waveformView, &WaveformView::findRepeatRequested,
			this, [this](int dir) { performSearch(dir); });

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

		recordTable->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(recordTable, &QWidget::customContextMenuRequested,
			this, &MainWindow::onRecordTableContextMenu);
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

		// Style the splitter handles so they are visible and draggable
		splitter->setHandleWidth(5);
		splitter->setStyleSheet(
			"QSplitter::handle:vertical {"
			"  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
			"    stop:0 #444, stop:0.5 #666, stop:1 #444);"
			"  height: 5px;"
			"}"
			"QSplitter::handle:vertical:hover {"
			"  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
			"    stop:0 #555, stop:0.5 #888, stop:1 #555);"
			"}"
		);

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

	// interface factory
	DataInterfacePtr CreateDataInterface() {
		if (!audioPtr) return nullptr;
		if (settings.tapeFormat == TapeFormat::KansasCity) {
			return std::make_shared<KansasCity>(audioPtr, settings.bitrate, settings.hysterisis);
		} else {
			return std::make_shared<PolyPhase>(audioPtr, settings.bitrate, settings.hysterisis);
		}
	}

	void refreshRecordTable() {
		if (!recordTable) return;
		recordTable->setRowCount(0);

		int row = 0;
		for (auto &file : tape.GetFiles()) {
			int lastRecordNumber = -1;
			for (auto &record : file.GetRecords()) {
				recordTable->insertRow(row);

				auto setItem = [&](int col, const QString &text) {
					auto *item = new QTableWidgetItem(text);
					item->setTextAlignment(Qt::AlignCenter);
					recordTable->setItem(row, col, item);
				};

				std::string recordName;
				if (record.GetRecordNumber() != lastRecordNumber + 1) {
					recordName = "*";
				}
				lastRecordNumber = record.GetRecordNumber();
				recordName += record.GetName();
				setItem(0, QString::fromStdString(recordName));
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

	void onRecordTableContextMenu(const QPoint &pos) {
		QModelIndex index = recordTable->indexAt(pos);
		if (!index.isValid()) return;
		int row = index.row();

		QMenu contextMenu(recordTable);
		QAction *deleteAction = contextMenu.addAction("Delete");
		QAction *saveAction = contextMenu.addAction("Save Tape File");
		QAction *exportCasAction = contextMenu.addAction("Export Tape File");

		QAction *chosen = contextMenu.exec(recordTable->viewport()->mapToGlobal(pos));
		if (chosen == deleteAction) {
			deleteRecordAtRow(row);
		} else if (chosen == saveAction) {
			saveTapeFileFromRow(row);
		} else if (chosen == exportCasAction) {
			exportCasFileFromRow(row);
		}
	}

	void deleteRecordAtRow(int row) {
		int idx = 0;
		for (auto &file : tape.GetFiles()) {
			auto &recs = file.GetRecords();
			for (auto it = recs.begin(); it != recs.end(); ++it) {
				if (idx == row) {
					recs.erase(it);
					// Remove empty files
					auto &files = tape.GetFiles();
					files.erase(
						std::remove_if(files.begin(), files.end(),
							[](File &f) { return f.GetRecords().empty(); }),
						files.end());
					waveformView->update();
					refreshRecordTable();
					return;
				}
				idx++;
			}
		}
	}

	// Find the file index and record index within that file for a given table row
	bool findRecordByRow(int row, int &fileIdx, int &recIdx) {
		int idx = 0;
		fileIdx = 0;
		for (auto &file : tape.GetFiles()) {
			recIdx = 0;
			for (auto &record : file.GetRecords()) {
				if (idx == row) return true;
				idx++;
				recIdx++;
			}
			fileIdx++;
		}
		return false;
	}

	void saveTapeFileFromRow(int row) {
		int fileIdx, recIdx;
		if (!findRecordByRow(row, fileIdx, recIdx)) return;

		// Pop up a directory selection dialog
		QString dirPath = QFileDialog::getExistingDirectory(
			this, "Select Output Directory", QString(),
			QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
		if (dirPath.isEmpty()) return;

		namespace fs = std::filesystem;
		fs::path outDir(dirPath.toStdString());
		if (!fs::exists(outDir)) {
			fs::create_directories(outDir);
		}

		auto &files = tape.GetFiles();
		auto &recs = files[fileIdx].GetRecords();
		Record &firstRecord = recs[recIdx];
		std::string tapeFileName = firstRecord.GetName();

		// Trim trailing spaces from the tape file name
		while (!tapeFileName.empty() && tapeFileName.back() == ' ')
			tapeFileName.pop_back();

		int writtenCount = 0;

		for (size_t ri = recIdx; ri < recs.size(); ri++) {
			Record &record = recs[ri];

			// After the first record, check continuity
			if (ri > static_cast<size_t>(recIdx)) {
				std::string recName = record.GetName();
				if (recName != firstRecord.GetName()) break;
				if (record.GetRecordNumber() <= recs[ri - 1].GetRecordNumber()) break;
			}

			// Build the output filename: {tapeFileName}-{recordNumber}-{seq}
			uint16_t recordNumber = record.GetRecordNumber();
			int seq = 1;
			fs::path outPath;
			while (true) {
				std::string fname = std::format("{}-{}-{}", tapeFileName, recordNumber, seq);
				outPath = outDir / fname;
				if (!fs::exists(outPath)) break;
				seq++;
			}

			// Write the file
			std::ofstream ofs(outPath, std::ios::binary);
			if (!ofs) {
				statusBar()->showMessage(
					QString("Failed to create file: %1")
					.arg(QString::fromStdString(outPath.string())));
				return;
			}

			// Line 1: ASCII header
			ofs << record.GetHeaderAsAscii() << "\n";

			// Line 2: hex dump of data bytes (if record has data content)
			if (record.HasDataContent()) {
				auto &dataBytes = record.GetData();
				for (size_t i = 0; i < dataBytes.size(); i++) {
					if (dataBytes[i].value) {
						char buf[4];
						snprintf(buf, sizeof(buf), "%02X ", *(dataBytes[i].value));
						ofs << buf;
					} else {
						ofs << "?? ";
					}
				}
				ofs << "\n";

				// Line 3+: raw binary data
				for (size_t i = 0; i < dataBytes.size(); i++) {
					if (dataBytes[i].value) {
						uint8_t byte = *(dataBytes[i].value);
						ofs.write(reinterpret_cast<const char *>(&byte), 1);
					} else {
						uint8_t zero = 0;
						ofs.write(reinterpret_cast<const char *>(&zero), 1);
					}
				}
			}

			ofs.close();
			writtenCount++;
		}

		statusBar()->showMessage(
			QString("Saved %1 record(s) to %2")
			.arg(writtenCount).arg(dirPath));
	}

	void exportCasFileFromRow(int row) {
		int fileIdx, recIdx;
		if (!findRecordByRow(row, fileIdx, recIdx)) return;

		auto &files = tape.GetFiles();
		auto &recs = files[fileIdx].GetRecords();

		// Get the tape file name from record 0 of this file
		std::string tapeFileName = recs[0].GetName();
		std::string trimmedName = tapeFileName;
		while (!trimmedName.empty() && trimmedName.back() == ' ')
			trimmedName.pop_back();

		// --- Gather the contiguous run of records starting from record 0 ---
		std::vector<Record *> casRecords;
		for (size_t i = 0; i < recs.size(); i++) {
			if (recs[i].GetName() == tapeFileName) {
				casRecords.push_back(&recs[i]);
			}
		}

		// --- Validate the record set ---
		QStringList warnings;
		bool hasEndRecord = false;
		int expectedRecNum = 0;

		for (size_t i = 0; i < casRecords.size(); i++) {
			Record *r = casRecords[i];
			int recNum = r->GetRecordNumber();

			// Check sequential record numbers starting at 0
			if (recNum != expectedRecNum) {
				warnings << QString("Record number %1 found, expected %2")
					.arg(recNum).arg(expectedRecNum);
			}
			expectedRecNum = recNum + 1;

			// Check name consistency
			if (r->GetName() != tapeFileName) {
				warnings << QString("Record %1 has name '%2', expected '%3'")
					.arg(recNum)
					.arg(QString::fromStdString(r->GetName()).trimmed())
					.arg(QString::fromStdString(trimmedName));
			}

			// Check for End record
			if (r->GetTypeValue() == 0x02) {
				hasEndRecord = true;
				if (i != casRecords.size() - 1) {
					warnings << QString("End record at position %1 is not the last record")
						.arg(static_cast<int>(i));
				}
			}

			// Check data length: data/binary records (not last) should be 256
			uint8_t rtype = r->GetTypeValue();
			bool lengthExempt = (rtype == 0x01 || rtype == 0x02 || rtype == 0x03);
			if (!lengthExempt && i < casRecords.size() - 1) {
				if (r->GetDataLength() != 256) {
					warnings << QString("Record %1 has length %2, expected 256")
						.arg(recNum).arg(r->GetDataLength());
				}
			}

			// Checksum warnings (soft errors)
			if (!r->HeaderChecksumIsValid()) {
				warnings << QString("Record %1 has header checksum error").arg(recNum);
			}
			if (!r->DataChecksumIsValid()) {
				warnings << QString("Record %1 has data checksum error").arg(recNum);
			}
		}

		if (!hasEndRecord) {
			warnings << "No End record found";
		}

		// --- Show validation dialog ---
		QDialog validationDlg(this);
		validationDlg.setWindowTitle("Export CAS File - Validation");
		auto *vLayout = new QVBoxLayout(&validationDlg);

		vLayout->addWidget(new QLabel(
			QString("Tape file: <b>%1</b> (%2 records)")
			.arg(QString::fromStdString(trimmedName))
			.arg(casRecords.size())));

		if (warnings.isEmpty()) {
			vLayout->addWidget(new QLabel("All checks passed."));
		} else {
			auto *warnLabel = new QLabel("Warnings:");
			warnLabel->setStyleSheet("color: #ff5050; font-weight: bold;");
			vLayout->addWidget(warnLabel);
			auto *warnList = new QTextEdit(&validationDlg);
			warnList->setReadOnly(true);
			warnList->setPlainText(warnings.join("\n"));
			warnList->setMaximumHeight(150);
			vLayout->addWidget(warnList);
		}

		auto *exportAsCasCheckBox = new QCheckBox("Export as CAS file", &validationDlg);
		exportAsCasCheckBox->setChecked(true);
		vLayout->addWidget(exportAsCasCheckBox);

		QCheckBox *createEndCheckBox = nullptr;
		if (!hasEndRecord) {
			createEndCheckBox = new QCheckBox("Create missing end record?", &validationDlg);
			createEndCheckBox->setChecked(true);
			vLayout->addWidget(createEndCheckBox);
		}

		auto *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &validationDlg);
		vLayout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &validationDlg, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &validationDlg, &QDialog::reject);

		if (validationDlg.exec() != QDialog::Accepted) return;

		bool createEndRecord = createEndCheckBox && createEndCheckBox->isChecked();
		bool exportAsCas = exportAsCasCheckBox->isChecked();

		// --- File save dialog ---
		QString defaultName = QString::fromStdString(trimmedName)
			+ (exportAsCas ? ".CAS" : "");
		QString filter = exportAsCas
			? "CAS Files (*.CAS *.cas);;All Files (*)"
			: "All Files (*)";
		QString filePath = QFileDialog::getSaveFileName(
			this, "Export Tape File", defaultName, filter);
		if (filePath.isEmpty()) return;

		// --- Write the file ---
		std::ofstream ofs(filePath.toStdString(), std::ios::binary);
		if (!ofs) {
			QMessageBox::critical(this, "Export Tape File",
				QString("Failed to create file: %1").arg(filePath));
			return;
		}

		int recordsWritten = 0;
		int totalDataBytes = 0;

		auto writeRecordBinary = [&](Record *r) {
			if (exportAsCas) {
				// Write 16 bytes of 0xe6 leader
				uint8_t leader = 0xe6;
				for (int i = 0; i < 16; i++)
					ofs.write(reinterpret_cast<const char *>(&leader), 1);

				// Write SOH byte (0x01)
				uint8_t sohByte = 0x01;
				ofs.write(reinterpret_cast<const char *>(&sohByte), 1);

				// Write 14 header bytes + header checksum
				auto allBytes = r->GetAllBytes();
				size_t leaderCount = 0;
				for (auto *tb : allBytes) {
					if (tb->fieldType == FieldType::Leader) leaderCount++;
					else break;
				}
				size_t hdrStart = leaderCount + 1; // skip soh
				for (size_t i = hdrStart; i < hdrStart + 14; i++) {
					uint8_t b = allBytes[i]->value ? *(allBytes[i]->value) : 0;
					ofs.write(reinterpret_cast<const char *>(&b), 1);
				}
				{
					uint8_t b = allBytes[hdrStart + 14]->value
						? *(allBytes[hdrStart + 14]->value) : 0;
					ofs.write(reinterpret_cast<const char *>(&b), 1);
				}
			}

			// Write data bytes
			auto &dataBytes = r->GetData();
			int dataLen = static_cast<int>(dataBytes.size());
			for (int i = 0; i < dataLen; i++) {
				uint8_t b = dataBytes[i].value ? *(dataBytes[i].value) : 0;
				ofs.write(reinterpret_cast<const char *>(&b), 1);
			}
			totalDataBytes += dataLen;

			if (exportAsCas) {
				// Write data checksum
				auto allBytes = r->GetAllBytes();
				uint8_t b = allBytes.back()->value
					? *(allBytes.back()->value) : 0;
				ofs.write(reinterpret_cast<const char *>(&b), 1);
			}

			recordsWritten++;
		};

		bool wroteEndRecord = false;
		int prevRecNum = -1;
		for (auto *r : casRecords) {
			int recNum = r->GetRecordNumber();
			if (prevRecNum >= 0 && recNum <= prevRecNum) {
				// Record number decreased — duplicate copy, stop
				break;
			}
			writeRecordBinary(r);
			prevRecNum = recNum;
			if (r->GetTypeValue() == 0x02) {
				wroteEndRecord = true;
				break;
			}
		}

		// Create a synthetic End record if requested (CAS mode only)
		if (exportAsCas && createEndRecord && !wroteEndRecord) {
			// Write 16 bytes of 0xe6 leader
			uint8_t leader = 0xe6;
			for (int i = 0; i < 16; i++)
				ofs.write(reinterpret_cast<const char *>(&leader), 1);

			// Write SOH
			uint8_t sohByte = 0x01;
			ofs.write(reinterpret_cast<const char *>(&sohByte), 1);

			// Build a 14-byte header + checksum for End record
			uint8_t endHeader[14] = {};
			// Copy the tape file name (8 bytes, space-padded)
			for (int i = 0; i < 8; i++) {
				endHeader[i] = (i < static_cast<int>(tapeFileName.size()))
					? static_cast<uint8_t>(tapeFileName[i]) : ' ';
			}
			// Record number = next after the last one written
			uint16_t nextRecNum = 0;
			if (!casRecords.empty())
				nextRecNum = casRecords.back()->GetRecordNumber() + 1;
			endHeader[8] = static_cast<uint8_t>(nextRecNum & 0xff);
			endHeader[9] = static_cast<uint8_t>((nextRecNum >> 8) & 0xff);
			// Length = 0
			endHeader[10] = 0;
			// Address = 0
			endHeader[11] = 0;
			endHeader[12] = 0;
			// Type = End (0x02)
			endHeader[13] = 0x02;

			// Compute header checksum (two's complement so sum of all + checksum = 0)
			uint8_t hdrSum = 0;
			for (int i = 0; i < 14; i++) hdrSum += endHeader[i];
			uint8_t hdrCS = static_cast<uint8_t>(-hdrSum);

			ofs.write(reinterpret_cast<const char *>(endHeader), 14);
			ofs.write(reinterpret_cast<const char *>(&hdrCS), 1);

			// End records have no data, but write a zero data checksum
			uint8_t dataCS = 0;
			ofs.write(reinterpret_cast<const char *>(&dataCS), 1);

			recordsWritten++;
		}

		ofs.close();

		QMessageBox::information(this, "Export Tape File",
			QString("Export complete: %1 record(s) written, %2 data bytes written to\n%3")
			.arg(recordsWritten).arg(totalDataBytes).arg(filePath));
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

	void LoadAudioFile(QString fileName) {
		try {
			tape.GetFiles().clear();
			refreshRecordTable();
			audioPtr = std::make_shared<Audio>(fileName.toStdString());
			dataInterface = CreateDataInterface();
			applyAudioSettings();
			waveformView->setAudio(audioPtr);
			waveformView->SetDataInterface(dataInterface);
			waveformView->setTape(&tape);
			statusBar()->showMessage(
				"Loaded: " + fileName +
				" (" + QString::number(audioPtr->SampleCount()) + " samples)");
		} catch (const std::exception &e) {
			QMessageBox::critical(this, "Error loading file",
				QString::fromStdString(e.what()));
		}
	}

	void onLoad() {
		QString fileName = QFileDialog::getOpenFileName(
			this, "Open WAV File", QString(),
			"WAV files (*.wav);;All files (*)");
		if (fileName.isEmpty()) return;

		LoadAudioFile(fileName);
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

	void applyAudioSettings() {
		if (!audioPtr) return;
		audioPtr->SetInvertPhase(settings.invertSignal);
		audioPtr->SetDCOffset(settings.dcOffset);
	}

	void onSettings() {
		TapeFormat previousFormat = settings.tapeFormat;
		SettingsDialog dlg(settings, this);
		if (dlg.exec() == QDialog::Accepted) {
			if (audioPtr) {
				applyAudioSettings();

				// Always construct a new data interface to
				// ensure it picks up all changed settings.
				dataInterface = CreateDataInterface();

				// give waveformview a copy of our interface pointer
				waveformView->SetDataInterface(dataInterface);

				// eye candy, isn't really all that useful
				if (settings.tapeFormat != previousFormat) {
					statusBar()->showMessage(
						QString("Tape format changed to %1")
						.arg(settings.tapeFormat == TapeFormat::KansasCity
							? "Kansas City Standard" : "Poly-88 Phase Encoding"));
				}

				waveformView->update();
			}
		}
	}

	void onScanAll() {
		if (!audioPtr) {
			QMessageBox::warning(this, "Scan", "No audio file loaded.");
			return;
		}

		QApplication::setOverrideCursor(Qt::WaitCursor);

		tape.SetScanDecoder(dataInterface);
		tape.SetRepairDataLength(settings.autoRepairHeaderLength);
		int recordCount = 0;
		int errorCount = 0;
		tape.Scan(0, audioPtr->SampleCount(),
			[this, &recordCount, &errorCount](
				const std::string &fileName, uint16_t recNum, int found, int errors) {
				recordCount = found;
				errorCount = errors;
				statusBar()->showMessage(
					QString("Scanning: %1 record %2 (%3 found, %4 errors)")
						.arg(QString::fromStdString(fileName).trimmed())
						.arg(recNum)
						.arg(found)
						.arg(errors));
				QApplication::processEvents();
			});

		QApplication::restoreOverrideCursor();
		waveformView->setTape(&tape);
		waveformView->update();
		refreshRecordTable();
		statusBar()->showMessage(
			QString("Scan complete: %1 records found, %2 errors")
			.arg(recordCount).arg(errorCount));
	}

	void onQuit() {
		close();
	}

	void onFind() {
		if (!findDialog) {
			findDialog = new FindDialog(this);
			findDialog->setAttribute(Qt::WA_DeleteOnClose);
			connect(findDialog, &QObject::destroyed, this, [this]() {
				findDialog = nullptr;
			});
			connect(findDialog, &FindDialog::searchTriggered,
				this, [this](int direction) {
					// Save state from dialog before searching
					persistentSearchString = findDialog->getSearchString();
					lastSearchWidth = findDialog->getSearchWidth();
					lastSearchDirection = direction;
					performSearch(direction);
				});
		}
		findDialog->setSearchString(persistentSearchString);
		findDialog->show();
		findDialog->raise();
		findDialog->activateWindow();
	}

	void performSearch(int direction) {
		if (!audioPtr || !waveformView) {
			statusBar()->showMessage("No audio file loaded.");
			return;
		}

		if (!dataInterface) {
			statusBar()->showMessage("No decoder available.");
			return;
		}

		// Get the search bytes from the persistent string
		QByteArray searchBytes;
		for (int i = 0; i < persistentSearchString.size(); i++) {
			searchBytes.append(
				static_cast<char>(persistentSearchString[i].unicode() & 0xFF));
		}

		if (searchBytes.isEmpty()) {
			statusBar()->showMessage("Search string is empty.");
			return;
		}

		// Determine start position: use selection if active, else scroll offset
		const auto &sel = waveformView->getSelection();
		TapeIndex startIdx;
		if (sel.active) {
			// Start one transition past current selection to avoid re-finding
			startIdx = sel.startIndex;
			try {
				if (direction > 0) {
					startIdx += audioPtr->SamplesPerBit(settings.bitrate);
				} else {
					startIdx -= audioPtr->SamplesPerBit(settings.bitrate);
				}
			} catch (...) {}
		} else {
			startIdx = waveformView->getScrollOffset();
		}

		int searchWidth = lastSearchWidth;
		TapeIndex limitIdx;
		if (direction > 0) {
			limitIdx = std::min(
				startIdx + searchWidth,
				static_cast<double>(audioPtr->SampleCount()));
		} else {
			limitIdx = std::max(startIdx - searchWidth, 0.0);
		}

		QApplication::setOverrideCursor(Qt::WaitCursor);
		statusBar()->showMessage(QString("Searching for %1 bytes...").arg(searchBytes.size()));
		QApplication::processEvents();

		int patternLen = searchBytes.size();
		TapeIndex idx = startIdx;
		bool found = false;
		TapeIndex foundIdx = 0;
		TapeIndex foundEndIdx = 0;

		while (true) {
			if (direction > 0 && idx >= limitIdx) break;
			if (direction < 0 && idx <= limitIdx) break;
			if (idx < 0 || idx >= audioPtr->SampleCount()) break;

			// Try to match the full pattern starting at idx
			bool match = true;
			TapeIndex readIdx = idx;
			TapeIndex matchStartIdx = idx;

			for (int p = 0; p < patternLen; p++) {
				try {
					auto [nextIdx, byteVal] = dataInterface->ReadByte(readIdx);
					uint8_t expected = static_cast<uint8_t>(searchBytes[p]);
					if (byteVal != expected) {
						match = false;
						break;
					}
					readIdx = nextIdx;
				} catch (...) {
					match = false;
					break;
				}
			}

			if (match) {
				found = true;
				foundIdx = matchStartIdx;
				foundEndIdx = readIdx;
				break;
			}

			try {
				if (direction > 0) {
					idx += audioPtr->SamplesPerBit(settings.bitrate) / 8.0;
				} else {
					if (idx < 1) break;
					idx -= audioPtr->SamplesPerBit(settings.bitrate) / 8.0;
				}
				dataInterface->Rewind();
			} catch (...) {
				break;
			}
		}

		QApplication::restoreOverrideCursor();

		if (found) {
			// Set the waveform selection to the found location
			waveformView->computeSelection(foundIdx);
			waveformView->setScrollOffset(
				foundIdx - waveformView->visibleSamples() * 0.25);
			waveformView->update();

			qint64 width = static_cast<qint64>(foundEndIdx - foundIdx);
			QString msg = QString("Found at %1 (width %2)")
				.arg(static_cast<qint64>(foundIdx)).arg(width);

			// Check if this is in a known record
			for (auto &file : tape.GetFiles()) {
				for (auto &record : file.GetRecords()) {
					if (record.ContainsIndex(foundIdx)) {
						msg += QString(" in file %1 record %2")
							.arg(QString::fromStdString(
								record.GetName()).trimmed())
							.arg(record.GetRecordNumber());
						break;
					}
				}
			}

			statusBar()->showMessage(msg);
		} else {
			statusBar()->showMessage("Search string not found");
		}
	}

	void onQuickHelp() {
		if (quickHelpDialog) {
			quickHelpDialog->raise();
			quickHelpDialog->activateWindow();
			return;
		}
		quickHelpDialog = new QDialog(this);
		quickHelpDialog->setWindowTitle("Quick Help");
		quickHelpDialog->resize(500, 400);
		quickHelpDialog->setAttribute(Qt::WA_DeleteOnClose);
		connect(quickHelpDialog, &QObject::destroyed, this, [this]() {
			quickHelpDialog = nullptr;
		});
		auto *layout = new QVBoxLayout(quickHelpDialog);
		auto *browser = new QTextBrowser(quickHelpDialog);
		browser->setHtml(R"(
<h3> Introduction </h3>

This program provides a means of examining and extracting digital data
from audio tapes written using a circa 1976 PolyMorphic-88 S-100 computer.
<br>
The audio files are binary encoded from binary in two basic ways: Kansas City Standard
and what they called Polyphase (which is just Manchester encoding).
<br>
To organize the data, each tape file is written as one or more records, each
of which has a header, a header checksum, data, and a data checksum.
<br>
The goal is to be able to preserve these audiotapes in both formats, first
by converting them from analog audio to mono 16 bit signed WAV files.
<br>
Once that is done, run this program, load that WAV file, and see what
you can find on the tape!
<br>

<h3> Menus </h3>

Waveform context menu

<ul>
<li> <b> scan for record </b> attempts to find a new record starting at mouse position </li>
<li> <b> scan all from here </b> (re)scans all records from this point on </li>
<li> <b> scan for carrier </b> search for strings of 0's </li>
</ul>

<br>
Scan for record is the one you want - the other two were bad experiments and
will be removed.

<h3> Mouse </h3>

<ul>
<li> <b> Control Mouse Wheel </b> change horizontal scale (zoom in/out) </li>
<li> <b> Shift Mouse Wheel </b> scroll left and right </li>
<li> <b> Control-Shift Mouse Wheel </b> change vertical scale </li>
<li> <b> Left Click drag </b> drag waveform left and right </li>
<li> <b> Left Click </b> highlight and decode the next two bytes from the cursor position </li>
<li> <b> Control Left Click </b> set selected waveform to that location and decode two bytes </li>
<li> <b> Shift Left Click </b> drag waveform vertically (zoom in for this to work) </li>
</ul>

<h3> Keys </h3>
<ul>
<li> <b> Right Arrow </b> scroll right to next negative to positive transition </li>
<li> <b> Left Arrow </b> scroll left to previous negative to positive transition </li>
<li> <b> Control Right Arrow </b> scroll right to next byte boundary </li>
<li> <b> Control Left Arrow </b> scroll left to previous byte boundary (approximate, uses samples per bit)</li>
</ul>

)");
		layout->addWidget(browser);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, quickHelpDialog);
		layout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::accepted, quickHelpDialog, &QDialog::close);
		quickHelpDialog->show();
	}

	void onDocumentation() {
		QDesktopServices::openUrl(QUrl("https://github.com/powool/poly88_tape"));
	}

	void onAbout() {
		QMessageBox::about(this, "About PolyWaveToImage",
			"Written by Paul Anderson");
	}

protected:
	void closeEvent(QCloseEvent *event) override {
		if (audioPtr && audioPtr->IsDirty()) {
			auto reply = QMessageBox::question(
				this, "Quit",
				"The audio data has been modified. Are you sure you want to quit?",
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if (reply == QMessageBox::Yes) {
				event->accept();
			} else {
				event->ignore();
			}
		} else {
			event->accept();
		}
	}
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QString fileName;

	if (argc == 2) {
		fileName = argv[1];
	}


	MainWindow win(nullptr, fileName);
	win.show();

	return app.exec();
}

#include "PolyWaveToImage.moc"
