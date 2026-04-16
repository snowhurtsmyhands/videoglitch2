#pragma once

#include <QObject>
#include <QString>

#include "app/AppState.h"

class ExportWorker final : public QObject
{
    Q_OBJECT
public:
    ExportWorker(QString inputPath, QString outputPath, AppState::EffectSettings fx, AppState::ExportQuality quality);

public slots:
    void run();

signals:
    void progressChanged(int percent);
    void finished(bool ok, const QString& message);

private:
    QString m_inputPath;
    QString m_outputPath;
    AppState::EffectSettings m_fx;
    AppState::ExportQuality m_quality = AppState::ExportQuality::Balanced;
};
