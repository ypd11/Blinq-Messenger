#include "windowsmediawatcher.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>
#endif

namespace {
QString cleanMediaPart(const QString &text)
{
    QString result = text.trimmed();
    result.replace(QChar(0x2013), QLatin1Char('-'));
    result.replace(QChar(0x2014), QLatin1Char('-'));
    return result.simplified();
}

#ifdef Q_OS_WIN
QString fromHString(const winrt::hstring &text)
{
    return cleanMediaPart(QString::fromWCharArray(text.c_str()));
}
#endif
}

WindowsMediaWatcher::WindowsMediaWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &WindowsMediaWatcher::poll);
}

void WindowsMediaWatcher::start()
{
    poll();
    m_timer.start();
}

void WindowsMediaWatcher::poll()
{
#ifdef Q_OS_WIN
    QString mediaText;
    try {
        static bool apartmentInitialized = false;
        if (!apartmentInitialized) {
            try {
                winrt::init_apartment(winrt::apartment_type::single_threaded);
            } catch (const winrt::hresult_error &) {
                // Qt or another library may already have initialized COM for this thread.
            }
            apartmentInitialized = true;
        }

        using namespace winrt::Windows::Media::Control;
        const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        const auto session = manager.GetCurrentSession();
        if (session) {
            const auto playbackInfo = session.GetPlaybackInfo();
            if (playbackInfo
                && playbackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                const auto properties = session.TryGetMediaPropertiesAsync().get();
                const QString title = fromHString(properties.Title());
                QString artist = fromHString(properties.Artist());
                if (artist.isEmpty()) {
                    artist = fromHString(properties.AlbumArtist());
                }
                if (!title.isEmpty() && !artist.isEmpty()) {
                    mediaText = QStringLiteral("%1 - %2").arg(title, artist);
                } else if (!title.isEmpty()) {
                    mediaText = title;
                } else {
                    mediaText = fromHString(session.SourceAppUserModelId());
                }
            }
        }
    } catch (const winrt::hresult_error &) {
        mediaText.clear();
    } catch (...) {
        mediaText.clear();
    }

    if (mediaText != m_lastText) {
        m_lastText = mediaText;
        emit mediaTextChanged(mediaText);
    }
#else
    if (!m_lastText.isEmpty()) {
        m_lastText.clear();
        emit mediaTextChanged(QString());
    }
#endif
}
