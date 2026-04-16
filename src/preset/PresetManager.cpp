#include "preset/PresetManager.h"

#include <QHash>

PresetManager::PresetManager(QObject* parent) : QObject(parent) {}

QStringList PresetManager::builtInPresets() const
{
    return {QStringLiteral("RAW"), QStringLiteral("VHS"), QStringLiteral("CRT"), QStringLiteral("SIGNAL"), QStringLiteral("AKERA")};
}

PresetManager::BuiltInPreset PresetManager::preset(const QString& name) const
{
    static const QHash<QString, BuiltInPreset> map = {
        {QStringLiteral("RAW"), {QStringLiteral("RAW"), AppState::PreviewMode::Balanced, false, QColor("#e6b84a"), 32, 2, 92, 0, 0, 0, 0, 0, 0, 0, 0, 0, 45, 45, 45, 40}},
        {QStringLiteral("VHS"), {QStringLiteral("VHS"), AppState::PreviewMode::Balanced, true, QColor("#e6b84a"), 36, 2, 92, 0, 30, 0, 10, 40, 35, 35, 15, 10, 4, 45, 45, 45, 45}},
        {QStringLiteral("CRT"), {QStringLiteral("CRT"), AppState::PreviewMode::Ultra, false, QColor("#b8e64a"), 32, 2, 92, 0, 60, 0, 5, 0, 20, 30, 0, 5, 6, 40, 35, 35, 55}},
        {QStringLiteral("SIGNAL"), {QStringLiteral("SIGNAL"), AppState::PreviewMode::Draft, false, QColor("#9be2ff"), 30, 2, 92, 30, 40, 20, 40, 60, 60, 45, 30, 15, 12, 60, 55, 65, 55}},
        {QStringLiteral("AKERA"), {QStringLiteral("AKERA"), AppState::PreviewMode::Balanced, true, QColor("#e6b84a"), 36, 2, 92, 18, 35, 30, 25, 50, 45, 35, 20, 12, 8, 50, 45, 55, 50}}
    };
    return map.value(name, map.value(QStringLiteral("AKERA")));
}
