#ifndef INTERNETRELAYSERVICE_H
#define INTERNETRELAYSERVICE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QJsonObject;
class QTcpSocket;

struct InternetRelayPeer
{
    QString id;
    QString username;
    QString blinqId;
    QString displayName;
    QString status;
    QString personalMessage;
    QString avatar;
    QString themeColor;
    QString lastSeenAt;
};

struct InternetContactRequest
{
    QString id;
    InternetRelayPeer fromUser;
};

struct InternetUserSearchResult
{
    InternetRelayPeer user;
};

class InternetRelayService : public QObject
{
    Q_OBJECT

public:
    explicit InternetRelayService(QObject *parent = nullptr);
    ~InternetRelayService() override;

    bool isAuthenticated() const;
    QList<InternetRelayPeer> contacts() const;
    QList<InternetContactRequest> contactRequests() const;
    InternetRelayPeer self() const;
    QString token() const;
    QString localId() const;
    QString blinqIdForPeer(const QString &peerId) const;

public slots:
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void signUp(const QString &username, const QString &password, const QString &displayName, const QString &email);
    void login(const QString &username, const QString &password);
    void resume(const QString &token);
    void logout();
    void setPresence(const QString &status, const QString &personalMessage);
    void setProfile(const QString &displayName, const QByteArray &avatarData, const QString &themeColor = QString());
    void setSearchable(bool searchable);
    void searchUsers(const QString &query);
    void addContact(const QString &blinqId);
    void removeContact(const QString &peerId);
    void acceptContact(const QString &requestId);
    void rejectContact(const QString &requestId);
    void sendMessage(const QString &peerId, const QString &message, bool isHtml = false);
    void sendImage(const QString &peerId, const QString &filePath);
    void sendTypingState(const QString &peerId, bool isTyping);
    void sendReceipt(const QString &peerId, const QString &messageId, const QString &status);
    void sendBuzz(const QString &peerId);
    void changePassword(const QString &currentPassword, const QString &newPassword);
    void setRecoveryEmail(const QString &email, const QString &password);
    void requestPasswordReset(const QString &identifier);
    void resetPassword(const QString &identifier, const QString &code, const QString &newPassword);
    void sendFeedback(const QString &category, const QString &message, const QString &contactEmail, const QString &platform, const QString &debugInfo);
    void deleteAccount(const QString &password);

signals:
    void authenticated(const QString &token, const InternetRelayPeer &self);
    void disconnectedFromServer();
    void connectionFailed(const QString &reason);
    void serverUnavailable(const QString &reason);
    void errorOccurred(const QString &message);
    void contactsChanged();
    void contactRequestReceived(const InternetContactRequest &request);
    void messageReceived(const QString &peerId, const QString &peerName, const QString &message, const QString &messageId, bool isHtml);
    void imageReceived(const QString &peerId, const QString &peerName, const QString &fileName, const QByteArray &data);
    void messageSent(const QString &peerId, const QString &message, const QString &messageId, bool isHtml);
    void imageSent(const QString &peerId, const QString &fileName, const QString &filePath);
    void typingStateReceived(const QString &peerId, bool isTyping);
    void receiptReceived(const QString &peerId, const QString &messageId, const QString &status);
    void buzzReceived(const QString &peerId, const QString &peerName);
    void buzzSent(const QString &peerId);
    void accountDeleted();
    void signedOut();
    void passwordChanged();
    void recoveryEmailSet();
    void passwordResetRequested();
    void passwordReset();
    void feedbackSent();
    void presenceReceived(const InternetRelayPeer &peer);
    void userSearchResultsReceived(const QString &query, const QList<InternetUserSearchResult> &results);

private:
    void sendJson(const QJsonObject &object);
    void processLine(const QByteArray &line);
    void resetSession();
    void upsertContact(const InternetRelayPeer &peer);
    QString targetForPeer(const QString &peerId) const;

    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
    QList<QByteArray> m_pendingWrites;
    QString m_host;
    quint16 m_port = 0;
    QString m_token;
    QString m_lastPresenceStatus = QStringLiteral("Available");
    QString m_lastPersonalMessage;
    QString m_profileDisplayName;
    QString m_profileAvatarBase64;
    QString m_profileThemeColor;
    bool m_searchable = true;
    InternetRelayPeer m_self;
    bool m_authenticated = false;
    QHash<QString, InternetRelayPeer> m_contacts;
    QList<InternetContactRequest> m_contactRequests;
};

#endif // INTERNETRELAYSERVICE_H
