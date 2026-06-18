#include "internetrelayservice.h"

#include <QAbstractSocket>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QTcpSocket>
#include <QUuid>

namespace {
constexpr qsizetype MaxServerBufferSize = 9 * 1024 * 1024;
constexpr qint64 MaxInternetImageBytes = 5 * 1024 * 1024;

QString normalizeBlinqId(QString value)
{
    value = value.trimmed().toLower();
    if (!value.contains(QLatin1Char('@'))) {
        value += QStringLiteral("@blinqm.net");
    }
    return value;
}

InternetRelayPeer peerFromObject(const QJsonObject &object)
{
    InternetRelayPeer peer;
    peer.id = object.value(QStringLiteral("id")).toString();
    peer.username = object.value(QStringLiteral("username")).toString();
    peer.blinqId = object.value(QStringLiteral("blinqId")).toString();
    peer.displayName = object.value(QStringLiteral("displayName")).toString();
    peer.status = object.value(QStringLiteral("status")).toString(QStringLiteral("Offline"));
    peer.personalMessage = object.value(QStringLiteral("personalMessage")).toString();
    peer.avatar = object.value(QStringLiteral("avatar")).toString();
    peer.themeColor = object.value(QStringLiteral("themeColor")).toString();
    return peer;
}

InternetContactRequest requestFromObject(const QJsonObject &object)
{
    InternetContactRequest request;
    request.id = object.value(QStringLiteral("id")).toString();
    request.fromUser = peerFromObject(object.value(QStringLiteral("fromUser")).toObject());
    return request;
}

InternetUserSearchResult searchResultFromObject(const QJsonObject &object)
{
    InternetUserSearchResult result;
    result.user = peerFromObject(object);
    return result;
}

QString imageMimeForFile(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("png")) return QStringLiteral("image/png");
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) return QStringLiteral("image/jpeg");
    if (suffix == QStringLiteral("gif")) return QStringLiteral("image/gif");
    if (suffix == QStringLiteral("webp")) return QStringLiteral("image/webp");
    if (suffix == QStringLiteral("bmp")) return QStringLiteral("image/bmp");

    QMimeDatabase database;
    const QString mime = database.mimeTypeForFile(info).name();
    return mime.startsWith(QStringLiteral("image/")) ? mime : QString();
}
}

InternetRelayService::InternetRelayService(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        const QList<QByteArray> pending = m_pendingWrites;
        m_pendingWrites.clear();
        for (const QByteArray &payload : pending) {
            m_socket->write(payload);
            m_socket->write("\n");
        }
    });

    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        m_buffer += m_socket->readAll();
        if (m_buffer.size() > MaxServerBufferSize) {
            emit connectionFailed(tr("Server sent too much data without a complete message."));
            disconnectFromServer();
            return;
        }

        int newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline).trimmed();
            m_buffer.remove(0, newline + 1);
            if (!line.isEmpty()) {
                processLine(line);
            }
        }
    });

    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        const bool wasAuthenticated = m_authenticated;
        resetSession();
        if (wasAuthenticated) {
            emit disconnectedFromServer();
        }
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        const QString message = m_socket->errorString();
        if (!m_authenticated) {
            emit serverUnavailable(message);
            emit connectionFailed(message);
        } else {
            emit errorOccurred(message);
        }
    });
}

InternetRelayService::~InternetRelayService() = default;

bool InternetRelayService::isAuthenticated() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState && m_authenticated;
}

QList<InternetRelayPeer> InternetRelayService::contacts() const
{
    return m_contacts.values();
}

QList<InternetContactRequest> InternetRelayService::contactRequests() const
{
    return m_contactRequests;
}

InternetRelayPeer InternetRelayService::self() const
{
    return m_self;
}

QString InternetRelayService::token() const
{
    return m_token;
}

QString InternetRelayService::localId() const
{
    return m_self.id;
}

QString InternetRelayService::blinqIdForPeer(const QString &peerId) const
{
    return m_contacts.value(peerId).blinqId;
}

void InternetRelayService::connectToServer(const QString &host, quint16 port)
{
    const QString trimmedHost = host.trimmed();
    if (trimmedHost.isEmpty()) {
        emit connectionFailed(tr("The Blinq server address is not configured."));
        return;
    }
    if (port == 0) {
        emit connectionFailed(tr("The Blinq server port is not valid."));
        return;
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    resetSession();
    m_host = trimmedHost;
    m_port = port;
    m_socket->connectToHost(m_host, m_port);
}

void InternetRelayService::disconnectFromServer()
{
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        resetSession();
        return;
    }
    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

void InternetRelayService::signUp(const QString &username, const QString &password, const QString &displayName)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("signup"));
    object.insert(QStringLiteral("username"), username.trimmed());
    object.insert(QStringLiteral("password"), password);
    object.insert(QStringLiteral("displayName"), displayName.trimmed());
    sendJson(object);
}

void InternetRelayService::login(const QString &username, const QString &password)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("login"));
    object.insert(QStringLiteral("username"), username.trimmed());
    object.insert(QStringLiteral("password"), password);
    sendJson(object);
}

void InternetRelayService::resume(const QString &token)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("resume"));
    object.insert(QStringLiteral("token"), token.trimmed());
    sendJson(object);
}

void InternetRelayService::setPresence(const QString &status, const QString &personalMessage)
{
    m_lastPresenceStatus = status;
    m_lastPersonalMessage = personalMessage;
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("setPresence"));
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("personalMessage"), personalMessage);
    if (!m_profileDisplayName.isEmpty()) {
        object.insert(QStringLiteral("displayName"), m_profileDisplayName);
    }
    if (!m_profileAvatarBase64.isEmpty()) {
        object.insert(QStringLiteral("avatar"), m_profileAvatarBase64);
    }
    if (!m_profileThemeColor.isEmpty()) {
        object.insert(QStringLiteral("themeColor"), m_profileThemeColor);
    }
    object.insert(QStringLiteral("searchable"), m_searchable);
    sendJson(object);
}

void InternetRelayService::setProfile(const QString &displayName, const QByteArray &avatarData, const QString &themeColor)
{
    if (!displayName.trimmed().isEmpty()) {
        m_profileDisplayName = displayName.trimmed();
    }
    if (!avatarData.isEmpty()) {
        m_profileAvatarBase64 = QString::fromLatin1(avatarData.toBase64());
    }
    if (!themeColor.trimmed().isEmpty()) {
        m_profileThemeColor = themeColor.trimmed();
    }
    setPresence(m_lastPresenceStatus, m_lastPersonalMessage);
}

void InternetRelayService::setSearchable(bool searchable)
{
    m_searchable = searchable;
    setPresence(m_lastPresenceStatus, m_lastPersonalMessage);
}

void InternetRelayService::searchUsers(const QString &query)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("searchUsers"));
    object.insert(QStringLiteral("query"), query.trimmed());
    sendJson(object);
}

void InternetRelayService::addContact(const QString &blinqId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("addContact"));
    object.insert(QStringLiteral("to"), normalizeBlinqId(blinqId));
    sendJson(object);
}

void InternetRelayService::acceptContact(const QString &requestId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("acceptContact"));
    object.insert(QStringLiteral("requestId"), requestId);
    sendJson(object);
}

void InternetRelayService::rejectContact(const QString &requestId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("rejectContact"));
    object.insert(QStringLiteral("requestId"), requestId);
    sendJson(object);
}

void InternetRelayService::sendMessage(const QString &peerId, const QString &message, bool isHtml)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QString target = targetForPeer(peerId);
    if (target.isEmpty()) {
        emit errorOccurred(tr("That internet contact is not available."));
        return;
    }

    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("message"));
    object.insert(QStringLiteral("to"), target);
    object.insert(QStringLiteral("body"), trimmed);
    object.insert(QStringLiteral("isHtml"), isHtml);
    object.insert(QStringLiteral("clientMessageId"), messageId);
    sendJson(object);
    emit messageSent(peerId, trimmed, messageId, isHtml);
}

void InternetRelayService::sendImage(const QString &peerId, const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        emit errorOccurred(tr("Could not find that image."));
        return;
    }
    const QString mime = imageMimeForFile(info);
    if (mime.isEmpty()) {
        emit errorOccurred(tr("Internet mode only supports image attachments."));
        return;
    }
    if (info.size() > MaxInternetImageBytes) {
        emit errorOccurred(tr("Images must be 5 MB or smaller in Internet Mode."));
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Could not open image: %1").arg(file.errorString()));
        return;
    }
    const QByteArray data = file.readAll();
    const QString target = targetForPeer(peerId);
    if (target.isEmpty()) {
        emit errorOccurred(tr("That internet contact is not available."));
        return;
    }

    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("imageMessage"));
    object.insert(QStringLiteral("to"), target);
    object.insert(QStringLiteral("fileName"), info.fileName());
    object.insert(QStringLiteral("mimeType"), mime);
    object.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));
    object.insert(QStringLiteral("clientMessageId"), messageId);
    sendJson(object);
    emit imageSent(peerId, info.fileName(), filePath);
}

void InternetRelayService::sendTypingState(const QString &peerId, bool isTyping)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("typing"));
    object.insert(QStringLiteral("to"), targetForPeer(peerId));
    object.insert(QStringLiteral("isTyping"), isTyping);
    sendJson(object);
}

void InternetRelayService::sendReceipt(const QString &peerId, const QString &messageId, const QString &status)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("receipt"));
    object.insert(QStringLiteral("to"), targetForPeer(peerId));
    object.insert(QStringLiteral("messageId"), messageId);
    object.insert(QStringLiteral("status"), status);
    sendJson(object);
}

void InternetRelayService::sendBuzz(const QString &peerId)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("buzz"));
    object.insert(QStringLiteral("to"), targetForPeer(peerId));
    sendJson(object);
    emit buzzSent(peerId);
}

void InternetRelayService::changePassword(const QString &currentPassword, const QString &newPassword)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("changePassword"));
    object.insert(QStringLiteral("currentPassword"), currentPassword);
    object.insert(QStringLiteral("newPassword"), newPassword);
    sendJson(object);
}

void InternetRelayService::deleteAccount(const QString &password)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("deleteAccount"));
    object.insert(QStringLiteral("password"), password);
    sendJson(object);
}

void InternetRelayService::sendJson(const QJsonObject &object)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        if (m_socket->state() == QAbstractSocket::ConnectingState || m_socket->state() == QAbstractSocket::HostLookupState) {
            m_pendingWrites.append(QJsonDocument(object).toJson(QJsonDocument::Compact));
        } else {
            emit connectionFailed(tr("Not connected to the Blinq server."));
        }
        return;
    }
    m_socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    m_socket->write("\n");
}

void InternetRelayService::processLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit errorOccurred(tr("Server sent an invalid message."));
        return;
    }

    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("authenticated")) {
        m_authenticated = true;
        m_token = object.value(QStringLiteral("token")).toString(m_token);
        m_self = peerFromObject(object.value(QStringLiteral("user")).toObject());
        m_profileDisplayName = m_self.displayName;
        m_profileAvatarBase64 = m_self.avatar;
        m_profileThemeColor = m_self.themeColor;
        m_searchable = object.value(QStringLiteral("user")).toObject().value(QStringLiteral("searchable")).toBool(m_searchable);
        m_contacts.clear();
        const QJsonArray contacts = object.value(QStringLiteral("contacts")).toArray();
        for (const QJsonValue &value : contacts) {
            upsertContact(peerFromObject(value.toObject()));
        }
        m_contactRequests.clear();
        const QJsonArray requests = object.value(QStringLiteral("contactRequests")).toArray();
        for (const QJsonValue &value : requests) {
            m_contactRequests.append(requestFromObject(value.toObject()));
        }
        emit authenticated(m_token, m_self);
        emit contactsChanged();
        return;
    }

    if (type == QStringLiteral("error")) {
        const QString message = object.value(QStringLiteral("message")).toString(tr("Blinq server error."));
        if (!m_authenticated) {
            emit connectionFailed(message);
        } else {
            emit errorOccurred(message);
        }
        return;
    }

    if (type == QStringLiteral("ping")) {
        QJsonObject response;
        response.insert(QStringLiteral("type"), QStringLiteral("pong"));
        sendJson(response);
        return;
    }

    if (!m_authenticated) {
        return;
    }

    if (type == QStringLiteral("accountDeleted")) {
        emit accountDeleted();
        resetSession();
        return;
    }

    if (type == QStringLiteral("passwordChanged")) {
        emit passwordChanged();
        return;
    }

    if (type == QStringLiteral("contacts")) {
        m_contacts.clear();
        const QJsonArray contacts = object.value(QStringLiteral("contacts")).toArray();
        for (const QJsonValue &value : contacts) {
            upsertContact(peerFromObject(value.toObject()));
        }
        m_contactRequests.clear();
        const QJsonArray requests = object.value(QStringLiteral("contactRequests")).toArray();
        for (const QJsonValue &value : requests) {
            m_contactRequests.append(requestFromObject(value.toObject()));
        }
        emit contactsChanged();
        return;
    }

    if (type == QStringLiteral("contactRequest")) {
        const InternetContactRequest request = requestFromObject(object.value(QStringLiteral("request")).toObject());
        if (!request.id.isEmpty()) {
            m_contactRequests.append(request);
            emit contactRequestReceived(request);
            emit contactsChanged();
        }
        return;
    }

    if (type == QStringLiteral("userSearchResults")) {
        QList<InternetUserSearchResult> results;
        const QJsonArray users = object.value(QStringLiteral("users")).toArray();
        for (const QJsonValue &value : users) {
            const InternetUserSearchResult result = searchResultFromObject(value.toObject());
            if (!result.user.id.isEmpty()) {
                results.append(result);
            }
        }
        emit userSearchResultsReceived(object.value(QStringLiteral("query")).toString(), results);
        return;
    }

    if (type == QStringLiteral("presence")) {
        const InternetRelayPeer peer = peerFromObject(object.value(QStringLiteral("user")).toObject());
        if (!peer.id.isEmpty()) {
            upsertContact(peer);
            emit presenceReceived(peer);
            emit contactsChanged();
        }
        return;
    }

    if (type == QStringLiteral("presenceSet")) {
        const InternetRelayPeer peer = peerFromObject(object.value(QStringLiteral("user")).toObject());
        if (!peer.id.isEmpty()) {
            m_self = peer;
            m_profileDisplayName = peer.displayName;
            m_profileAvatarBase64 = peer.avatar;
            m_profileThemeColor = peer.themeColor;
            m_searchable = object.value(QStringLiteral("user")).toObject().value(QStringLiteral("searchable")).toBool(m_searchable);
            emit presenceReceived(peer);
        }
        return;
    }

    if (type == QStringLiteral("message")) {
        const QJsonObject message = object.value(QStringLiteral("message")).toObject();
        const InternetRelayPeer from = peerFromObject(object.value(QStringLiteral("fromUser")).toObject());
        const QString peerId = from.id.isEmpty() ? message.value(QStringLiteral("from")).toString() : from.id;
        if (!from.id.isEmpty()) {
            upsertContact(from);
        }
        emit messageReceived(peerId,
                             from.displayName.isEmpty() ? from.blinqId : from.displayName,
                             message.value(QStringLiteral("body")).toString(),
                             message.value(QStringLiteral("id")).toString(),
                             message.value(QStringLiteral("isHtml")).toBool(false));
        return;
    }

    if (type == QStringLiteral("imageMessage")) {
        const QJsonObject message = object.value(QStringLiteral("message")).toObject();
        const InternetRelayPeer from = peerFromObject(object.value(QStringLiteral("fromUser")).toObject());
        const QString peerId = from.id.isEmpty() ? message.value(QStringLiteral("from")).toString() : from.id;
        if (!from.id.isEmpty()) {
            upsertContact(from);
        }
        const QByteArray data = QByteArray::fromBase64(message.value(QStringLiteral("data")).toString().toLatin1());
        emit imageReceived(peerId,
                           from.displayName.isEmpty() ? from.blinqId : from.displayName,
                           message.value(QStringLiteral("fileName")).toString(QStringLiteral("image")),
                           data);
        return;
    }

    const InternetRelayPeer from = peerFromObject(object.value(QStringLiteral("fromUser")).toObject());
    const QString peerId = from.id;
    if (type == QStringLiteral("typing")) {
        emit typingStateReceived(peerId, object.value(QStringLiteral("isTyping")).toBool(false));
    } else if (type == QStringLiteral("receipt")) {
        emit receiptReceived(peerId,
                             object.value(QStringLiteral("messageId")).toString(),
                             object.value(QStringLiteral("status")).toString());
    } else if (type == QStringLiteral("buzz")) {
        emit buzzReceived(peerId, from.displayName.isEmpty() ? from.blinqId : from.displayName);
    }
}

void InternetRelayService::resetSession()
{
    m_buffer.clear();
    m_pendingWrites.clear();
    m_authenticated = false;
    m_self = {};
    m_contacts.clear();
    m_contactRequests.clear();
    emit contactsChanged();
}

void InternetRelayService::upsertContact(const InternetRelayPeer &peer)
{
    if (!peer.id.isEmpty()) {
        m_contacts.insert(peer.id, peer);
    }
}

QString InternetRelayService::targetForPeer(const QString &peerId) const
{
    if (peerId.contains(QLatin1Char('@'))) {
        return normalizeBlinqId(peerId);
    }
    return normalizeBlinqId(m_contacts.value(peerId).blinqId);
}
