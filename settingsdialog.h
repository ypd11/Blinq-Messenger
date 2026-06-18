#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QMap>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

struct AppSettings
{
    bool showNotifications = true;
    bool minimizeToTray = true;
    bool openChatOnMessage = false;
    bool launchWithWindows = false;
    bool saveHistory = true;
    bool muteSounds = false;
    bool showPlayingInfo = true;
    bool hideTypingIndicator = false;
    bool awayAutoReply = false;
    QString awayAutoReplyMessage = QStringLiteral("I'm away right now. I'll reply when I'm back.");
    bool internetSearchable = true;
    bool directMessageNotifications = true;
    bool publicChatNotifications = true;
    QString appMode = QStringLiteral("lan");
    QString internetServerHost = QStringLiteral("66.154.104.66");
    int internetServerPort = 45476;
    QString internetAuthToken;
    QString internetBlinqId;
    QString internetDisplayName;
    QMap<QString, QString> blockedPeers;
    QStringList manualPeerAddresses;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);

    AppSettings settings() const;

signals:
    void directConnectRequested();
    void manualPeerConnectRequested(const QString &address);
    void resetSettingsRequested();
    void backupDataRequested();
    void restoreDataRequested();
    void deleteBlinqAccountRequested();
    void changeBlinqPasswordRequested();

private:
    QCheckBox *m_notificationsCheck = nullptr;
    QCheckBox *m_directNotificationsCheck = nullptr;
    QCheckBox *m_publicNotificationsCheck = nullptr;
    QCheckBox *m_minimizeToTrayCheck = nullptr;
    QCheckBox *m_openChatOnMessageCheck = nullptr;
    QCheckBox *m_launchWithWindowsCheck = nullptr;
    QCheckBox *m_saveHistoryCheck = nullptr;
    QCheckBox *m_showPlayingInfoCheck = nullptr;
    QCheckBox *m_hideTypingIndicatorCheck = nullptr;
    QCheckBox *m_awayAutoReplyCheck = nullptr;
    QCheckBox *m_internetSearchableCheck = nullptr;
    QLineEdit *m_awayAutoReplyEdit = nullptr;
    QLabel *m_blockedEmptyLabel = nullptr;
    QLabel *m_manualEmptyLabel = nullptr;
    QListWidget *m_blockedList = nullptr;
    QListWidget *m_manualPeersList = nullptr;
    QStringList m_savedManualPeerAddresses;
};

#endif // SETTINGSDIALOG_H
