#include "ui/ControlPanel.h"
#include "app/AppState.h"
#include "preset/PresetManager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QProgressBar>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QPushButton* makeChip(const QString& text, bool active = false)
{
    auto* button = new QPushButton(text);
    button->setCheckable(true);
    button->setChecked(active);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}
}

ControlPanel::ControlPanel(AppState* state, PresetManager* presetManager, QWidget* parent)
    : QWidget(parent), m_state(state), m_presetManager(presetManager)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);

    auto* content = new QWidget;
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    layout->addWidget(makeSectionLabel(QStringLiteral("BUILT-IN PRESETS")));
    auto* chipRow = new QWidget;
    auto* chipLayout = new QHBoxLayout(chipRow);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(6);
    m_presetGroup = new QButtonGroup(this);
    m_presetGroup->setExclusive(true);
    int presetId = 0;
    for (const QString& preset : m_presetManager->builtInPresets()) {
        auto* button = makeChip(preset, preset == QStringLiteral("AKERA"));
        m_presetGroup->addButton(button, presetId++);
        chipLayout->addWidget(button);
    }
    connect(m_presetGroup, &QButtonGroup::idClicked, this, [this](int id) {
        auto* button = m_presetGroup->button(id);
        if (button) {
            applyPreset(button->text());
        }
    });
    layout->addWidget(chipRow);

    layout->addWidget(makeSectionLabel(QStringLiteral("USER PRESETS")));
    m_userPresetCombo = new QComboBox;
    m_userPresetCombo->addItems(m_presetManager->userPresets());
    m_userPresetNameEdit = new QLineEdit(QStringLiteral("MyPreset"));
    auto* presetButtons = new QWidget;
    auto* presetButtonsLayout = new QHBoxLayout(presetButtons);
    presetButtonsLayout->setContentsMargins(0, 0, 0, 0);
    presetButtonsLayout->setSpacing(6);
    auto* saveBtn = new QPushButton(QStringLiteral("Save Current as Preset"));
    auto* loadBtn = new QPushButton(QStringLiteral("Load Preset"));
    auto* deleteBtn = new QPushButton(QStringLiteral("Delete Preset"));
    presetButtonsLayout->addWidget(saveBtn);
    presetButtonsLayout->addWidget(loadBtn);
    presetButtonsLayout->addWidget(deleteBtn);
    layout->addWidget(m_userPresetCombo);
    layout->addWidget(m_userPresetNameEdit);
    layout->addWidget(presetButtons);

    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        if (!m_userPresetNameEdit || !m_userPresetCombo) return;
        const QString name = m_userPresetNameEdit->text().trimmed();
        if (name.isEmpty()) return;
        const auto fx = m_state->effectSettings();
        PresetManager::BuiltInPreset p {
            name, m_state->previewMode(), m_state->timecodeEnabled(), m_state->timecodeColor(),
            m_state->timecodeSize(), m_state->timecodeX(), m_state->timecodeY(),
            fx.headGlitch, fx.interlace, fx.pixelSort, fx.glitch, fx.tracking, fx.grain, fx.grainSize,
            fx.sineWarp, fx.colorBleed, fx.chromaShift, fx.pixelSortSize, fx.glitchBlockSize, fx.headGlitchSize, fx.flickerAmount
        };
        if (m_presetManager->saveUserPreset(p)) {
            const QSignalBlocker block(m_userPresetCombo);
            m_userPresetCombo->clear();
            m_userPresetCombo->addItems(m_presetManager->userPresets());
            m_userPresetCombo->setCurrentText(name);
        }
    });
    connect(loadBtn, &QPushButton::clicked, this, [this]() {
        if (!m_userPresetCombo) return;
        const QString name = m_userPresetCombo->currentText().trimmed();
        if (!name.isEmpty() && m_presetManager->hasPreset(name)) {
            applyPreset(name);
        }
    });
    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        if (!m_userPresetCombo) return;
        const QString name = m_userPresetCombo->currentText().trimmed();
        if (name.isEmpty()) return;
        if (m_presetManager->deleteUserPreset(name)) {
            const QSignalBlocker block(m_userPresetCombo);
            m_userPresetCombo->clear();
            m_userPresetCombo->addItems(m_presetManager->userPresets());
        }
    });

    layout->addWidget(makeSectionLabel(QStringLiteral("PREVIEW MODE")));
    auto* modeRow = new QWidget;
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(6);

    m_modeGroup = new QButtonGroup(this);
    auto* draft = makeChip(QStringLiteral("Draft"));
    auto* balanced = makeChip(QStringLiteral("Balanced"), true);
    auto* ultra = makeChip(QStringLiteral("Ultra"));
    m_modeGroup->addButton(draft, 0);
    m_modeGroup->addButton(balanced, 1);
    m_modeGroup->addButton(ultra, 2);
    m_modeGroup->setExclusive(true);
    modeLayout->addWidget(draft);
    modeLayout->addWidget(balanced);
    modeLayout->addWidget(ultra);
    layout->addWidget(modeRow);

    QObject::connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        switch (id) {
        case 0: m_state->setPreviewMode(AppState::PreviewMode::Draft); break;
        case 1: m_state->setPreviewMode(AppState::PreviewMode::Balanced); break;
        case 2: m_state->setPreviewMode(AppState::PreviewMode::Ultra); break;
        default: break;
        }
    });

    m_previewAudioToggle = new QCheckBox(QStringLiteral("Preview Audio"));
    m_previewAudioToggle->setChecked(m_state->previewAudioEnabled());
    QObject::connect(m_previewAudioToggle, &QCheckBox::toggled, m_state, &AppState::setPreviewAudioEnabled);
    layout->addWidget(m_previewAudioToggle);

    m_timecodeToggle = new QCheckBox(QStringLiteral("Enable Timecode"));
    m_timecodeToggle->setChecked(m_state->timecodeEnabled());
    QObject::connect(m_timecodeToggle, &QCheckBox::toggled, m_state, &AppState::setTimecodeEnabled);
    layout->addWidget(m_timecodeToggle);

    layout->addWidget(makeSectionLabel(QStringLiteral("TIMECODE")));
    m_timecodeEdit = new QLineEdit(m_state->timecodeTemplate());
    QObject::connect(m_timecodeEdit, &QLineEdit::textChanged, m_state, &AppState::setTimecodeTemplate);
    layout->addWidget(m_timecodeEdit);
    layout->addWidget(makeLabeledSlider(QStringLiteral("Timecode Size"), 16, 96, m_state->timecodeSize(), &m_timecodeSizeSlider));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Timecode X"), 0, 100, m_state->timecodeX(), &m_timecodeXSlider));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Timecode Y"), 0, 100, m_state->timecodeY(), &m_timecodeYSlider));

    QObject::connect(m_timecodeSizeSlider, &QSlider::valueChanged, m_state, &AppState::setTimecodeSize);
    QObject::connect(m_timecodeXSlider, &QSlider::valueChanged, m_state, &AppState::setTimecodeX);
    QObject::connect(m_timecodeYSlider, &QSlider::valueChanged, m_state, &AppState::setTimecodeY);

    layout->addWidget(makeSectionLabel(QStringLiteral("SIGNAL CORRUPTION")));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Head Glitch"), 0, 100, 0, &m_effectSliders[QStringLiteral("Head Glitch")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Head Glitch Size"), 0, 100, 45, &m_effectSliders[QStringLiteral("Head Glitch Size")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Interlace Flicker"), 0, 100, 0, &m_effectSliders[QStringLiteral("Interlace Flicker")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Flicker Amount"), 0, 100, 40, &m_effectSliders[QStringLiteral("Flicker Amount")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Pixel Sort"), 0, 100, 0, &m_effectSliders[QStringLiteral("Pixel Sort")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Pixel Sort Size"), 0, 100, 45, &m_effectSliders[QStringLiteral("Pixel Sort Size")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Glitch Blocks"), 0, 100, 0, &m_effectSliders[QStringLiteral("Glitch Blocks")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Glitch Block Size"), 0, 100, 45, &m_effectSliders[QStringLiteral("Glitch Block Size")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Tracking Error"), 0, 100, 0, &m_effectSliders[QStringLiteral("Tracking Error")]));

    layout->addWidget(makeSectionLabel(QStringLiteral("ANALOG")));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Film Grain"), 0, 100, 20, &m_effectSliders[QStringLiteral("Film Grain")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Grain Size"), 0, 100, 35, &m_effectSliders[QStringLiteral("Grain Size")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Sine Warp"), 0, 100, 22, &m_effectSliders[QStringLiteral("Sine Warp")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Color Bleed"), 0, 30, 5, &m_effectSliders[QStringLiteral("Color Bleed")]));
    layout->addWidget(makeLabeledSlider(QStringLiteral("Chroma Shift"), 0, 30, 4, &m_effectSliders[QStringLiteral("Chroma Shift")]));

    QObject::connect(m_effectSliders[QStringLiteral("Head Glitch")], &QSlider::valueChanged, m_state, &AppState::setHeadGlitch);
    QObject::connect(m_effectSliders[QStringLiteral("Head Glitch Size")], &QSlider::valueChanged, m_state, &AppState::setHeadGlitchSize);
    QObject::connect(m_effectSliders[QStringLiteral("Interlace Flicker")], &QSlider::valueChanged, m_state, &AppState::setInterlace);
    QObject::connect(m_effectSliders[QStringLiteral("Flicker Amount")], &QSlider::valueChanged, m_state, &AppState::setFlickerAmount);
    QObject::connect(m_effectSliders[QStringLiteral("Pixel Sort")], &QSlider::valueChanged, m_state, &AppState::setPixelSort);
    QObject::connect(m_effectSliders[QStringLiteral("Pixel Sort Size")], &QSlider::valueChanged, m_state, &AppState::setPixelSortSize);
    QObject::connect(m_effectSliders[QStringLiteral("Glitch Blocks")], &QSlider::valueChanged, m_state, &AppState::setGlitch);
    QObject::connect(m_effectSliders[QStringLiteral("Glitch Block Size")], &QSlider::valueChanged, m_state, &AppState::setGlitchBlockSize);
    QObject::connect(m_effectSliders[QStringLiteral("Tracking Error")], &QSlider::valueChanged, m_state, &AppState::setTracking);
    QObject::connect(m_effectSliders[QStringLiteral("Film Grain")], &QSlider::valueChanged, m_state, &AppState::setGrain);
    QObject::connect(m_effectSliders[QStringLiteral("Grain Size")], &QSlider::valueChanged, m_state, &AppState::setGrainSize);
    QObject::connect(m_effectSliders[QStringLiteral("Sine Warp")], &QSlider::valueChanged, m_state, &AppState::setSineWarp);
    QObject::connect(m_effectSliders[QStringLiteral("Color Bleed")], &QSlider::valueChanged, m_state, &AppState::setColorBleed);
    QObject::connect(m_effectSliders[QStringLiteral("Chroma Shift")], &QSlider::valueChanged, m_state, &AppState::setChromaShift);

    layout->addWidget(makeSectionLabel(QStringLiteral("EXPORT")));
    m_exportQualityCombo = new QComboBox(this);
    m_exportQualityCombo->addItems({QStringLiteral("Small"), QStringLiteral("Balanced"), QStringLiteral("High")});
    m_exportQualityCombo->setCurrentText(QStringLiteral("Balanced"));
    connect(m_exportQualityCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (text == QStringLiteral("Small")) {
            m_state->setExportQuality(AppState::ExportQuality::Small);
        } else if (text == QStringLiteral("High")) {
            m_state->setExportQuality(AppState::ExportQuality::High);
        } else {
            m_state->setExportQuality(AppState::ExportQuality::Balanced);
        }
        refreshExportSizeLabel();
    });
    layout->addWidget(m_exportQualityCombo);

    m_exportSizeLabel = new QLabel(QStringLiteral("Est. size: — (load a video first)"), this);
    m_exportSizeLabel->setWordWrap(true);
    m_exportSizeLabel->setStyleSheet(QStringLiteral("color: #666; font-size: 11px; padding: 2px 0;"));
    layout->addWidget(m_exportSizeLabel);

    m_exportButton = new QPushButton(QStringLiteral("EXPORT VIDEO"));
    connect(m_exportButton, &QPushButton::clicked, this, &ControlPanel::exportRequested);
    layout->addWidget(m_exportButton);
    m_exportProgress = new QProgressBar(this);
    m_exportProgress->setRange(0, 100);
    m_exportProgress->setValue(0);
    layout->addWidget(m_exportProgress);
    layout->addStretch(1);

    applyPreset(QStringLiteral("AKERA"));
}

QWidget* ControlPanel::makeSectionLabel(const QString& title)
{
    auto* f = new QWidget;
    auto* row = new QHBoxLayout(f);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto* label = new QLabel(title);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);

    row->addWidget(label);
    row->addWidget(line, 1);
    return f;
}

QWidget* ControlPanel::makeLabeledSlider(const QString& name, int min, int max, int value, QSlider** outSlider)
{
    auto* wrap = new QWidget;
    auto* layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* label = new QLabel(name);
    label->setMinimumWidth(120);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(value);
    auto* valueLabel = new QLabel(QString::number(value));
    valueLabel->setMinimumWidth(32);

    QObject::connect(slider, &QSlider::valueChanged, valueLabel, [valueLabel](int v) {
        valueLabel->setText(QString::number(v));
    });

    layout->addWidget(label);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);

    if (outSlider) {
        *outSlider = slider;
    }
    return wrap;
}

void ControlPanel::applyPreset(const QString& name)
{
    if (!m_presetManager->hasPreset(name)) {
        return;
    }
    const auto p = m_presetManager->preset(name);
    {
        const QSignalBlocker stateBlocker(m_state);
        m_state->setCurrentPreset(name);
        m_state->setTimecodeEnabled(p.timecodeEnabled);
        m_state->setTimecodeColor(p.timecodeColor);
        m_state->setTimecodeSize(p.timecodeSize);
        m_state->setTimecodeX(p.timecodeX);
        m_state->setTimecodeY(p.timecodeY);
        m_state->setPreviewMode(p.previewMode);
        m_state->setPreviewAudioEnabled(p.previewMode == AppState::PreviewMode::Ultra);
        m_state->setHeadGlitch(p.headGlitch);
        m_state->setHeadGlitchSize(p.headGlitchSize);
        m_state->setInterlace(p.interlace);
        m_state->setFlickerAmount(p.flickerAmount);
        m_state->setPixelSort(p.pixelSort);
        m_state->setPixelSortSize(p.pixelSortSize);
        m_state->setGlitch(p.glitch);
        m_state->setGlitchBlockSize(p.glitchBlockSize);
        m_state->setTracking(p.tracking);
        m_state->setGrain(p.grain);
        m_state->setGrainSize(p.grainSize);
        m_state->setSineWarp(p.sineWarp);
        m_state->setColorBleed(p.colorBleed);
        m_state->setChromaShift(p.chromaShift);
    }

    {
        const QSignalBlocker b0(m_previewAudioToggle);
        m_previewAudioToggle->setChecked(m_state->previewAudioEnabled());
    }
    {
        const QSignalBlocker bq(m_exportQualityCombo);
        switch (m_state->exportQuality()) {
        case AppState::ExportQuality::Small: m_exportQualityCombo->setCurrentText(QStringLiteral("Small")); break;
        case AppState::ExportQuality::Balanced: m_exportQualityCombo->setCurrentText(QStringLiteral("Balanced")); break;
        case AppState::ExportQuality::High: m_exportQualityCombo->setCurrentText(QStringLiteral("High")); break;
        }
    }
    {
        const QSignalBlocker b1(m_timecodeToggle);
        m_timecodeToggle->setChecked(m_state->timecodeEnabled());
    }
    {
        const QSignalBlocker b2(m_timecodeSizeSlider);
        m_timecodeSizeSlider->setValue(m_state->timecodeSize());
    }
    {
        const QSignalBlocker b3(m_timecodeXSlider);
        m_timecodeXSlider->setValue(m_state->timecodeX());
    }
    {
        const QSignalBlocker b4(m_timecodeYSlider);
        m_timecodeYSlider->setValue(m_state->timecodeY());
    }
    {
        const QSignalBlocker b5(m_effectSliders[QStringLiteral("Head Glitch")]);
        m_effectSliders[QStringLiteral("Head Glitch")]->setValue(m_state->headGlitch());
    }
    {
        const QSignalBlocker b6(m_effectSliders[QStringLiteral("Interlace Flicker")]);
        m_effectSliders[QStringLiteral("Interlace Flicker")]->setValue(m_state->interlace());
    }
    {
        const QSignalBlocker b7(m_effectSliders[QStringLiteral("Pixel Sort")]);
        m_effectSliders[QStringLiteral("Pixel Sort")]->setValue(m_state->pixelSort());
    }
    {
        const QSignalBlocker b8(m_effectSliders[QStringLiteral("Glitch Blocks")]);
        m_effectSliders[QStringLiteral("Glitch Blocks")]->setValue(m_state->glitch());
    }
    {
        const QSignalBlocker b9(m_effectSliders[QStringLiteral("Tracking Error")]);
        m_effectSliders[QStringLiteral("Tracking Error")]->setValue(m_state->tracking());
    }
    {
        const QSignalBlocker b10(m_effectSliders[QStringLiteral("Film Grain")]);
        m_effectSliders[QStringLiteral("Film Grain")]->setValue(m_state->grain());
    }
    {
        const QSignalBlocker b11(m_effectSliders[QStringLiteral("Sine Warp")]);
        m_effectSliders[QStringLiteral("Sine Warp")]->setValue(m_state->sineWarp());
    }
    {
        const QSignalBlocker b12(m_effectSliders[QStringLiteral("Color Bleed")]);
        m_effectSliders[QStringLiteral("Color Bleed")]->setValue(m_state->colorBleed());
    }
    {
        const QSignalBlocker b13(m_effectSliders[QStringLiteral("Chroma Shift")]);
        m_effectSliders[QStringLiteral("Chroma Shift")]->setValue(m_state->chromaShift());
    }
    {
        const QSignalBlocker b14(m_effectSliders[QStringLiteral("Head Glitch Size")]);
        m_effectSliders[QStringLiteral("Head Glitch Size")]->setValue(m_state->headGlitchSize());
    }
    {
        const QSignalBlocker b15(m_effectSliders[QStringLiteral("Flicker Amount")]);
        m_effectSliders[QStringLiteral("Flicker Amount")]->setValue(m_state->flickerAmount());
    }
    {
        const QSignalBlocker b16(m_effectSliders[QStringLiteral("Pixel Sort Size")]);
        m_effectSliders[QStringLiteral("Pixel Sort Size")]->setValue(m_state->pixelSortSize());
    }
    {
        const QSignalBlocker b17(m_effectSliders[QStringLiteral("Glitch Block Size")]);
        m_effectSliders[QStringLiteral("Glitch Block Size")]->setValue(m_state->glitchBlockSize());
    }
    {
        const QSignalBlocker b18(m_effectSliders[QStringLiteral("Grain Size")]);
        m_effectSliders[QStringLiteral("Grain Size")]->setValue(m_state->grainSize());
    }

    switch (m_state->previewMode()) {
    case AppState::PreviewMode::Draft: m_modeGroup->button(0)->setChecked(true); break;
    case AppState::PreviewMode::Balanced: m_modeGroup->button(1)->setChecked(true); break;
    case AppState::PreviewMode::Ultra: m_modeGroup->button(2)->setChecked(true); break;
    }
    if (m_userPresetNameEdit) {
        const QSignalBlocker block(m_userPresetNameEdit);
        m_userPresetNameEdit->setText(name);
    }
    if (m_userPresetCombo) {
        const QSignalBlocker block(m_userPresetCombo);
        if (m_userPresetCombo->findText(name) >= 0) {
            m_userPresetCombo->setCurrentText(name);
        }
    }
    m_state->notifyStateChanged();
}

void ControlPanel::setExportBusy(bool busy)
{
    if (m_exportButton) {
        m_exportButton->setEnabled(!busy);
        m_exportButton->setText(busy ? QStringLiteral("EXPORTING...") : QStringLiteral("EXPORT VIDEO"));
    }
}

void ControlPanel::setExportProgress(int percent)
{
    if (m_exportProgress) {
        m_exportProgress->setValue(std::clamp(percent, 0, 100));
    }
}

void ControlPanel::updateExportSizeEstimate(qint64 durationMs)
{
    m_lastKnownDurationMs = durationMs;
    refreshExportSizeLabel();
}

void ControlPanel::refreshExportSizeLabel()
{
    if (!m_exportSizeLabel) return;

    if (m_lastKnownDurationMs <= 0) {
        m_exportSizeLabel->setText(QStringLiteral("Est. size: — (load a video first)"));
        return;
    }

    // Approximate bitrate estimates based on quality preset (1080p reference)
    // Small:    CRF 28 + faster  → ~800 kbps video
    // Balanced: CRF 23 + faster  → ~2000 kbps video
    // High:     CRF 18 + medium  → ~5000 kbps video
    // Audio: 128 kbps AAC always
    qint64 videoBitrateKbps = 2000;
    const auto q = m_state ? m_state->exportQuality() : AppState::ExportQuality::Balanced;
    switch (q) {
    case AppState::ExportQuality::Small:    videoBitrateKbps = 800;  break;
    case AppState::ExportQuality::Balanced: videoBitrateKbps = 2000; break;
    case AppState::ExportQuality::High:     videoBitrateKbps = 5000; break;
    }
    constexpr qint64 audioBitrateKbps = 128;
    const qint64 totalBitrateKbps = videoBitrateKbps + audioBitrateKbps;
    const double durationSec = m_lastKnownDurationMs / 1000.0;
    // size in bytes = (bitrate_kbps * 1000 / 8) * duration_sec
    const qint64 estimatedBytes = static_cast<qint64>((totalBitrateKbps * 1000.0 / 8.0) * durationSec);

    QString sizeStr;
    if (estimatedBytes < 1024LL * 1024) {
        sizeStr = QStringLiteral("%1 KB").arg(estimatedBytes / 1024);
    } else if (estimatedBytes < 1024LL * 1024 * 1024) {
        sizeStr = QStringLiteral("%1 MB").arg(estimatedBytes / (1024 * 1024));
    } else {
        sizeStr = QStringLiteral("%1.%2 GB")
            .arg(estimatedBytes / (1024LL * 1024 * 1024))
            .arg((estimatedBytes % (1024LL * 1024 * 1024)) / (1024LL * 1024 * 102));
    }

    const QString qualityNote = (q == AppState::ExportQuality::Small)
        ? QStringLiteral("compressed, smaller") : (q == AppState::ExportQuality::High)
        ? QStringLiteral("high quality, larger") : QStringLiteral("balanced quality");

    m_exportSizeLabel->setText(
        QStringLiteral("Est. size: ~%1  (%2)\nBased on %3 kbps video + 128k audio")
            .arg(sizeStr, qualityNote)
            .arg(videoBitrateKbps));
}
