#pragma once

#include <QObject>
#include <QColor>
#include <QString>

class AppState final : public QObject
{
    Q_OBJECT
public:
    enum class PreviewMode { Draft, Balanced, Ultra };
    Q_ENUM(PreviewMode)
    enum class ExportQuality { Small, Balanced, High };
    Q_ENUM(ExportQuality)

    struct EffectSettings {
        PreviewMode previewMode = PreviewMode::Balanced;
        bool timecodeEnabled = true;
        bool previewAudioEnabled = false;
        int headGlitch = 0;
        int interlace = 0;
        int pixelSort = 0;
        int glitch = 0;
        int tracking = 0;
        int grain = 0;
        int grainSize = 0;
        int sineWarp = 0;
        int colorBleed = 0;
        int chromaShift = 0;
        int pixelSortSize = 0;
        int glitchBlockSize = 0;
        int headGlitchSize = 0;
        int flickerAmount = 0;
    };

    explicit AppState(QObject* parent = nullptr);

    QString videoPath() const;
    void setVideoPath(const QString& path);

    QString currentPreset() const;
    void setCurrentPreset(const QString& preset);

    PreviewMode previewMode() const;
    void setPreviewMode(PreviewMode mode);
    ExportQuality exportQuality() const;
    void setExportQuality(ExportQuality quality);

    bool timecodeEnabled() const;
    void setTimecodeEnabled(bool enabled);
    bool previewAudioEnabled() const;
    void setPreviewAudioEnabled(bool enabled);

    QString timecodeTemplate() const;
    void setTimecodeTemplate(const QString& value);

    QColor timecodeColor() const;
    void setTimecodeColor(const QColor& color);

    int timecodeX() const;
    void setTimecodeX(int value);

    int timecodeY() const;
    void setTimecodeY(int value);

    int timecodeSize() const;
    void setTimecodeSize(int value);

    int headGlitch() const;
    void setHeadGlitch(int value);

    int interlace() const;
    void setInterlace(int value);

    int pixelSort() const;
    void setPixelSort(int value);

    int glitch() const;
    void setGlitch(int value);

    int tracking() const;
    void setTracking(int value);

    int grain() const;
    void setGrain(int value);
    int grainSize() const;
    void setGrainSize(int value);

    int sineWarp() const;
    void setSineWarp(int value);

    int colorBleed() const;
    void setColorBleed(int value);

    int chromaShift() const;
    void setChromaShift(int value);
    int pixelSortSize() const;
    void setPixelSortSize(int value);
    int glitchBlockSize() const;
    void setGlitchBlockSize(int value);
    int headGlitchSize() const;
    void setHeadGlitchSize(int value);
    int flickerAmount() const;
    void setFlickerAmount(int value);
    void notifyStateChanged();

    EffectSettings effectSettings() const;

signals:
    void stateChanged();

private:
    QString m_videoPath;
    QString m_currentPreset = QStringLiteral("AKERA");
    PreviewMode m_previewMode = PreviewMode::Balanced;
    ExportQuality m_exportQuality = ExportQuality::Balanced;
    bool m_timecodeEnabled = true;
    bool m_previewAudioEnabled = false;
    QString m_timecodeTemplate = QStringLiteral("2021-08-03  {time}");
    QColor m_timecodeColor = QColor(QStringLiteral("#e6b84a"));
    int m_timecodeX = 2;
    int m_timecodeY = 92;
    int m_timecodeSize = 36;
    int m_headGlitch = 18;
    int m_interlace = 35;
    int m_pixelSort = 30;
    int m_glitch = 25;
    int m_tracking = 50;
    int m_grain = 45;
    int m_grainSize = 35;
    int m_sineWarp = 20;
    int m_colorBleed = 12;
    int m_chromaShift = 8;
    int m_pixelSortSize = 45;
    int m_glitchBlockSize = 45;
    int m_headGlitchSize = 45;
    int m_flickerAmount = 40;
};
