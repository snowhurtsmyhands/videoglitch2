#include "app/MainWindow.h"

#include "app/AppState.h"
#include "media/MediaEngine.h"
#include "preset/PresetManager.h"
#include "export/ExportWorker.h"
#include "ui/ControlPanel.h"
#include "ui/PreviewWidget.h"
#include "ui/TransportBar.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QString formatMs(qint64 value)
{
    const qint64 totalSec = value / 1000;
    const qint64 min = totalSec / 60;
    const qint64 sec = totalSec % 60;
    return QStringLiteral("%1:%2").arg(min).arg(sec, 2, 10, QLatin1Char('0'));
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_state(new AppState(this))
    , m_mediaEngine(new MediaEngine(m_state, this))
    , m_presetManager(new PresetManager(this))
{
    setWindowTitle(QStringLiteral("AKERA SKY — Glitch Studio Premium"));
    resize(1420, 900);
    setMinimumSize(1120, 700);
    applyTheme();

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(14);

    auto* leftWrap = new QWidget(central);
    auto* leftLayout = new QVBoxLayout(leftWrap);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto* header = new QLabel(QStringLiteral("// AKERA SKY  GLITCH STUDIO  PREMIUM"), this);
    leftLayout->addWidget(header);

    m_previewWidget = new PreviewWidget(m_state, this);
    leftLayout->addWidget(m_previewWidget, 1);

    m_transportBar = new TransportBar(this);
    leftLayout->addWidget(m_transportBar);

    auto* statusLine = new QLabel(m_mediaEngine->backendSummary(), this);
    leftLayout->addWidget(statusLine);

    m_controlPanel = new ControlPanel(m_state, m_presetManager, this);
    m_controlPanel->setFixedWidth(420);

    root->addWidget(leftWrap, 1);
    root->addWidget(m_controlPanel);

    statusBar()->showMessage(QStringLiteral("Ready"));
    m_transportBar->setPerfText(QStringLiteral("Balanced • GStreamer preview • auto aspect"));

    connect(m_previewWidget, &PreviewWidget::browseRequested, this, &MainWindow::browseForVideo);
    connect(m_controlPanel, &ControlPanel::exportRequested, this, &MainWindow::onExportRequested);
    connect(m_mediaEngine, &MediaEngine::frameReady, m_previewWidget, &PreviewWidget::setFrame);
    connect(m_mediaEngine, &MediaEngine::statusChanged, this, [this](const QString& text) {
        statusBar()->showMessage(text);
    });
    connect(m_transportBar, &TransportBar::playClicked, m_mediaEngine, &MediaEngine::togglePlayPause);
    connect(m_transportBar, &TransportBar::seekRequested, m_mediaEngine, &MediaEngine::setPositionMs);
    connect(m_mediaEngine, &MediaEngine::playbackStateChanged, m_transportBar, &TransportBar::setPlaying);
    connect(m_mediaEngine, &MediaEngine::perfTextChanged, m_transportBar, &TransportBar::setPerfText);
    connect(m_mediaEngine, &MediaEngine::positionChanged, this, [this](qint64 pos, qint64 dur) {
        m_transportBar->setDurationAndPosition(dur, pos);
        m_transportBar->setFrameText(QStringLiteral("%1 / %2").arg(formatMs(pos)).arg(formatMs(dur)));
        // Keep export size estimate in sync whenever duration becomes known
        if (dur > 0) {
            m_controlPanel->updateExportSizeEstimate(dur);
        }
    });
}

void MainWindow::browseForVideo()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Video"),
        QString(),
        QStringLiteral("Video Files (*.mp4 *.mov *.mkv *.avi *.m4v *.webm);;All Files (*.*)"));

    if (path.isEmpty()) {
        return;
    }

    m_state->setVideoPath(path);
    m_mediaEngine->loadFile(path);
}

void MainWindow::onExportRequested()
{
    if (m_state->videoPath().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No video"), QStringLiteral("Load a video first."));
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export MP4"),
        QString(),
        QStringLiteral("MP4 Video (*.mp4)"));
    if (outPath.isEmpty()) {
        return;
    }
    if (m_exportThread) {
        QMessageBox::information(this, QStringLiteral("Export running"), QStringLiteral("An export is already in progress."));
        return;
    }

    m_controlPanel->setExportBusy(true);
    m_controlPanel->setExportProgress(0);
    statusBar()->showMessage(QStringLiteral("Export started..."));

    m_exportThread = new QThread(this);
    auto* worker = new ExportWorker(m_state->videoPath(), outPath, m_state->effectSettings(), m_state->exportQuality());
    worker->moveToThread(m_exportThread);

    connect(m_exportThread, &QThread::started, worker, &ExportWorker::run);
    connect(worker, &ExportWorker::progressChanged, this, [this](int p) {
        m_controlPanel->setExportProgress(p);
        statusBar()->showMessage(QStringLiteral("Exporting... %1%").arg(p));
    });
    connect(worker, &ExportWorker::finished, this, [this, worker](bool ok, const QString& message) {
        m_controlPanel->setExportBusy(false);
        statusBar()->showMessage(message);
        if (ok) {
            QMessageBox::information(this, QStringLiteral("Export complete"), message);
        } else {
            QMessageBox::critical(this, QStringLiteral("Export failed"), message);
        }
        worker->deleteLater();
        if (m_exportThread) {
            m_exportThread->quit();
            m_exportThread->wait();
            m_exportThread->deleteLater();
            m_exportThread = nullptr;
        }
    });
    m_exportThread->start();
}

void MainWindow::applyTheme()
{
    const QString style = QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #090909;
            color: #d9d9d0;
            font-family: 'Segoe UI';
            font-size: 12px;
        }
        QLabel {
            color: #9c9c9c;
        }
        QPushButton {
            background: #111111;
            color: #e8e8df;
            border: 1px solid #2a2a2a;
            border-radius: 8px;
            padding: 8px 12px;
        }
        QPushButton:hover {
            border-color: #3f3f3f;
        }
        QPushButton:checked {
            background: #c8ff38;
            color: #070707;
            border-color: #c8ff38;
            font-weight: 600;
        }
        QLineEdit, QComboBox {
            background: #0c0c0c;
            border: 1px solid #252525;
            border-radius: 8px;
            padding: 8px 10px;
            min-height: 18px;
        }
        QScrollArea {
            border: none;
        }
        QSlider::groove:horizontal {
            background: #1a1a1a;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #2a2a2a;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #c8ff38;
            width: 14px;
            margin: -6px 0;
            border-radius: 7px;
        }
        QCheckBox {
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 5px;
            border: 1px solid #2a2a2a;
            background: #121212;
        }
        QCheckBox::indicator:checked {
            background: #c8ff38;
            border-color: #c8ff38;
        }
        QStatusBar {
            color: #8a8a8a;
        }
        QFrame[frameShape="4"] {
            color: #232323;
            background: #232323;
        }
    )");
    setStyleSheet(style);
}
