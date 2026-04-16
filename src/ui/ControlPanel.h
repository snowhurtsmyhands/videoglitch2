#pragma once

#include <QHash>
#include <QLabel>
#include <QWidget>

class AppState;
class PresetManager;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;

class ControlPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPanel(AppState* state, PresetManager* presetManager, QWidget* parent = nullptr);
    void setExportBusy(bool busy);
    void setExportProgress(int percent);
    void updateExportSizeEstimate(qint64 durationMs);

signals:
    void exportRequested();

private:
    QWidget* makeSectionLabel(const QString& title);
    QWidget* makeLabeledSlider(const QString& name, int min, int max, int value, QSlider** outSlider);
    void applyPreset(const QString& name);
    void refreshExportSizeLabel();

    AppState* m_state;
    PresetManager* m_presetManager;
    QButtonGroup* m_presetGroup = nullptr;
    QButtonGroup* m_modeGroup = nullptr;
    QComboBox* m_userPresetCombo = nullptr;
    QLineEdit* m_userPresetNameEdit = nullptr;
    QCheckBox* m_previewAudioToggle = nullptr;
    QComboBox* m_exportQualityCombo = nullptr;
    QCheckBox* m_timecodeToggle = nullptr;
    QLineEdit* m_timecodeEdit = nullptr;
    QSlider* m_timecodeSizeSlider = nullptr;
    QSlider* m_timecodeXSlider = nullptr;
    QSlider* m_timecodeYSlider = nullptr;
    QHash<QString, QSlider*> m_effectSliders;
    QPushButton* m_exportButton = nullptr;
    QProgressBar* m_exportProgress = nullptr;
    QLabel* m_exportSizeLabel = nullptr;
    qint64 m_lastKnownDurationMs = 0;
};
