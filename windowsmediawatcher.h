#ifndef WINDOWSMEDIAWATCHER_H
#define WINDOWSMEDIAWATCHER_H

#include <QObject>
#include <QTimer>

class WindowsMediaWatcher : public QObject
{
    Q_OBJECT

public:
    explicit WindowsMediaWatcher(QObject *parent = nullptr);
    void start();

signals:
    void mediaTextChanged(const QString &text);

private:
    void poll();

    QTimer m_timer;
    QString m_lastText;
};

#endif // WINDOWSMEDIAWATCHER_H
