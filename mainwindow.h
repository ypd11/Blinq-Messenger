#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QColor>
#include <QMainWindow>
#include <QLabel>
#include <QSet>
#include <QStringList>

class ChatWindow;
class PublicChatWindow;
class WindowsMediaWatcher;
#include "internetrelayservice.h"
#include "lanchatservice.h"
#include "settingsdialog.h"
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QCloseEvent;
class QDialog;
class QMenu;
class QSettings;
class QSoundEffect;
class QSystemTrayIcon;
class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MarqueeLabel : public QLabel
{
    Q_OBJECT

public:
    explicit MarqueeLabel(QWidget *parent = nullptr);
    void setText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateAnimation();

    QTimer *m_timer = nullptr;
    QString m_text;
    int m_offset = 0;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void showFromTray();

private:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void buildMenus();
    void buildTrayIcon();
    void connectSignals();
    void loadSettings();
    void saveSettings() const;
    void applyLaunchWithWindowsSetting() const;
    void applyTheme();
    QString appStyleSheet() const;
    void upsertPeer(const ChatPeer &peer);
    void removePeer(const QString &peerId, bool appClosed);
    void openChat(const QString &peerId);
    void openPublicChat();
    void showInternetSignInDialog();
    void showAddInternetContactDialog();
    void switchAppMode();
    void deleteBlinqAccount();
    void changeBlinqPassword();
    void handleInternetServerUnavailable(const QString &reason);
    void restartApplication();
    void sendPendingReadReceipts(const QString &peerId);
    void changeDisplayName();
    void showDirectConnectDialog();
    void showConnectionInviteDialog();
    void showFirewallHelperDialog();
    void scanLocalSubnet();
    void reconnectSavedContacts();
    void showSettings();
    void showGroupManager();
    void resetAppSettings();
    void backupAppData();
    void restoreAppData();
    void showWelcomeDialog();
    void showAbout();
    void showHelp();
    void showUpdateDialog();
    void showConnectionInfo();
    QWidget *dialogParent() const;
    bool showIncomingNotification(const ChatPeer &peer, const QString &message);
    bool showPublicIncomingNotification(const QString &sender, const QString &message);
    void showNativeNotification(const QString &title, const QString &message, const QIcon &icon);
    void openPendingNotification();
    void markChatActive();
    void markChatIdle();
    void updateTrayTooltip();
    void updateNetworkStatus();
    void updateEmptyContactsLabel();
    void updateContactsLabel();
    void rebuildContactList();
    QString contactGroupKey(const ChatPeer &peer) const;
    QString contactGroupTitle(const QString &groupKey, int count) const;
    QString contactCustomGroupName(const QString &groupKey) const;
    void showSignInNotification(const ChatPeer &peer);
    void updatePublicChatParticipants();
    void rebuildInternetContacts();
    void upsertInternetPeer(const InternetRelayPeer &peer);
    bool isInternetMode() const;
    bool isInternetPeer(const QString &peerId) const;
    ChatPeer chatPeerFromInternetPeer(const InternetRelayPeer &peer) const;
    void queueOfflineMessage(const QString &peerId, const QString &message, bool isHtml);
    void flushPendingOfflineMessages(const QString &peerId);
    void showQueuedMessages(const QString &peerId, ChatWindow *parentWindow);
    void maybeSendAwayAutoReply(const QString &peerId, const QString &incomingMessage, bool isHtml);
    bool shouldSendAwayAutoReply(const QString &peerId, const QString &incomingMessage, bool isHtml) const;
    void loadOfflineMessages(const QSettings &settings);
    void saveOfflineMessages(QSettings &settings) const;
    void loadKnownPeers(const QSettings &settings);
    void saveKnownPeers(QSettings &settings) const;
    QStringList rememberedPeerAddresses() const;
    QStringList localIpv4Addresses() const;
    QString connectionInviteText() const;
    void startBackgroundConnectionAttempts(const QStringList &addresses, const QString &label, bool openFirstChat);
    bool allowRateLimitedAction(const QString &key, int cooldownMs);
    void showNetworkDiagnostics();
    void setupSounds();
    QString prepareSoundFile(const QString &resourcePath, const QString &fileName) const;
    void playSentSound();
    void playReceivedSound();
    void playNotificationSound();
    void playWhistleSound();
    void playSound(QSoundEffect *sound);
    void setSoundsMuted(bool muted);
    void clearSelectedHistory();
    void blockSelectedPeer();
    void unblockSelectedPeer();
    void buzzSelectedPeer();
    bool makeAvailableForOutgoingPrivateAction(ChatWindow *window = nullptr);
    void startActivity(const QString &peerId, const QString &activity);
    void startRockPaperScissors(const QString &peerId);
    void startTicTacToe(const QString &peerId);
    void openDrawingPad(const QString &peerId);
    void sendWink(const QString &peerId);
    void showWink(const QString &peerName, const QString &kind);
    void handleGameAction(const QString &peerId, const QString &peerName, const QString &game, const QString &action, const QString &gameId, const QString &value, const QString &board);
    void showPeerContextMenu(const QPoint &position);
    void showContactInfo(const QString &peerId);
    void updateLocalProfile();
    QIcon avatarIcon(const QByteArray &avatarData, const QString &fallbackText) const;
    QPixmap avatarPixmap(const QByteArray &avatarData, const QString &fallbackText, int size, const QColor &frameColor = QColor()) const;
    QPixmap avatarPixmapWithStatus(const QByteArray &avatarData, const QString &fallbackText, const QString &status, int size, const QColor &frameColor = QColor()) const;
    QWidget *createGroupHeader(const QString &groupKey, int count);
    QWidget *createPeerRow(const ChatPeer &peer);
    QString selectedPeerId() const;
    QString peerIdForManualConnection(const QString &address, const QString &peerName) const;
    ChatWindow *chatWindowFor(const QString &peerId);
    QString promptRpsChoice(const QString &title) const;
    QString tttBoardHtml(const QString &board) const;
    QString tttWinner(const QString &board) const;
    bool promptTicTacToeMove(QString *board, const QString &mark, const QString &title) const;

    Ui::MainWindow *ui;
    LanChatService *m_chatService = nullptr;
    InternetRelayService *m_internetRelay = nullptr;
    QListWidget *m_peerList = nullptr;
    QComboBox *m_statusCombo = nullptr;
    QLabel *m_localAvatar = nullptr;
    QLabel *m_localName = nullptr;
    QLabel *m_contactsLabel = nullptr;
    QLabel *m_contactsTotalLabel = nullptr;
    QLabel *m_networkStatus = nullptr;
    QLabel *m_emptyContactsLabel = nullptr;
    MarqueeLabel *m_mediaLabel = nullptr;
    WindowsMediaWatcher *m_mediaWatcher = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QSoundEffect *m_sentSound = nullptr;
    QSoundEffect *m_receivedSound = nullptr;
    QSoundEffect *m_notificationSound = nullptr;
    QSoundEffect *m_whistleSound = nullptr;
    QTimer *m_chatIdleTimer = nullptr;
    QTimer *m_networkStatusTimer = nullptr;
    QAction *m_muteSoundsAction = nullptr;
    bool m_reallyQuit = false;
    bool m_openNextManualConnectionChat = true;
    int m_backgroundConnectionAttempts = 0;
    int m_directConnectDialogsOpen = 0;
    bool m_openFirstBackgroundConnectionChat = false;
    QHash<QString, ChatPeer> m_peers;
    QHash<QString, ChatPeer> m_knownPeers;
    QHash<QString, InternetRelayPeer> m_internetPeers;
    QHash<QString, QListWidgetItem *> m_peerItems;
    QHash<QString, bool> m_groupCollapsed;
    QSet<QString> m_favoritePeers;
    QSet<QString> m_seenOnlineNotifications;
    QSet<QString> m_typingPeers;
    QStringList m_customContactGroups;
    QHash<QString, QString> m_peerGroups;
    QHash<QString, ChatWindow *> m_chatWindows;
    QHash<QString, QStringList> m_pendingReadReceipts;
    QHash<QString, QString> m_rpsChoices;
    QHash<QString, QString> m_tttMarks;
    struct PendingOfflineMessage
    {
        QString message;
        bool isHtml = false;
        QDateTime queuedAt;
    };
    QHash<QString, QList<PendingOfflineMessage>> m_offlineMessages;
    QHash<QString, QDateTime> m_lastRateLimitedActions;
    QHash<QString, QDateTime> m_autoReplySentAt;
    QSet<QString> m_protocolWarningsShown;
    PublicChatWindow *m_publicChatWindow = nullptr;
    QSet<QString> m_blockedPeers;
    QSet<QSoundEffect *> m_pendingSoundPlays;
    QHash<QString, QString> m_blockedPeerNames;
    QString m_pendingNotificationPeerId;
    QString m_pendingNotificationAction;
    QString m_draggedPeerId;
    QString m_manualPersonalMessage;
    QString m_currentMediaText;
    bool m_pendingNotificationIsPublic = false;
    bool m_authDialogOpen = false;
    bool m_isLoading = true;
    bool m_suppressSignInNotifications = true;
    AppSettings m_settings;
};

#endif // MAINWINDOW_H
