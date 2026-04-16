#pragma once

#include <QMainWindow>

class AppState;
class ControlPanel;
class MediaEngine;
class PresetManager;
class PreviewWidget;
class TransportBar;
class ExportWorker;
class QThread;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void browseForVideo();
    void onExportRequested();

private:
    void applyTheme();

    AppState* m_state = nullptr;
    MediaEngine* m_mediaEngine = nullptr;
    PresetManager* m_presetManager = nullptr;
    PreviewWidget* m_previewWidget = nullptr;
    ControlPanel* m_controlPanel = nullptr;
    TransportBar* m_transportBar = nullptr;
    QThread* m_exportThread = nullptr;
};
