#include "export/ExportWorker.h"

#include "effects/PreviewEffects.h"

#include <QImage>
#include <QProcess>
#include <algorithm>
#include <utility>

namespace {
double parseFps(const QString& value)
{
    if (value.contains('/')) {
        const auto parts = value.split('/');
        if (parts.size() == 2) {
            const double num = parts[0].toDouble();
            const double den = parts[1].toDouble();
            if (den > 0.0) return num / den;
        }
    }
    const double fps = value.toDouble();
    return fps > 0.0 ? fps : 24.0;
}

bool hasNvenc()
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    if (!p.waitForFinished(5000) || p.exitCode() != 0) return false;
    return QString::fromUtf8(p.readAllStandardOutput()).contains(QStringLiteral("h264_nvenc"));
}

bool canCopyAudioCodec(const QString& codec)
{
    const QString c = codec.trimmed().toLower();
    return c == QStringLiteral("aac") || c == QStringLiteral("mp3") || c == QStringLiteral("alac");
}
}

ExportWorker::ExportWorker(QString inputPath, QString outputPath, AppState::EffectSettings fx, AppState::ExportQuality quality)
    : m_inputPath(std::move(inputPath))
    , m_outputPath(std::move(outputPath))
    , m_fx(fx)
    , m_quality(quality)
{
}

void ExportWorker::run()
{
    QProcess probe;
    probe.start(QStringLiteral("ffprobe"), {
                                            QStringLiteral("-v"), QStringLiteral("error"),
                                            QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                            QStringLiteral("-show_entries"), QStringLiteral("stream=width,height,r_frame_rate"),
                                            QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                            QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                                            m_inputPath });
    if (!probe.waitForFinished(10000) || probe.exitCode() != 0) {
        emit finished(false, QStringLiteral("ffprobe failed. Is ffprobe installed?"));
        return;
    }
    const QStringList lines = QString::fromUtf8(probe.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    if (lines.size() < 4) {
        emit finished(false, QStringLiteral("Could not read input video metadata."));
        return;
    }

    const int width = lines[0].toInt();
    const int height = lines[1].toInt();
    const double fps = parseFps(lines[2]);
    const double durationSec = lines[3].toDouble();
    if (width <= 0 || height <= 0) {
        emit finished(false, QStringLiteral("Invalid source dimensions from ffprobe."));
        return;
    }

    const qint64 frameSize = static_cast<qint64>(width) * static_cast<qint64>(height) * 4;
    const qint64 totalFrames = std::max<qint64>(1, static_cast<qint64>(durationSec * fps));
    QString audioCodec;
    {
        QProcess audioProbe;
        audioProbe.start(QStringLiteral("ffprobe"), {
                                                    QStringLiteral("-v"), QStringLiteral("error"),
                                                    QStringLiteral("-select_streams"), QStringLiteral("a:0"),
                                                    QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name"),
                                                    QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                                                    m_inputPath });
        if (audioProbe.waitForFinished(5000) && audioProbe.exitCode() == 0) {
            audioCodec = QString::fromUtf8(audioProbe.readAllStandardOutput()).trimmed();
        }
    }

    QProcess decoder;
    decoder.setProcessChannelMode(QProcess::SeparateChannels);
    decoder.start(QStringLiteral("ffmpeg"), {
                                             QStringLiteral("-v"), QStringLiteral("error"),
                                             QStringLiteral("-i"), m_inputPath,
                                             QStringLiteral("-f"), QStringLiteral("rawvideo"),
                                             QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                                             QStringLiteral("-vsync"), QStringLiteral("0"),
                                             QStringLiteral("-") });
    if (!decoder.waitForStarted(10000)) {
        emit finished(false, QStringLiteral("Could not start ffmpeg decoder."));
        return;
    }

    QStringList encoderArgs {
        QStringLiteral("-y"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height),
        QStringLiteral("-r"), QString::number(fps, 'f', 6),
        QStringLiteral("-i"), QStringLiteral("-"),
        QStringLiteral("-i"), m_inputPath,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map"), QStringLiteral("1:a?")
    };

    const bool useNvenc = hasNvenc();
    if (useNvenc) {
        // NVENC CQ: lower = better quality / larger file
        int cq = 28; // Balanced
        if (m_quality == AppState::ExportQuality::Small) cq = 34;
        if (m_quality == AppState::ExportQuality::High)  cq = 22;
        encoderArgs << QStringLiteral("-c:v") << QStringLiteral("h264_nvenc")
                    << QStringLiteral("-preset") << QStringLiteral("p5")
                    << QStringLiteral("-cq") << QString::number(cq)
                    << QStringLiteral("-b:v") << QStringLiteral("0")
                    << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    } else {
        // libx264 CRF: higher = more compression / smaller file
        int crf = 23;   // Balanced — was 22, ~30% smaller output
        QString preset = QStringLiteral("faster"); // was "medium"; faster encodes smaller too
        if (m_quality == AppState::ExportQuality::Small) {
            crf = 28;
            preset = QStringLiteral("fast");
        }
        if (m_quality == AppState::ExportQuality::High) {
            crf = 18;
            preset = QStringLiteral("medium"); // keep medium for quality
        }
        encoderArgs << QStringLiteral("-c:v") << QStringLiteral("libx264")
                    << QStringLiteral("-preset") << preset
                    << QStringLiteral("-crf") << QString::number(crf)
                    << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    if (canCopyAudioCodec(audioCodec)) {
        encoderArgs << QStringLiteral("-c:a") << QStringLiteral("copy");
    } else {
        encoderArgs << QStringLiteral("-c:a") << QStringLiteral("aac")
                    << QStringLiteral("-b:a") << QStringLiteral("128k");
    }
    encoderArgs << QStringLiteral("-shortest") << m_outputPath;

    QProcess encoder;
    encoder.setProcessChannelMode(QProcess::SeparateChannels);
    encoder.start(QStringLiteral("ffmpeg"), encoderArgs);
    if (!encoder.waitForStarted(10000)) {
        emit finished(false, QStringLiteral("Could not start ffmpeg encoder."));
        decoder.kill();
        decoder.waitForFinished(3000);
        return;
    }

    const auto cfg = PreviewEffects::buildRuntimePreviewCfg(m_fx, false);
    QByteArray raw;
    raw.reserve(static_cast<int>(frameSize * 2));
    qint64 frameIndex = 0;
    int lastProgress = -1;

    while (decoder.state() != QProcess::NotRunning || decoder.bytesAvailable() > 0) {
        if (!decoder.waitForReadyRead(50) && decoder.state() == QProcess::NotRunning) {
            break;
        }
        raw.append(decoder.readAllStandardOutput());
        while (raw.size() >= frameSize) {
            const QByteArray chunk = raw.left(static_cast<int>(frameSize));
            raw.remove(0, static_cast<int>(frameSize));

            QImage src(reinterpret_cast<const uchar*>(chunk.constData()), width, height, width * 4, QImage::Format_ARGB32);
            QImage processed = PreviewEffects::applyExport(src.copy(), cfg, frameIndex);

            encoder.write(reinterpret_cast<const char*>(processed.constBits()), static_cast<qint64>(processed.sizeInBytes()));
            if (!encoder.waitForBytesWritten(10000)) {
                emit finished(false, QStringLiteral("Failed writing frame to ffmpeg encoder."));
                decoder.kill();
                encoder.kill();
                return;
            }

            ++frameIndex;
            const int percent = std::clamp(static_cast<int>((100.0 * frameIndex) / totalFrames), 0, 100);
            if (percent != lastProgress) {
                lastProgress = percent;
                emit progressChanged(percent);
            }
        }
    }

    encoder.closeWriteChannel();
    decoder.waitForFinished(-1);
    encoder.waitForFinished(-1);

    if (decoder.exitStatus() != QProcess::NormalExit || decoder.exitCode() != 0) {
        emit finished(false, QStringLiteral("ffmpeg decoder failed during export."));
        return;
    }
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0) {
        const QString err = QString::fromUtf8(encoder.readAllStandardError());
        emit finished(false, QStringLiteral("ffmpeg encoder failed: %1").arg(err.isEmpty() ? QStringLiteral("unknown error") : err));
        return;
    }

    emit progressChanged(100);
    emit finished(true, QStringLiteral("Export finished: %1").arg(m_outputPath));
}
