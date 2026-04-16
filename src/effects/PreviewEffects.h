#pragma once

#include <QImage>
#include <QStringList>
#include "app/AppState.h"

namespace PreviewEffects {
struct RuntimePreviewConfig {
    AppState::EffectSettings fx;
    bool degraded = false;
    QStringList reasons;
    double trackingScale = 1.0;
    double glitchScale = 1.0;
    double headRectScale = 1.0;
    double headZoneScale = 1.0;
    double pixelPassScale = 1.0;
    bool sineLite = false;
};

RuntimePreviewConfig buildRuntimePreviewCfg(const AppState::EffectSettings& fx, bool playing);
QImage applyPreview(const QImage& source, const RuntimePreviewConfig& runtimeCfg, qint64 frameIndex);
QImage applyExport(const QImage& source, const RuntimePreviewConfig& runtimeCfg, qint64 frameIndex);
}
