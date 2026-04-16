#include "effects/PreviewEffects.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {
static inline int clip8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

struct FastRng {
    quint32 state = 1;
    explicit FastRng(quint32 seed) : state(seed ? seed : 1u) {}
    quint32 nextU32()
    {
        state ^= (state << 13);
        state ^= (state >> 17);
        state ^= (state << 5);
        return state;
    }
    double next01() { return static_cast<double>(nextU32() & 0x00FFFFFFu) / static_cast<double>(0x01000000u); }
    int nextInt(int lo, int hiInclusive)
    {
        if (hiInclusive <= lo) return lo;
        return lo + static_cast<int>(nextU32() % static_cast<quint32>(hiInclusive - lo + 1));
    }
};

QImage toArgb(const QImage& src)
{
    if (src.format() == QImage::Format_ARGB32 || src.format() == QImage::Format_RGB32) {
        return src.copy();
    }
    return src.convertToFormat(QImage::Format_ARGB32);
}

void rollRowSegment(QRgb* row, int width, int x0, int x1, int shift)
{
    if (width <= 1 || x1 <= x0) return;
    x0 = std::clamp(x0, 0, width - 1);
    x1 = std::clamp(x1, 0, width);
    const int count = x1 - x0;
    if (count <= 1) return;
    shift %= count;
    if (shift < 0) shift += count;
    if (shift == 0) return;

    std::vector<QRgb> temp(static_cast<size_t>(count));
    std::copy(row + x0, row + x1, temp.begin());
    for (int i = 0; i < count; ++i) {
        row[x0 + i] = temp[static_cast<size_t>((i - shift + count) % count)];
    }
}

QImage fxChroma(const QImage& src, int v)
{
    if (v <= 0) return src;
    // Write directly into output without an extra intermediate copy
    QImage out(src.size(), QImage::Format_ARGB32);
    const int w = src.width();
    const int h = src.height();
    for (int y = 0; y < h; ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int xr = x + v < w ? x + v : w - 1;
            const int xb = x - v > 0 ? x - v : 0;
            dst[x] = qRgba(qRed(in[xr]), qGreen(in[x]), qBlue(in[xb]), qAlpha(in[x]));
        }
    }
    return out;
}

QImage fxBleed(const QImage& src, int v)
{
    if (v <= 0) return src;
    const int k = std::max(1, v / 5) * 2 + 1;
    const int radius = k / 2;
    const double blend = std::clamp(v / 100.0, 0.0, 1.0);
    QImage out = src.copy();
    const int w = src.width();
    const int h = src.height();
    // Use prefix sums on all 3 channels to avoid per-pixel division
    std::vector<int> prefR(static_cast<size_t>(w + 1), 0);
    std::vector<int> prefG(static_cast<size_t>(w + 1), 0);
    std::vector<int> prefB(static_cast<size_t>(w + 1), 0);

    for (int y = 0; y < h; ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        prefR[0] = prefG[0] = prefB[0] = 0;
        for (int x = 0; x < w; ++x) {
            prefR[x + 1] = prefR[x] + qRed(in[x]);
            prefG[x + 1] = prefG[x] + qGreen(in[x]);
            prefB[x + 1] = prefB[x] + qBlue(in[x]);
        }
        for (int x = 0; x < w; ++x) {
            const int lx = std::max(0, x - radius);
            const int rx = std::min(w - 1, x + radius);
            const int cnt = std::max(1, rx - lx + 1);
            const int blurR = (prefR[rx + 1] - prefR[lx]) / cnt;
            const int blurG = (prefG[rx + 1] - prefG[lx]) / cnt;
            const int blurB = (prefB[rx + 1] - prefB[lx]) / cnt;
            const int r = clip8(static_cast<int>((1.0 - blend) * qRed(in[x])   + blend * blurR));
            const int g = clip8(static_cast<int>((1.0 - blend) * qGreen(in[x]) + blend * blurG));
            const int b = clip8(static_cast<int>((1.0 - blend) * qBlue(in[x])  + blend * blurB));
            dst[x] = qRgba(r, g, b, qAlpha(in[x]));
        }
    }
    return out;
}

void fxTracking(QImage& frame, int v, qint64 fidx, double scale)
{
    if (v <= 0) return;
    FastRng rng(static_cast<quint32>(fidx * 7 + 3));
    const int passes = std::max(1, static_cast<int>((v / 15.0) * scale));
    for (int i = 0; i < passes; ++i) {
        if (rng.next01() >= (v / 100.0)) continue;
        const int y = rng.nextInt(0, std::max(0, frame.height() - 10));
        const int lineH = rng.nextInt(1, 5);
        const int shift = static_cast<int>((rng.next01() - 0.5) * v * 2.0);
        for (int yy = y; yy < std::min(frame.height(), y + lineH); ++yy) {
            QRgb* row = reinterpret_cast<QRgb*>(frame.scanLine(yy));
            rollRowSegment(row, frame.width(), 0, frame.width(), shift);
        }
    }
}

void fxGlitch(QImage& frame, int v, int size, qint64 fidx, double scale)
{
    if (v <= 0) return;
    FastRng rng(static_cast<quint32>(fidx * 13));
    if (rng.next01() > (v / 100.0)) return;
    const int passes = std::max(1, static_cast<int>((v / 15.0) * scale));
    for (int i = 0; i < passes; ++i) {
        const int bhMax = std::max(4, static_cast<int>(4 + (size / 100.0) * std::min(40, frame.height())));
        const int bh = rng.nextInt(2, std::min(bhMax, frame.height()));
        const int y = rng.nextInt(0, std::max(0, frame.height() - bh));
        const int shift = static_cast<int>((rng.next01() - 0.5) * frame.width() * 0.3);
        for (int yy = y; yy < y + bh; ++yy) {
            QRgb* row = reinterpret_cast<QRgb*>(frame.scanLine(yy));
            rollRowSegment(row, frame.width(), 0, frame.width(), shift);
        }
    }
}

void fxHeadGlitch(QImage& frame, int v, int size, qint64 fidx, double rectScale, double zoneScale)
{
    if (v <= 0) return;
    FastRng rng(static_cast<quint32>(fidx * 31 + 7));
    if (rng.next01() > std::clamp(v / 85.0, 0.0, 1.0)) return;

    const int h = frame.height();
    const int w = frame.width();
    const double intensity = std::clamp(v / 100.0, 0.0, 1.0);
    const double sizeScale = 0.35 + (size / 100.0) * 1.35;
    const int zoneH = std::clamp(static_cast<int>(h * (0.05 + 0.26 * sizeScale) * zoneScale), 2, h);
    const int passes = std::max(1, static_cast<int>((2.0 + intensity * 20.0) * rectScale));
    const int minBlock = std::max(2, static_cast<int>(std::round(2.0 + sizeScale * 2.6)));
    const int maxBlock = std::max(minBlock + 1, static_cast<int>(std::round(8.0 + sizeScale * 12.0)));

    for (int i = 0; i < passes; ++i) {
        const int y = rng.nextInt(0, std::max(0, zoneH - 1));
        const int x = rng.nextInt(0, std::max(0, w - 2));
        const int base = rng.nextInt(minBlock, maxBlock);
        const int bh = std::min(h - y, std::max(1, base + rng.nextInt(-base / 3, base / 2)));
        const int bw = std::min(w - x, std::max(1, base + rng.nextInt(-base / 3, base / 2)));
        const bool noiseRect = rng.next01() < 0.45;
        const int darkJitter = rng.nextInt(0, static_cast<int>(55.0 * intensity + 1.0));
        for (int yy = y; yy < y + bh; ++yy) {
            QRgb* row = reinterpret_cast<QRgb*>(frame.scanLine(yy));
            for (int xx = x; xx < x + bw; ++xx) {
                if (noiseRect) {
                    const int mix = rng.nextInt(35, 160);
                    row[xx] = qRgba(
                        clip8((qRed(row[xx]) * (255 - mix) + rng.nextInt(0, 255) * mix) / 255),
                        clip8((qGreen(row[xx]) * (255 - mix) + rng.nextInt(0, 255) * mix) / 255),
                        clip8((qBlue(row[xx]) * (255 - mix) + rng.nextInt(0, 255) * mix) / 255),
                        qAlpha(row[xx]));
                } else {
                    const int blackout = rng.nextInt(0, darkJitter);
                    row[xx] = qRgba(blackout, blackout, blackout, qAlpha(row[xx]));
                }
            }
        }
    }
}

void fxInterlace(QImage& frame, int v, int flickerAmount, qint64 fidx)
{
    if (v <= 0) return;
    const double strength = v / 100.0;
    const int shift = static_cast<int>(2 + strength * 4.0);
    const double flickerScale = 0.3 + 0.7 * (flickerAmount / 100.0);
    const double blend = strength * 0.35 * flickerScale * (0.5 + 0.5 * std::sin(fidx * 0.3));
    // Only copy one row at a time instead of the whole frame
    std::vector<QRgb> rowBuf(static_cast<size_t>(frame.width()));
    for (int y = 1; y < frame.height(); y += 2) {
        QRgb* row = reinterpret_cast<QRgb*>(frame.scanLine(y));
        std::copy(row, row + frame.width(), rowBuf.begin());
        for (int x = 0; x < frame.width(); ++x) {
            const int gx = (x - shift + frame.width()) % frame.width();
            row[x] = qRgba(
                clip8(static_cast<int>(qRed(rowBuf[x])   * (1.0 - blend) + qRed(rowBuf[gx])   * blend)),
                clip8(static_cast<int>(qGreen(rowBuf[x]) * (1.0 - blend) + qGreen(rowBuf[gx]) * blend)),
                clip8(static_cast<int>(qBlue(rowBuf[x])  * (1.0 - blend) + qBlue(rowBuf[gx])  * blend)),
                qAlpha(rowBuf[x]));
        }
    }
}

void fxPixelSort(QImage& frame, int v, int size, qint64 fidx, double passScale)
{
    if (v <= 0) return;
    FastRng rng(static_cast<quint32>(fidx * 41 + 11));
    if (rng.next01() > (v / 80.0)) return;
    const int passes = std::max(1, static_cast<int>((v / 15.0) * passScale));

    for (int i = 0; i < passes; ++i) {
        const int x = rng.nextInt(0, std::max(0, frame.width() - 2));
        const double spanScale = 0.01 + (size / 100.0) * 0.08;
        const int maxW = std::max(3, static_cast<int>(frame.width() * spanScale));
        const int x2 = std::min(frame.width(), x + rng.nextInt(2, maxW));
        const int y1 = rng.nextInt(0, std::max(0, frame.height() / 2));
        const int y2 = rng.nextInt(std::max(y1 + 1, frame.height() / 2), std::max(y1 + 1, frame.height() - 1));
        if (x2 - x <= 1 || y2 - y1 <= 1) continue;

        std::vector<int> idx(static_cast<size_t>(y2 - y1));
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            const QRgb* rowA = reinterpret_cast<const QRgb*>(frame.constScanLine(y1 + a));
            const QRgb* rowB = reinterpret_cast<const QRgb*>(frame.constScanLine(y1 + b));
            int sa = 0;
            int sb = 0;
            for (int xx = x; xx < x2; ++xx) {
                sa += qRed(rowA[xx]) + qGreen(rowA[xx]) + qBlue(rowA[xx]);
                sb += qRed(rowB[xx]) + qGreen(rowB[xx]) + qBlue(rowB[xx]);
            }
            return sa < sb;
        });

        std::vector<std::vector<QRgb>> chunk(static_cast<size_t>(y2 - y1));
        for (int r = 0; r < y2 - y1; ++r) {
            const QRgb* src = reinterpret_cast<const QRgb*>(frame.constScanLine(y1 + r));
            chunk[static_cast<size_t>(r)].assign(src + x, src + x2);
        }
        for (int r = 0; r < y2 - y1; ++r) {
            QRgb* dst = reinterpret_cast<QRgb*>(frame.scanLine(y1 + r));
            const auto& sortedRow = chunk[static_cast<size_t>(idx[static_cast<size_t>(r)])];
            std::copy(sortedRow.begin(), sortedRow.end(), dst + x);
        }
    }
}

void fxSineWarp(QImage& frame, int v, qint64 fidx, bool lite)
{
    if (v <= 0) return;
    QImage src = frame.copy();
    const double amp = v * (lite ? 0.22 : 0.3);
    const double phase = fidx * (lite ? 0.055 : 0.08);
    const int stride = lite ? 2 : 1;

    for (int y = 0; y < frame.height(); y += stride) {
        constexpr double kPi = 3.14159265358979323846;
        const int shift = static_cast<int>(amp * std::sin(3.0 * y / std::max(1, frame.height()) * 2.0 * kPi + phase));
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(frame.scanLine(y));
        for (int x = 0; x < frame.width(); ++x) {
            const int sx = (x - shift + frame.width()) % frame.width();
            out[x] = in[sx];
        }
        if (stride > 1 && y + 1 < frame.height()) {
            std::copy(out, out + frame.width(), reinterpret_cast<QRgb*>(frame.scanLine(y + 1)));
        }
    }
}

void addGrain(QImage& img, int amount, int size, qint64 frameIndex)
{
    if (amount <= 0) return;
    FastRng rng(static_cast<quint32>(frameIndex * 1664525ull + 1013904223ull));
    const int strength = std::max(1, amount / 3);
    const double granularity = 1.0 + (size / 100.0) * 11.0;
    const int lowW = std::max(1, static_cast<int>(img.width() / granularity));
    const int lowH = std::max(1, static_cast<int>(img.height() / granularity));
    std::vector<int> lowNoise(static_cast<size_t>(lowW * lowH), 0);
    for (int i = 0; i < lowW * lowH; ++i) {
        lowNoise[static_cast<size_t>(i)] = static_cast<int>(rng.nextU32() & 0xFF) - 128;
    }

    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int ny = std::clamp((y * lowH) / std::max(1, img.height()), 0, lowH - 1);
        for (int x = 0; x < img.width(); ++x) {
            const int nx = std::clamp((x * lowW) / std::max(1, img.width()), 0, lowW - 1);
            const int delta = (lowNoise[static_cast<size_t>(ny * lowW + nx)] * strength) / 32;
            row[x] = qRgba(
                clip8(qRed(row[x]) + delta),
                clip8(qGreen(row[x]) + delta),
                clip8(qBlue(row[x]) + delta),
                qAlpha(row[x]));
        }
    }
}
}

namespace PreviewEffects {
RuntimePreviewConfig buildRuntimePreviewCfg(const AppState::EffectSettings& fx, bool playing)
{
    RuntimePreviewConfig cfg;
    cfg.fx = fx;
    if (!playing) return cfg;

    if (fx.previewMode == AppState::PreviewMode::Draft) {
        cfg.trackingScale = 0.6;
        cfg.glitchScale = 0.45;
        cfg.headRectScale = 0.38;
        cfg.headZoneScale = 0.5;
        cfg.pixelPassScale = 0.24;
        cfg.sineLite = true;
        cfg.degraded = true;
        cfg.reasons << QStringLiteral("simplify:tracking")
                    << QStringLiteral("simplify:glitch")
                    << QStringLiteral("simplify:head")
                    << QStringLiteral("simplify:pixel-sort");
    } else if (fx.previewMode == AppState::PreviewMode::Balanced) {
        cfg.trackingScale = 0.86;
        cfg.glitchScale = 0.72;
        cfg.headRectScale = 0.62;
        cfg.headZoneScale = 0.78;
        cfg.pixelPassScale = 0.38;
        cfg.sineLite = true;
        cfg.degraded = true;
        cfg.reasons << QStringLiteral("simplify:head")
                    << QStringLiteral("simplify:pixel-sort");
    } else if (fx.previewMode == AppState::PreviewMode::Ultra) {
        cfg.trackingScale = 0.95;
        cfg.glitchScale = 0.84;
        cfg.headRectScale = 0.78;
        cfg.headZoneScale = 0.92;
        cfg.pixelPassScale = 0.7;
        cfg.reasons << QStringLiteral("ultra:optimized-preview");
    }
    return cfg;
}

RuntimePreviewConfig buildRuntimeExportCfg(const AppState::EffectSettings& fx)
{
    RuntimePreviewConfig cfg;
    cfg.fx = fx;
    cfg.trackingScale = 1.0;
    cfg.glitchScale = 1.0;
    cfg.headRectScale = 1.0;
    cfg.headZoneScale = 1.0;
    cfg.pixelPassScale = 1.0;
    cfg.sineLite = false;
    return cfg;
}

QImage applyPreview(const QImage& source, const RuntimePreviewConfig& cfg, qint64 frameIndex)
{
    if (source.isNull()) return source;
    QImage out = toArgb(source);
    const auto& fx = cfg.fx;

    out = fxChroma(out, fx.chromaShift);
    out = fxBleed(out, fx.colorBleed);
    fxSineWarp(out, fx.sineWarp, frameIndex, cfg.sineLite);
    fxTracking(out, fx.tracking, frameIndex, cfg.trackingScale);
    fxGlitch(out, fx.glitch, fx.glitchBlockSize, frameIndex, cfg.glitchScale);
    fxHeadGlitch(out, fx.headGlitch, fx.headGlitchSize, frameIndex, cfg.headRectScale, cfg.headZoneScale);
    fxPixelSort(out, fx.pixelSort, fx.pixelSortSize, frameIndex, cfg.pixelPassScale);
    fxInterlace(out, fx.interlace, fx.flickerAmount, frameIndex);
    addGrain(out, fx.grain, fx.grainSize, frameIndex);

    return out;
}

QImage applyExport(const QImage& source, const RuntimePreviewConfig& cfg, qint64 frameIndex)
{
    return applyPreview(source, cfg, frameIndex);
}
}
