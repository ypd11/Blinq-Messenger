#ifndef LANCHATSERVICE_H
#define LANCHATSERVICE_H

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QAtomicInt>
#include <QSharedPointer>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

struct ChatPeer
{
    QString id;
    QString name;
    QString status;
    QString personalMessage;
    QString themeColor;
    QByteArray avatarData;
    QByteArray publicKeyData;
    QHostAddress address;
    quint16 port = 0;
    QDateTime lastSeen;
    bool publicChatOpen = false;
    int protocolVersion = 1;
};

class LanChatService : public QObject
{
    Q_OBJECT

public:
    explicit LanChatService(QObject *parent = nullptr);
    ~LanChatService() override;

    QString localName() const;
    QString localStatus() const;
    QString effectiveLocalStatus() const;
    QString localPersonalMessage() const;
    QByteArray localAvatarData() const;
    QString connectionInfo() const;
    quint16 discoveryPort() const;
    bool discoveryAvailable() const;
    QString discoveryError() const;
    quint16 directTcpPort() const;
    quint16 preferredDirectTcpPortStart() const;
    quint16 preferredDirectTcpPortEnd() const;
    bool usingFallbackDirectTcpPort() const;
    QList<ChatPeer> peers() const;

public slots:
    void start();
    void refreshConnection();
    void connectToAddress(const QString &address);
    void setLocalName(const QString &name);
    void setLocalStatus(const QString &status);
    void setLocalPersonalMessage(const QString &message);
    void setLocalThemeColor(const QString &color);
    void setLocalIdle(bool idle);
    void setPublicChatOpen(bool open);
    void setLocalAvatar(const QString &filePath);
    void sendMessage(const QString &peerId, const QString &message, bool isHtml = false);
    void sendFile(const QString &peerId, const QString &filePath);
    void sendPublicMessage(const QString &message, bool isHtml = false);
    void sendMessageReceipt(const QString &peerId, const QString &messageId, const QString &status);
    void sendTypingState(const QString &peerId, bool isTyping);
    void sendBuzz(const QString &peerId);
    void sendGameAction(const QString &peerId, const QString &game, const QString &action, const QString &gameId, const QString &value = QString(), const QString &board = QString());
    void cancelTransfer(const QString &fileId);
    void acceptFileTransfer(const QString &peerId, const QString &fileId);
    void rejectFileTransfer(const QString &peerId, const QString &fileId);

signals:
    void peerJoined(const ChatPeer &peer);
    void peerUpdated(const ChatPeer &peer);
    void peerLeft(const QString &peerId, bool appClosed);
    void messageReceived(const QString &peerId, const QString &peerName, const QString &message, const QString &messageId, bool isHtml);
    void messageSent(const QString &peerId, const QString &message, const QString &messageId, bool isHtml);
    void messageReceiptReceived(const QString &peerId, const QString &messageId, const QString &status);
    void typingStateReceived(const QString &peerId, bool isTyping);
    void fileReceived(const QString &peerId, const QString &peerName, const QString &fileName, const QByteArray &data, bool isImage);
    void fileSent(const QString &peerId, const QString &fileName, const QString &filePath, bool isImage);
    void publicMessageReceived(const QString &peerId, const QString &peerName, const QString &message, const QString &messageId, bool isHtml);
    void publicMessageSent(const QString &message, const QString &messageId, bool isHtml);
    void statusChanged(const QString &status);
    void manualConnectionSucceeded(const QString &address, const QString &peerName);
    void manualConnectionFailed(const QString &address, const QString &reason);
    void buzzReceived(const QString &peerId, const QString &peerName);
    void buzzSent(const QString &peerId);
    void gameActionReceived(const QString &peerId, const QString &peerName, const QString &game, const QString &action, const QString &gameId, const QString &value, const QString &board);
    void gameActionSent(const QString &peerId, const QString &game, const QString &action, const QString &gameId);
    void privateChatRejected(const QString &peerId, const QString &peerName, const QString &reason);
    void fileTooLarge(const QString &fileName, int maxSizeMb);
    void errorOccurred(const QString &message);
    void fileTransferOffered(const QString &peerId, const QString &peerName, const QString &fileId, const QString &fileName, qint64 fileSize);
    void fileTransferStarted(const QString &peerId, const QString &fileId, const QString &fileName, int totalChunks, bool isSending);
    void fileTransferProgress(const QString &peerId, const QString &fileId, int currentChunk);
    void fileTransferFinished(const QString &peerId, const QString &fileId, bool success, const QString &error);

private slots:
    void readPendingDatagrams();
    void acceptIncomingConnection();
    void sendPresence();
    void pruneStalePeers();

private:
    void handlePresence(const QJsonObject &message, const QHostAddress &sender);
    void handleChatSocket(QTcpSocket *socket);
    void sendJsonPayload(const ChatPeer &peer, const QJsonObject &payload, const QString &successPeerId, const QString &successText, bool isFile, const QString &messageId = QString(), bool isHtml = false, const QString &filePath = QString(), bool isImage = false);
    void sendJsonPayloadToHost(const QHostAddress &address, quint16 port, const QJsonObject &payload, std::function<void()> onConnected, std::function<void(const QString &)> onFailed);
    void sendSilentJsonPayload(const ChatPeer &peer, const QJsonObject &payload);
    bool allowsPrivateInteraction(const QString &peerId, const QHostAddress &senderAddress = QHostAddress()) const;
    QString privateInteractionRejectReason() const;
    void sendPrivateChatReject(const QString &peerId, const QString &reason);
    void sendPresenceMessage(const QString &type);
    void sendDirectPresenceUpdate(bool includeLargeProfile = false);
    void sendDirectPresenceBye();
    void loadSettings();
    void saveSettings() const;
    void ensurePrivateMessageKeys();
    QString peerDisplayName(const QString &peerId) const;
    QJsonObject messagePayloadForPeer(const QString &message, const QString &peerId = QString()) const;
    QString decryptedMessageText(const QJsonObject &payload, const QString &peerId = QString());
    QByteArray cryptMessageBytes(const QByteArray &data, const QString &peerId = QString()) const;
    QByteArray messageAuthCode(const QByteArray &cipherText, const QString &peerId) const;
    QJsonObject privateMessagePayloadForPeer(const QString &message, const ChatPeer &peer) const;
    QString decryptedPrivateMessageText(const QJsonObject &payload, const QString &peerId);

    static constexpr quint16 DiscoveryPort = 45454;
    static constexpr quint16 DirectTcpPortStart = 45455;
    static constexpr quint16 DirectTcpPortEnd = 45475;
    static constexpr int PresenceIntervalMs = 2000;
    static constexpr int PeerTimeoutMs = 45000;

    QSet<QString> m_pendingManualConnectionHosts;
    QString m_localId;
    QString m_localName;
    QString m_localStatus;
    QString m_localPersonalMessage;
    QString m_localThemeColor;
    bool m_localIdle = false;
    bool m_publicChatOpen = false;
    QByteArray m_localAvatarData;
    QByteArray m_privateKeyData;
    QByteArray m_publicKeyData;
    QString m_connectionInfo;
    bool m_discoveryAvailable = false;
    QString m_discoveryError;
    bool m_usingFallbackDirectTcpPort = false;
    QUdpSocket *m_udpSocket = nullptr;
    QTcpServer *m_tcpServer = nullptr;
    QTimer m_presenceTimer;
    QTimer m_pruneTimer;
    QHash<QString, ChatPeer> m_peers;
    QSet<QString> m_seenPublicMessageIds;
    QHash<QString, QSharedPointer<QAtomicInt>> m_activeOutTransfers;
};

#endif // LANCHATSERVICE_H
