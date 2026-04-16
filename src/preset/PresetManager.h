#pragma once

#include <QObject>
#include <QColor>
#include <QStringList>

#include "app/AppState.h"

class PresetManager final : public QObject
{
    Q_OBJECT
public:
    struct BuiltInPreset {
        QString name;
        AppState::PreviewMode previewMode;
        bool timecodeEnabled;
        QColor timecodeColor;
        int timecodeSize;
        int timecodeX;
        int timecodeY;
        int headGlitch;
        int interlace;
        int pixelSort;
        int glitch;
        int tracking;
        int grain;
        int grainSize;
        int sineWarp;
        int colorBleed;
        int chromaShift;
        int pixelSortSize;
        int glitchBlockSize;
        int headGlitchSize;
        int flickerAmount;
    };

    explicit PresetManager(QObject* parent = nullptr);

    QStringList builtInPresets() const;
    BuiltInPreset preset(const QString& name) const;
};
