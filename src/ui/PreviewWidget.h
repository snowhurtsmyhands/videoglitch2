#pragma once

#include <QImage>
#include <QWidget>

class AppState;

class PreviewWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWidget(AppState* state, QWidget* parent = nullptr);

    void setFrame(const QImage& image);

signals:
    void browseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString formatTimeText() const;
    QRect videoRect() const;

    AppState* m_state;
    QImage m_frame;
};
