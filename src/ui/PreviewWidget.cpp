#include "ui/PreviewWidget.h"
#include "app/AppState.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

PreviewWidget::PreviewWidget(AppState* state, QWidget* parent)
    : QWidget(parent), m_state(state)
{
    setMinimumSize(640, 360);
    setAutoFillBackground(false);

    connect(m_state, &AppState::stateChanged, this, QOverload<>::of(&PreviewWidget::update));
}

void PreviewWidget::setFrame(const QImage& image)
{
    m_frame = image;
    update();
}

void PreviewWidget::setPlaybackPositionMs(qint64 positionMs)
{
    const qint64 clamped = std::max<qint64>(0, positionMs);
    if (m_positionMs == clamped) {
        return;
    }
    m_positionMs = clamped;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor("#050505"));

    const QRect outer = rect().adjusted(0, 0, -1, -1);
    p.setPen(QPen(QColor("#1a1a1a"), 1.0));
    p.drawRoundedRect(outer, 8, 8);

    const QRect target = videoRect();
    p.fillRect(target, QColor("#080808"));

    if (!m_frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage scaled = m_frame.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint topLeft(target.x() + (target.width() - scaled.width()) / 2,
                             target.y() + (target.height() - scaled.height()) / 2);
        p.drawImage(topLeft, scaled);
    } else {
        p.setPen(QColor("#515151"));
        p.setFont(QFont(QStringLiteral("Consolas"), 14));
        p.drawText(target, Qt::AlignCenter, QStringLiteral("DROP VIDEO HERE\n\nor click to browse"));
    }

    if (m_state->timecodeEnabled()) {
        const int x = target.left() + (target.width() * m_state->timecodeX()) / 100;
        const int y = target.top() + (target.height() * m_state->timecodeY()) / 100;
        QFont f(QStringLiteral("Consolas"));
        f.setPixelSize(m_state->timecodeSize());
        p.setFont(f);
        const QString text = m_state->timecodeTemplate().replace(QStringLiteral("{time}"), formatTimeText());
        p.setPen(QColor(10, 10, 10, 190));
        p.drawText(x + 2, y + 2, text);
        p.setPen(m_state->timecodeColor());
        p.drawText(x, y, text);
    }
}

void PreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit browseRequested();
    }
}

QString PreviewWidget::formatTimeText() const
{
    const qint64 totalSec = m_positionMs / 1000;
    const qint64 hh = totalSec / 3600;
    const qint64 mm = (totalSec / 60) % 60;
    const qint64 ss = totalSec % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'));
}

QRect PreviewWidget::videoRect() const
{
    const QRect area = rect().adjusted(14, 14, -14, -14);
    if (m_frame.isNull()) {
        const double aspect = 16.0 / 9.0;
        int w = area.width();
        int h = static_cast<int>(w / aspect);
        if (h > area.height()) {
            h = area.height();
            w = static_cast<int>(h * aspect);
        }
        const int x = area.x() + (area.width() - w) / 2;
        const int y = area.y() + (area.height() - h) / 2;
        return QRect(x, y, w, h);
    }

    QSize fitted = m_frame.size();
    fitted.scale(area.size(), Qt::KeepAspectRatio);
    const int x = area.x() + (area.width() - fitted.width()) / 2;
    const int y = area.y() + (area.height() - fitted.height()) / 2;
    return QRect(x, y, fitted.width(), fitted.height());
}
