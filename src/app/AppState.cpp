#include "app/AppState.h"

#include <QtGlobal>

AppState::AppState(QObject* parent) : QObject(parent) {}

QString AppState::videoPath() const { return m_videoPath; }
void AppState::setVideoPath(const QString& path)
{
    if (m_videoPath == path) return;
    m_videoPath = path;
    emit stateChanged();
}

QString AppState::currentPreset() const { return m_currentPreset; }
void AppState::setCurrentPreset(const QString& preset)
{
    if (m_currentPreset == preset) return;
    m_currentPreset = preset;
    emit stateChanged();
}

AppState::PreviewMode AppState::previewMode() const { return m_previewMode; }
void AppState::setPreviewMode(PreviewMode mode)
{
    if (m_previewMode == mode) return;
    m_previewMode = mode;
    emit stateChanged();
}

AppState::ExportQuality AppState::exportQuality() const { return m_exportQuality; }
void AppState::setExportQuality(ExportQuality quality)
{
    if (m_exportQuality == quality) return;
    m_exportQuality = quality;
    emit stateChanged();
}

bool AppState::timecodeEnabled() const { return m_timecodeEnabled; }
void AppState::setTimecodeEnabled(bool enabled)
{
    if (m_timecodeEnabled == enabled) return;
    m_timecodeEnabled = enabled;
    emit stateChanged();
}

bool AppState::previewAudioEnabled() const { return m_previewAudioEnabled; }
void AppState::setPreviewAudioEnabled(bool enabled)
{
    if (m_previewAudioEnabled == enabled) return;
    m_previewAudioEnabled = enabled;
    emit stateChanged();
}

QString AppState::timecodeTemplate() const { return m_timecodeTemplate; }
void AppState::setTimecodeTemplate(const QString& value)
{
    if (m_timecodeTemplate == value) return;
    m_timecodeTemplate = value;
    emit stateChanged();
}

QColor AppState::timecodeColor() const { return m_timecodeColor; }
void AppState::setTimecodeColor(const QColor& color)
{
    if (m_timecodeColor == color) return;
    m_timecodeColor = color;
    emit stateChanged();
}

int AppState::timecodeX() const { return m_timecodeX; }
void AppState::setTimecodeX(int value)
{
    value = qBound(0, value, 100);
    if (m_timecodeX == value) return;
    m_timecodeX = value;
    emit stateChanged();
}

int AppState::timecodeY() const { return m_timecodeY; }
void AppState::setTimecodeY(int value)
{
    value = qBound(0, value, 100);
    if (m_timecodeY == value) return;
    m_timecodeY = value;
    emit stateChanged();
}

int AppState::timecodeSize() const { return m_timecodeSize; }
void AppState::setTimecodeSize(int value)
{
    value = qBound(16, value, 180);
    if (m_timecodeSize == value) return;
    m_timecodeSize = value;
    emit stateChanged();
}

int AppState::headGlitch() const { return m_headGlitch; }
void AppState::setHeadGlitch(int value)
{
    value = qBound(0, value, 100);
    if (m_headGlitch == value) return;
    m_headGlitch = value;
    emit stateChanged();
}

int AppState::interlace() const { return m_interlace; }
void AppState::setInterlace(int value)
{
    value = qBound(0, value, 100);
    if (m_interlace == value) return;
    m_interlace = value;
    emit stateChanged();
}

int AppState::pixelSort() const { return m_pixelSort; }
void AppState::setPixelSort(int value)
{
    value = qBound(0, value, 100);
    if (m_pixelSort == value) return;
    m_pixelSort = value;
    emit stateChanged();
}

int AppState::glitch() const { return m_glitch; }
void AppState::setGlitch(int value)
{
    value = qBound(0, value, 100);
    if (m_glitch == value) return;
    m_glitch = value;
    emit stateChanged();
}

int AppState::tracking() const { return m_tracking; }
void AppState::setTracking(int value)
{
    value = qBound(0, value, 100);
    if (m_tracking == value) return;
    m_tracking = value;
    emit stateChanged();
}

int AppState::grain() const { return m_grain; }
void AppState::setGrain(int value)
{
    value = qBound(0, value, 100);
    if (m_grain == value) return;
    m_grain = value;
    emit stateChanged();
}

int AppState::grainSize() const { return m_grainSize; }
void AppState::setGrainSize(int value)
{
    value = qBound(0, value, 100);
    if (m_grainSize == value) return;
    m_grainSize = value;
    emit stateChanged();
}

int AppState::sineWarp() const { return m_sineWarp; }
void AppState::setSineWarp(int value)
{
    value = qBound(0, value, 100);
    if (m_sineWarp == value) return;
    m_sineWarp = value;
    emit stateChanged();
}

int AppState::colorBleed() const { return m_colorBleed; }
void AppState::setColorBleed(int value)
{
    value = qBound(0, value, 30);
    if (m_colorBleed == value) return;
    m_colorBleed = value;
    emit stateChanged();
}

int AppState::chromaShift() const { return m_chromaShift; }
void AppState::setChromaShift(int value)
{
    value = qBound(0, value, 30);
    if (m_chromaShift == value) return;
    m_chromaShift = value;
    emit stateChanged();
}

int AppState::pixelSortSize() const { return m_pixelSortSize; }
void AppState::setPixelSortSize(int value)
{
    value = qBound(0, value, 100);
    if (m_pixelSortSize == value) return;
    m_pixelSortSize = value;
    emit stateChanged();
}

int AppState::glitchBlockSize() const { return m_glitchBlockSize; }
void AppState::setGlitchBlockSize(int value)
{
    value = qBound(0, value, 100);
    if (m_glitchBlockSize == value) return;
    m_glitchBlockSize = value;
    emit stateChanged();
}

int AppState::headGlitchSize() const { return m_headGlitchSize; }
void AppState::setHeadGlitchSize(int value)
{
    value = qBound(0, value, 100);
    if (m_headGlitchSize == value) return;
    m_headGlitchSize = value;
    emit stateChanged();
}

int AppState::flickerAmount() const { return m_flickerAmount; }
void AppState::setFlickerAmount(int value)
{
    value = qBound(0, value, 100);
    if (m_flickerAmount == value) return;
    m_flickerAmount = value;
    emit stateChanged();
}

AppState::EffectSettings AppState::effectSettings() const
{
    EffectSettings fx;
    fx.previewMode = m_previewMode;
    fx.timecodeEnabled = m_timecodeEnabled;
    fx.previewAudioEnabled = m_previewAudioEnabled;
    fx.headGlitch = m_headGlitch;
    fx.interlace = m_interlace;
    fx.pixelSort = m_pixelSort;
    fx.glitch = m_glitch;
    fx.tracking = m_tracking;
    fx.grain = m_grain;
    fx.grainSize = m_grainSize;
    fx.sineWarp = m_sineWarp;
    fx.colorBleed = m_colorBleed;
    fx.chromaShift = m_chromaShift;
    fx.pixelSortSize = m_pixelSortSize;
    fx.glitchBlockSize = m_glitchBlockSize;
    fx.headGlitchSize = m_headGlitchSize;
    fx.flickerAmount = m_flickerAmount;
    return fx;
}
