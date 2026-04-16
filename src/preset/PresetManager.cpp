#include "preset/PresetManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
QHash<QString, PresetManager::BuiltInPreset> builtInPresetMap()
{
    return {
        {QStringLiteral("RAW"), {QStringLiteral("RAW"), AppState::PreviewMode::Balanced, false, QColor("#e6b84a"), 32, 2, 92, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("VHS"), {QStringLiteral("VHS"), AppState::PreviewMode::Balanced, true, QColor("#e6b84a"), 36, 2, 92, 0, 30, 0, 10, 40, 35, 35, 15, 10, 4, 45, 45, 45, 45}},
        {QStringLiteral("CRT"), {QStringLiteral("CRT"), AppState::PreviewMode::Ultra, false, QColor("#b8e64a"), 32, 2, 92, 0, 60, 0, 5, 0, 20, 30, 0, 5, 6, 40, 35, 35, 55}},
        {QStringLiteral("SIGNAL"), {QStringLiteral("SIGNAL"), AppState::PreviewMode::Draft, false, QColor("#9be2ff"), 30, 2, 92, 30, 40, 20, 40, 60, 60, 45, 30, 15, 12, 60, 55, 65, 55}},
        {QStringLiteral("AKERA"), {QStringLiteral("AKERA"), AppState::PreviewMode::Balanced, true, QColor("#e6b84a"), 36, 2, 92, 18, 35, 30, 25, 50, 45, 35, 20, 12, 8, 50, 45, 55, 50}}
    };
}

QJsonObject toJson(const PresetManager::BuiltInPreset& p)
{
    QJsonObject o;
    o[QStringLiteral("name")] = p.name;
    o[QStringLiteral("previewMode")] = static_cast<int>(p.previewMode);
    o[QStringLiteral("timecodeEnabled")] = p.timecodeEnabled;
    o[QStringLiteral("timecodeColor")] = p.timecodeColor.name();
    o[QStringLiteral("timecodeSize")] = p.timecodeSize;
    o[QStringLiteral("timecodeX")] = p.timecodeX;
    o[QStringLiteral("timecodeY")] = p.timecodeY;
    o[QStringLiteral("headGlitch")] = p.headGlitch;
    o[QStringLiteral("interlace")] = p.interlace;
    o[QStringLiteral("pixelSort")] = p.pixelSort;
    o[QStringLiteral("glitch")] = p.glitch;
    o[QStringLiteral("tracking")] = p.tracking;
    o[QStringLiteral("grain")] = p.grain;
    o[QStringLiteral("grainSize")] = p.grainSize;
    o[QStringLiteral("sineWarp")] = p.sineWarp;
    o[QStringLiteral("colorBleed")] = p.colorBleed;
    o[QStringLiteral("chromaShift")] = p.chromaShift;
    o[QStringLiteral("pixelSortSize")] = p.pixelSortSize;
    o[QStringLiteral("glitchBlockSize")] = p.glitchBlockSize;
    o[QStringLiteral("headGlitchSize")] = p.headGlitchSize;
    o[QStringLiteral("flickerAmount")] = p.flickerAmount;
    return o;
}

PresetManager::BuiltInPreset fromJson(const QJsonObject& o)
{
    PresetManager::BuiltInPreset p{};
    p.name = o.value(QStringLiteral("name")).toString();
    p.previewMode = static_cast<AppState::PreviewMode>(o.value(QStringLiteral("previewMode")).toInt(1));
    p.timecodeEnabled = o.value(QStringLiteral("timecodeEnabled")).toBool(true);
    p.timecodeColor = QColor(o.value(QStringLiteral("timecodeColor")).toString(QStringLiteral("#e6b84a")));
    p.timecodeSize = o.value(QStringLiteral("timecodeSize")).toInt(36);
    p.timecodeX = o.value(QStringLiteral("timecodeX")).toInt(2);
    p.timecodeY = o.value(QStringLiteral("timecodeY")).toInt(92);
    p.headGlitch = o.value(QStringLiteral("headGlitch")).toInt(0);
    p.interlace = o.value(QStringLiteral("interlace")).toInt(0);
    p.pixelSort = o.value(QStringLiteral("pixelSort")).toInt(0);
    p.glitch = o.value(QStringLiteral("glitch")).toInt(0);
    p.tracking = o.value(QStringLiteral("tracking")).toInt(0);
    p.grain = o.value(QStringLiteral("grain")).toInt(0);
    p.grainSize = o.value(QStringLiteral("grainSize")).toInt(0);
    p.sineWarp = o.value(QStringLiteral("sineWarp")).toInt(0);
    p.colorBleed = o.value(QStringLiteral("colorBleed")).toInt(0);
    p.chromaShift = o.value(QStringLiteral("chromaShift")).toInt(0);
    p.pixelSortSize = o.value(QStringLiteral("pixelSortSize")).toInt(0);
    p.glitchBlockSize = o.value(QStringLiteral("glitchBlockSize")).toInt(0);
    p.headGlitchSize = o.value(QStringLiteral("headGlitchSize")).toInt(0);
    p.flickerAmount = o.value(QStringLiteral("flickerAmount")).toInt(0);
    return p;
}
}

PresetManager::PresetManager(QObject* parent) : QObject(parent) {}

QStringList PresetManager::builtInPresets() const
{
    return {QStringLiteral("RAW"), QStringLiteral("VHS"), QStringLiteral("CRT"), QStringLiteral("SIGNAL"), QStringLiteral("AKERA")};
}

QStringList PresetManager::userPresets() const
{
    return loadUserPresets().keys();
}

PresetManager::BuiltInPreset PresetManager::preset(const QString& name) const
{
    const auto builtins = builtInPresetMap();
    if (builtins.contains(name)) return builtins.value(name);
    const auto users = loadUserPresets();
    if (users.contains(name)) return users.value(name);
    return builtins.value(QStringLiteral("AKERA"));
}

bool PresetManager::hasPreset(const QString& name) const
{
    const auto builtins = builtInPresetMap();
    if (builtins.contains(name)) return true;
    return loadUserPresets().contains(name);
}

bool PresetManager::saveUserPreset(const BuiltInPreset& preset)
{
    if (preset.name.trimmed().isEmpty()) {
        return false;
    }
    auto presets = loadUserPresets();
    presets.insert(preset.name.trimmed(), preset);
    return writeUserPresets(presets);
}

bool PresetManager::deleteUserPreset(const QString& name)
{
    auto presets = loadUserPresets();
    if (!presets.remove(name)) {
        return false;
    }
    return writeUserPresets(presets);
}

QString PresetManager::userPresetPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/user_presets.json");
}

QHash<QString, PresetManager::BuiltInPreset> PresetManager::loadUserPresets() const
{
    QHash<QString, BuiltInPreset> out;
    QFile f(userPresetPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto arr = doc.array();
    for (const auto& v : arr) {
        const auto p = fromJson(v.toObject());
        if (!p.name.isEmpty()) {
            out.insert(p.name, p);
        }
    }
    return out;
}

bool PresetManager::writeUserPresets(const QHash<QString, BuiltInPreset>& presets) const
{
    QFileInfo fi(userPresetPath());
    QDir().mkpath(fi.path());
    QFile f(fi.filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QJsonArray arr;
    for (const auto& p : presets) {
        arr.push_back(toJson(p));
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}
