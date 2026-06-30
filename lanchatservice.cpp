#include "lanchatservice.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
constexpr qint64 MaxTransferBytes = 250 * 1024 * 1024;
constexpr int SendTimeoutMs = 6000;
constexpr int ProtocolVersion = 3;
const QByteArray MessageEncryptionKey = QByteArrayLiteral("BlinqMessenger-v1-local-message-key");

struct IncomingTransfer {
    QString senderId;
    QString senderName;
    QString fileName;
    QString tempPath;
    bool isImage = false;
    int expectedChunks = 0;
    int chunksReceived = 0;
    QDateTime lastUpdated;
};
QHash<QString, IncomingTransfer> g_incomingTransfers;

#ifdef Q_OS_WIN
struct PrivateMessageCipher
{
    QByteArray nonce;
    QByteArray cipherText;
    QByteArray tag;
};

bool isNtSuccess(NTSTATUS status)
{
    return status >= 0;
}

void closeAlg(BCRYPT_ALG_HANDLE handle)
{
    if (handle) {
        BCryptCloseAlgorithmProvider(handle, 0);
    }
}

void destroyKey(BCRYPT_KEY_HANDLE handle)
{
    if (handle) {
        BCryptDestroyKey(handle);
    }
}

void destroySecret(BCRYPT_SECRET_HANDLE handle)
{
    if (handle) {
        BCryptDestroySecret(handle);
    }
}

bool generateEcdhP256KeyPair(QByteArray *privateBlob, QByteArray *publicBlob)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;

    if (!isNtSuccess(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0))) {
        return false;
    }
    if (!isNtSuccess(BCryptGenerateKeyPair(alg, &key, 256, 0))
        || !isNtSuccess(BCryptFinalizeKeyPair(key, 0))) {
        closeAlg(alg);
        return false;
    }

    ULONG privateSize = 0;
    ULONG publicSize = 0;
    if (isNtSuccess(BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &privateSize, 0))
        && isNtSuccess(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &publicSize, 0))) {
        privateBlob->resize(static_cast<int>(privateSize));
        publicBlob->resize(static_cast<int>(publicSize));
        ok = isNtSuccess(BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, reinterpret_cast<PUCHAR>(privateBlob->data()), privateSize, &privateSize, 0))
             && isNtSuccess(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(publicBlob->data()), publicSize, &publicSize, 0));
    }

    destroyKey(key);
    closeAlg(alg);
    return ok;
}

QByteArray sharedSecretForPeer(const QByteArray &privateBlob, const QByteArray &peerPublicBlob)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE localKey = nullptr;
    BCRYPT_KEY_HANDLE peerKey = nullptr;
    BCRYPT_SECRET_HANDLE secret = nullptr;
    QByteArray result;

    if (!isNtSuccess(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0))) {
        return result;
    }
    if (!isNtSuccess(BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &localKey, reinterpret_cast<PUCHAR>(const_cast<char *>(privateBlob.constData())), static_cast<ULONG>(privateBlob.size()), 0))
        || !isNtSuccess(BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &peerKey, reinterpret_cast<PUCHAR>(const_cast<char *>(peerPublicBlob.constData())), static_cast<ULONG>(peerPublicBlob.size()), 0))
        || !isNtSuccess(BCryptSecretAgreement(localKey, peerKey, &secret, 0))) {
        destroySecret(secret);
        destroyKey(peerKey);
        destroyKey(localKey);
        closeAlg(alg);
        return result;
    }

    ULONG rawSize = 0;
    if (isNtSuccess(BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &rawSize, 0))) {
        QByteArray raw;
        raw.resize(static_cast<int>(rawSize));
        if (isNtSuccess(BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, reinterpret_cast<PUCHAR>(raw.data()), rawSize, &rawSize, 0))) {
            result = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
        }
    }

    destroySecret(secret);
    destroyKey(peerKey);
    destroyKey(localKey);
    closeAlg(alg);
    return result;
}

bool aesGcmCrypt(const QByteArray &keyBytes, const QByteArray &nonce, const QByteArray &input, QByteArray *output, QByteArray *tag, bool encrypt)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;

    if (!isNtSuccess(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return false;
    }
    if (!isNtSuccess(BCryptSetProperty(alg,
                                       BCRYPT_CHAINING_MODE,
                                       reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                       static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
                                       0))) {
        closeAlg(alg);
        return false;
    }
    if (!isNtSuccess(BCryptGenerateSymmetricKey(alg,
                                               &key,
                                               nullptr,
                                               0,
                                               reinterpret_cast<PUCHAR>(const_cast<char *>(keyBytes.constData())),
                                               static_cast<ULONG>(keyBytes.size()),
                                               0))) {
        closeAlg(alg);
        return false;
    }

    output->resize(input.size());
    if (encrypt) {
        tag->resize(16);
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbTag = reinterpret_cast<PUCHAR>(tag->data());
    authInfo.cbTag = static_cast<ULONG>(tag->size());

    ULONG bytesDone = 0;
    const NTSTATUS status = encrypt
                                ? BCryptEncrypt(key,
                                                reinterpret_cast<PUCHAR>(const_cast<char *>(input.constData())),
                                                static_cast<ULONG>(input.size()),
                                                &authInfo,
                                                nullptr,
                                                0,
                                                reinterpret_cast<PUCHAR>(output->data()),
                                                static_cast<ULONG>(output->size()),
                                                &bytesDone,
                                                0)
                                : BCryptDecrypt(key,
                                                reinterpret_cast<PUCHAR>(const_cast<char *>(input.constData())),
                                                static_cast<ULONG>(input.size()),
                                                &authInfo,
                                                nullptr,
                                                0,
                                                reinterpret_cast<PUCHAR>(output->data()),
                                                static_cast<ULONG>(output->size()),
                                                &bytesDone,
                                                0);
    ok = isNtSuccess(status);
    if (ok) {
        output->resize(static_cast<int>(bytesDone));
    }

    destroyKey(key);
    closeAlg(alg);
    return ok;
}

PrivateMessageCipher encryptPrivateMessage(const QByteArray &keyBytes, const QByteArray &plainText)
{
    PrivateMessageCipher result;
    result.nonce.resize(12);
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(result.nonce.data()), static_cast<ULONG>(result.nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    aesGcmCrypt(keyBytes, result.nonce, plainText, &result.cipherText, &result.tag, true);
    return result;
}

QByteArray decryptPrivateMessage(const QByteArray &keyBytes, const QByteArray &nonce, const QByteArray &cipherText, const QByteArray &tag)
{
    QByteArray plainText;
    QByteArray mutableTag = tag;
    if (!aesGcmCrypt(keyBytes, nonce, cipherText, &plainText, &mutableTag, false)) {
        return QByteArray();
    }
    return plainText;
}
#endif

bool isDisplayableImageFile(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("png")
        || suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("gif")
        || suffix == QStringLiteral("bmp")
        || suffix == QStringLiteral("webp");
}

QString endpointLabel(const QHostAddress &address, quint16 port)
{
    return QStringLiteral("%1:%2").arg(address.toString()).arg(port);
}
}

LanChatService::LanChatService(QObject *parent)
    : QObject(parent)
    , m_localId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_localName(qEnvironmentVariable("USERNAME"))
    , m_udpSocket(new QUdpSocket(this))
    , m_tcpServer(new QTcpServer(this))
{
    loadSettings();

    connect(m_udpSocket, &QUdpSocket::readyRead, this, &LanChatService::readPendingDatagrams);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &LanChatService::acceptIncomingConnection);
    connect(&m_presenceTimer, &QTimer::timeout, this, &LanChatService::sendPresence);
    connect(&m_pruneTimer, &QTimer::timeout, this, &LanChatService::pruneStalePeers);
}

LanChatService::~LanChatService()
{
    sendPresenceMessage(QStringLiteral("bye"));
}

QString LanChatService::localName() const
{
    return m_localName;
}

QString LanChatService::localStatus() const
{
    return m_localStatus;
}

QString LanChatService::localPersonalMessage() const
{
    return m_localPersonalMessage;
}

QByteArray LanChatService::localAvatarData() const
{
    return m_localAvatarData;
}

QString LanChatService::connectionInfo() const
{
    QStringList localAddresses;
    const auto addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            localAddresses.append(address.toString());
        }
    }

    return tr("Listening as %1 on TCP port %2 from %3")
        .arg(m_localName,
             QString::number(m_tcpServer->serverPort()),
             localAddresses.isEmpty() ? tr("this PC") : localAddresses.join(QStringLiteral(", ")));
}

quint16 LanChatService::discoveryPort() const
{
    return DiscoveryPort;
}

bool LanChatService::discoveryAvailable() const
{
    return m_discoveryAvailable;
}

QString LanChatService::discoveryError() const
{
    return m_discoveryError;
}

quint16 LanChatService::directTcpPort() const
{
    return m_tcpServer && m_tcpServer->isListening() ? m_tcpServer->serverPort() : 0;
}

quint16 LanChatService::preferredDirectTcpPortStart() const
{
    return DirectTcpPortStart;
}

quint16 LanChatService::preferredDirectTcpPortEnd() const
{
    return DirectTcpPortEnd;
}

bool LanChatService::usingFallbackDirectTcpPort() const
{
    return m_usingFallbackDirectTcpPort;
}

QList<ChatPeer> LanChatService::peers() const
{
    return m_peers.values();
}

void LanChatService::start()
{
    m_discoveryError.clear();
    m_discoveryAvailable = m_udpSocket->bind(QHostAddress::AnyIPv4,
                                             DiscoveryPort,
                                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!m_discoveryAvailable) {
        m_discoveryError = m_udpSocket->errorString();
        emit errorOccurred(tr("Could not bind UDP discovery on port %1: %2")
                               .arg(DiscoveryPort)
                               .arg(m_discoveryError));
    }

    bool tcpBound = false;
    m_usingFallbackDirectTcpPort = false;
    for (quint16 port = DirectTcpPortStart; port <= DirectTcpPortEnd; ++port) {
        if (m_tcpServer->listen(QHostAddress::AnyIPv4, port)) {
            tcpBound = true;
            break;
        }
    }
    if (!tcpBound) {
        m_usingFallbackDirectTcpPort = true;
        if (!m_tcpServer->listen(QHostAddress::AnyIPv4)) {
            emit errorOccurred(tr("Could not start TCP listener: %1").arg(m_tcpServer->errorString()));
            return;
        }
    }

    QStringList localAddresses;
    const auto addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            localAddresses.append(address.toString());
        }
    }

    m_connectionInfo = tr("Listening as %1 on TCP port %2 from %3")
                           .arg(m_localName,
                                QString::number(m_tcpServer->serverPort()),
                                localAddresses.isEmpty() ? tr("this PC") : localAddresses.join(QStringLiteral(", ")));
    emit statusChanged(m_connectionInfo);

    m_presenceTimer.start(PresenceIntervalMs);
    m_pruneTimer.start(PresenceIntervalMs);
    sendPresence();
}

void LanChatService::refreshConnection()
{
    m_presenceTimer.stop();
    m_pruneTimer.stop();
    sendPresenceMessage(QStringLiteral("bye"));

    if (m_udpSocket) {
        m_udpSocket->close();
    }
    if (m_tcpServer) {
        m_tcpServer->close();
    }

    const QStringList peerIds = m_peers.keys();
    m_peers.clear();
    for (const QString &peerId : peerIds) {
        emit peerLeft(peerId, false);
    }

    m_discoveryAvailable = false;
    m_discoveryError.clear();
    m_usingFallbackDirectTcpPort = false;
    m_pendingManualConnectionHosts.clear();
    start();
}

void LanChatService::connectToAddress(const QString &address)
{
    QString input = address.trimmed();
    static const QRegularExpression endpointPattern(
        QStringLiteral(R"(\b((?:25[0-5]|2[0-4]\d|1?\d?\d)(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){3})(?::([1-9]\d{0,4}))?\b)"));
    const QRegularExpressionMatch endpointMatch = endpointPattern.match(input);
    if (endpointMatch.hasMatch()) {
        input = endpointMatch.captured(0);
    }
    QString hostText = input;
    quint16 requestedPort = 0;
    const int portSeparator = input.lastIndexOf(QLatin1Char(':'));
    if (portSeparator > 0 && input.indexOf(QLatin1Char(':')) == portSeparator) {
        bool ok = false;
        const int parsedPort = input.mid(portSeparator + 1).toInt(&ok);
        if (ok && parsedPort > 0 && parsedPort <= 65535) {
            hostText = input.left(portSeparator).trimmed();
            requestedPort = static_cast<quint16>(parsedPort);
        }
    }

    const QHostAddress host(hostText);
    if (host.isNull() || host.protocol() != QAbstractSocket::IPv4Protocol) {
        emit manualConnectionFailed(address, tr("Enter a valid IPv4 address."));
        return;
    }

    for (const ChatPeer &peer : std::as_const(m_peers)) {
        if (peer.address == host && (requestedPort == 0 || peer.port == requestedPort)) {
            emit manualConnectionSucceeded(endpointLabel(peer.address, peer.port),
                                           peer.name.isEmpty() ? tr("Blinq Messenger user") : peer.name);
            return;
        }
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("manualHello"));
    payload.insert(QStringLiteral("id"), m_localId);
    payload.insert(QStringLiteral("name"), m_localName);
    payload.insert(QStringLiteral("status"), effectiveLocalStatus());
    payload.insert(QStringLiteral("personalMessage"), m_localPersonalMessage);
    payload.insert(QStringLiteral("themeColor"), m_localThemeColor);
    payload.insert(QStringLiteral("avatar"), QString::fromLatin1(m_localAvatarData.toBase64()));
    payload.insert(QStringLiteral("publicKey"), QString::fromLatin1(m_publicKeyData.toBase64()));
    payload.insert(QStringLiteral("port"), static_cast<int>(m_tcpServer->serverPort()));
    payload.insert(QStringLiteral("publicChatOpen"), m_publicChatOpen);
    payload.insert(QStringLiteral("protocolVersion"), ProtocolVersion);

    const QString hostKey = host.toString();
    m_pendingManualConnectionHosts.insert(hostKey);

    auto ports = QSharedPointer<QList<quint16>>::create();
    if (requestedPort > 0) {
        ports->append(requestedPort);
    } else {
        for (quint16 port = DirectTcpPortStart; port <= DirectTcpPortEnd; ++port) {
            ports->append(port);
        }
    }

    auto remaining = QSharedPointer<int>::create(ports->size());
    auto delivered = QSharedPointer<bool>::create(false);
    auto finished = QSharedPointer<bool>::create(false);
    auto lastError = QSharedPointer<QString>::create();
    auto failAttempt = QSharedPointer<std::function<void(const QString &)>>::create();
    *failAttempt = [this, input, hostKey, finished](const QString &reason) {
        if (*finished || !m_pendingManualConnectionHosts.contains(hostKey)) {
            return;
        }
        *finished = true;
        m_pendingManualConnectionHosts.remove(hostKey);
        emit manualConnectionFailed(input, reason);
    };

    for (const quint16 port : std::as_const(*ports)) {
        sendJsonPayloadToHost(host,
                              port,
                              payload,
                              [delivered] {
                                  *delivered = true;
                              },
                              [lastError, remaining, failAttempt, port](const QString &reason) {
                                  *lastError = QObject::tr("Port %1: %2").arg(port).arg(reason);
                                  --(*remaining);
                                  if (*remaining <= 0) {
                                      (*failAttempt)(lastError->isEmpty()
                                                         ? QObject::tr("No Blinq Messenger listener answered in the direct-connect port range.")
                                                         : *lastError);
                                  }
                              });
    }

    QTimer::singleShot(SendTimeoutMs + 2500, this, [delivered, failAttempt] {
        (*failAttempt)(*delivered
                           ? QObject::tr("The other computer answered, but did not confirm the connection. Check its firewall and Blinq Messenger version.")
                           : QObject::tr("No Blinq Messenger listener answered in the direct-connect port range."));
    });
}

void LanChatService::setLocalStatus(const QString &status)
{
    if (status.trimmed().isEmpty() || status == m_localStatus) {
        return;
    }

    const bool wasInvisible = m_localStatus == tr("Invisible");
    m_localStatus = status;
    saveSettings();
    if (m_localStatus == tr("Invisible")) {
        sendDirectPresenceBye();
        sendPresenceMessage(QStringLiteral("bye"));
    } else if (wasInvisible) {
        sendPresenceMessage(QStringLiteral("hello"));
        sendDirectPresenceUpdate(true);
    } else {
        sendPresence();
        sendDirectPresenceUpdate();
    }
}

void LanChatService::setLocalPersonalMessage(const QString &message)
{
    if (message == m_localPersonalMessage) {
        return;
    }

    m_localPersonalMessage = message;
    saveSettings();
    sendPresence();
    sendDirectPresenceUpdate();
}

void LanChatService::setLocalThemeColor(const QString &color)
{
    if (color == m_localThemeColor) {
        return;
    }

    m_localThemeColor = color;
    sendPresence();
    sendDirectPresenceUpdate();
}

void LanChatService::setLocalIdle(bool idle)
{
    if (m_localIdle == idle) {
        return;
    }

    m_localIdle = idle;
    sendPresence();
    sendDirectPresenceUpdate();
}

void LanChatService::setPublicChatOpen(bool open)
{
    if (m_publicChatOpen == open) {
        return;
    }

    m_publicChatOpen = open;
    sendPresence();
    sendDirectPresenceUpdate();
}

void LanChatService::setLocalName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_localName) {
        return;
    }

    m_localName = trimmed;
    saveSettings();
    sendPresence();
    sendDirectPresenceUpdate();
}

void LanChatService::setLocalAvatar(const QString &filePath)
{
    QImage image(filePath);
    if (image.isNull()) {
        emit errorOccurred(tr("Could not load that avatar image."));
        return;
    }

    const int squareSize = qMin(image.width(), image.height());
    const QRect cropRect((image.width() - squareSize) / 2, (image.height() - squareSize) / 2, squareSize, squareSize);
    image = image.copy(cropRect).scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    m_localAvatarData = bytes;
    saveSettings();
    sendPresence();
    sendDirectPresenceUpdate(true);
}

void LanChatService::sendMessage(const QString &peerId, const QString &message, bool isHtml)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        emit errorOccurred(tr("That peer is no longer online."));
        return;
    }

    QJsonObject payload;
    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    payload.insert(QStringLiteral("type"), QStringLiteral("chat"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fromName"), m_localName);
    payload.insert(QStringLiteral("presenceStatus"), effectiveLocalStatus());
    payload.insert(QStringLiteral("messageId"), messageId);
    const QJsonObject messageFields = peerIt->protocolVersion >= ProtocolVersion && !peerIt->publicKeyData.isEmpty() && !m_privateKeyData.isEmpty()
                                          ? privateMessagePayloadForPeer(trimmed, *peerIt)
                                          : QJsonObject{{QStringLiteral("message"), trimmed}};
    for (auto it = messageFields.constBegin(); it != messageFields.constEnd(); ++it) {
        payload.insert(it.key(), it.value());
    }
    payload.insert(QStringLiteral("isHtml"), isHtml);

    sendJsonPayload(*peerIt, payload, peerId, trimmed, false, messageId, isHtml);
}

void LanChatService::sendFile(const QString &peerId, const QString &filePath)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        emit errorOccurred(tr("That peer is no longer online."));
        return;
    }

    const ChatPeer peer = *peerIt;
    const QString localId = m_localId;
    const QString localName = m_localName;
    auto *worker = QThread::create([this, peer, peerId, filePath, localId, localName] {
        QFile file(filePath);
        const QFileInfo info(file);
        if (info.size() > MaxTransferBytes) {
            QMetaObject::invokeMethod(this, [this, fileName = info.fileName()] {
                emit fileTooLarge(fileName, static_cast<int>(MaxTransferBytes / (1024 * 1024)));
            }, Qt::QueuedConnection);
            return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            const QString error = file.errorString();
            QMetaObject::invokeMethod(this, [this, error] {
                emit errorOccurred(tr("Could not open file: %1").arg(error));
            }, Qt::QueuedConnection);
            return;
        }

        const bool isImage = isDisplayableImageFile(info);
            const QString fileName = info.fileName();

            if (peer.protocolVersion < 3) {
                if (info.size() > 10 * 1024 * 1024) {
                    QMetaObject::invokeMethod(this, [this, fileName] {
                        emit fileTooLarge(fileName, 10);
                    }, Qt::QueuedConnection);
                    return;
                }
                const QByteArray data = file.readAll();
                QJsonObject payload;
                payload.insert(QStringLiteral("type"), QStringLiteral("file"));
                payload.insert(QStringLiteral("fromId"), localId);
                payload.insert(QStringLiteral("fromName"), localName);
                payload.insert(QStringLiteral("presenceStatus"), effectiveLocalStatus());
                payload.insert(QStringLiteral("fileName"), fileName);
                payload.insert(QStringLiteral("isImage"), isImage);
                payload.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));

                QMetaObject::invokeMethod(this, [this, peer, peerId, payload, fileName, filePath, isImage] {
                    sendJsonPayload(peer, payload, peerId, fileName, true, QString(), false, filePath, isImage);
                }, Qt::QueuedConnection);
                return;
            }

            const QString fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const int chunkSize = 512 * 1024; // 512 KB per chunk
            int totalChunks = static_cast<int>((info.size() + chunkSize - 1) / chunkSize);
            if (totalChunks == 0) totalChunks = 1; // Support for sending 0-byte empty files

        auto transferState = QSharedPointer<QAtomicInt>::create(0);
        m_activeOutTransfers.insert(fileId, transferState);

        QMetaObject::invokeMethod(this, [this, peerId, fileId, fileName, totalChunks] {
            emit fileTransferStarted(peerId, fileId, fileName, totalChunks, true);
        }, Qt::QueuedConnection);

        if (isImage) {
            transferState->storeRelaxed(1);
        } else {
            QJsonObject offer;
            offer.insert(QStringLiteral("type"), QStringLiteral("fileOffer"));
            offer.insert(QStringLiteral("fromId"), localId);
            offer.insert(QStringLiteral("fromName"), localName);
            offer.insert(QStringLiteral("presenceStatus"), effectiveLocalStatus());
            offer.insert(QStringLiteral("fileId"), fileId);
            offer.insert(QStringLiteral("fileName"), fileName);
            offer.insert(QStringLiteral("fileSize"), info.size());
            offer.insert(QStringLiteral("isImage"), false);

            QMetaObject::invokeMethod(this, [this, peer, offer] {
                sendSilentJsonPayload(peer, offer);
            }, Qt::QueuedConnection);

            int waitMs = 0;
            while (transferState->loadRelaxed() == 0 && waitMs < 60000) {
                QThread::msleep(100);
                waitMs += 100;
            }

            if (transferState->loadRelaxed() != 1) {
                bool wasCanceled = (transferState->loadRelaxed() == 2);
                if (wasCanceled) {
                    QJsonObject cancelPayload;
                    cancelPayload.insert(QStringLiteral("type"), QStringLiteral("cancelTransfer"));
                    cancelPayload.insert(QStringLiteral("fromId"), localId);
                    cancelPayload.insert(QStringLiteral("fileId"), fileId);
                    QMetaObject::invokeMethod(this, [this, peer, cancelPayload] {
                        sendSilentJsonPayload(peer, cancelPayload);
                    }, Qt::QueuedConnection);
                }
                QMetaObject::invokeMethod(this, [this, peerId, fileId, wasCanceled] {
                    m_activeOutTransfers.remove(fileId);
                    emit fileTransferFinished(peerId, fileId, false, wasCanceled ? tr("Canceled") : tr("Timed out"));
                }, Qt::QueuedConnection);
                return;
            }
        }

            QTcpSocket socket;
            socket.connectToHost(peer.address, peer.port);
            if (!socket.waitForConnected(SendTimeoutMs)) {
                QMetaObject::invokeMethod(this, [this] {
                    emit errorOccurred(tr("Could not connect to peer for file transfer."));
                }, Qt::QueuedConnection);
            m_activeOutTransfers.remove(fileId);
                return;
            }

            for (int i = 0; i < totalChunks; ++i) {
            if (transferState->loadRelaxed() == 2) {
                QJsonObject cancelPayload;
                cancelPayload.insert(QStringLiteral("type"), QStringLiteral("cancelTransfer"));
                cancelPayload.insert(QStringLiteral("fromId"), localId);
                cancelPayload.insert(QStringLiteral("fileId"), fileId);
                const QByteArray json = QJsonDocument(cancelPayload).toJson(QJsonDocument::Compact);
                QByteArray frame;
                QDataStream stream(&frame, QIODevice::WriteOnly);
                stream.setByteOrder(QDataStream::BigEndian);
                stream << static_cast<quint32>(json.size());
                frame.append(json);
                socket.write(frame);
                socket.waitForBytesWritten(SendTimeoutMs);
                break;
            }

                const QByteArray chunk = file.read(chunkSize);
                QJsonObject payload;
                payload.insert(QStringLiteral("type"), QStringLiteral("fileChunk"));
                payload.insert(QStringLiteral("fromId"), localId);
                payload.insert(QStringLiteral("fromName"), localName);
                payload.insert(QStringLiteral("presenceStatus"), effectiveLocalStatus());
                payload.insert(QStringLiteral("fileId"), fileId);
                payload.insert(QStringLiteral("fileName"), fileName);
                payload.insert(QStringLiteral("isImage"), isImage);
                payload.insert(QStringLiteral("chunkIndex"), i);
                payload.insert(QStringLiteral("totalChunks"), totalChunks);
                payload.insert(QStringLiteral("data"), QString::fromLatin1(chunk.toBase64()));

                const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
                QByteArray frame;
                QDataStream stream(&frame, QIODevice::WriteOnly);
                stream.setByteOrder(QDataStream::BigEndian);
                stream << static_cast<quint32>(json.size());
                frame.append(json);

                socket.write(frame);
                if (!socket.waitForBytesWritten(SendTimeoutMs)) {
                QMetaObject::invokeMethod(this, [this, peerId, fileId] {
                    m_activeOutTransfers.remove(fileId);
                    emit fileTransferFinished(peerId, fileId, false, tr("Timed out"));
                    emit errorOccurred(tr("File transfer timed out."));
                    }, Qt::QueuedConnection);
                    return;
                }

            QMetaObject::invokeMethod(this, [this, peerId, fileId, i] {
                emit fileTransferProgress(peerId, fileId, i + 1);
            }, Qt::QueuedConnection);
            }

            socket.disconnectFromHost();
            if (socket.state() != QAbstractSocket::UnconnectedState) {
                socket.waitForDisconnected(SendTimeoutMs);
            }

        bool wasCanceled = transferState->loadRelaxed() == 2;

        QMetaObject::invokeMethod(this, [this, peerId, fileId, wasCanceled, fileName, filePath, isImage] {
            m_activeOutTransfers.remove(fileId);
            if (wasCanceled) {
                emit fileTransferFinished(peerId, fileId, false, tr("Canceled"));
            } else {
                emit fileTransferFinished(peerId, fileId, true, QString());
                emit fileSent(peerId, fileName, filePath, isImage);
            }
            }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void LanChatService::sendPublicMessage(const QString &message, bool isHtml)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QJsonObject payload;
    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    payload.insert(QStringLiteral("type"), QStringLiteral("publicChat"));
    payload.insert(QStringLiteral("messageId"), messageId);
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fromName"), m_localName);
    payload.insert(QStringLiteral("message"), trimmed);
    payload.insert(QStringLiteral("isHtml"), isHtml);

    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QSet<QHostAddress> broadcastAddresses;
    broadcastAddresses.insert(QHostAddress::Broadcast);

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        const bool canBroadcast = networkInterface.flags().testFlag(QNetworkInterface::CanBroadcast);
        const bool isRunning = networkInterface.flags().testFlag(QNetworkInterface::IsRunning);
        const bool isLoopback = networkInterface.flags().testFlag(QNetworkInterface::IsLoopBack);
        if (!canBroadcast || !isRunning || isLoopback) {
            continue;
        }

        const auto entries = networkInterface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress broadcast = entry.broadcast();
            if (broadcast.protocol() == QAbstractSocket::IPv4Protocol && !broadcast.isNull()) {
                broadcastAddresses.insert(broadcast);
            }
        }
    }

    for (const QHostAddress &broadcastAddress : std::as_const(broadcastAddresses)) {
        m_udpSocket->writeDatagram(data, broadcastAddress, DiscoveryPort);
    }
    emit publicMessageSent(trimmed, messageId, isHtml);
}

void LanChatService::sendMessageReceipt(const QString &peerId, const QString &messageId, const QString &status)
{
    if (peerId.isEmpty() || messageId.isEmpty()) {
        return;
    }

    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("receipt"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fromName"), m_localName);
    payload.insert(QStringLiteral("messageId"), messageId);
    payload.insert(QStringLiteral("status"), status);
    sendSilentJsonPayload(*peerIt, payload);
}

void LanChatService::sendTypingState(const QString &peerId, bool isTyping)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("typing"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fromName"), m_localName);
    payload.insert(QStringLiteral("isTyping"), isTyping);
    sendSilentJsonPayload(*peerIt, payload);
}

void LanChatService::sendBuzz(const QString &peerId)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        emit errorOccurred(tr("That peer is no longer online."));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("buzz"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fromName"), m_localName);
    payload.insert(QStringLiteral("presenceStatus"), effectiveLocalStatus());
    sendJsonPayloadToHost(peerIt->address,
                          peerIt->port,
                          payload,
                          [this, peerId] {
                              emit buzzSent(peerId);
                          },
                          [this](const QString &reason) {
                              emit errorOccurred(tr("Could not send buzz: %1").arg(reason));
                          });
}

void LanChatService::sendGameAction(const QString &peerId, const QString &game, const QString &action, const QString &gameId, const QString &value, const QString &board)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        emit errorOccurred(tr("That peer is no longer online."));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("game"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fromName"), m_localName);
    payload.insert(QStringLiteral("presenceStatus"), effectiveLocalStatus());
    payload.insert(QStringLiteral("game"), game);
    payload.insert(QStringLiteral("action"), action);
    payload.insert(QStringLiteral("gameId"), gameId);
    payload.insert(QStringLiteral("value"), value);
    payload.insert(QStringLiteral("board"), board);
    sendJsonPayloadToHost(peerIt->address,
                          peerIt->port,
                          payload,
                          [this, peerId, game, action, gameId] {
                              emit gameActionSent(peerId, game, action, gameId);
                          },
                          [this](const QString &reason) {
                              emit errorOccurred(tr("Could not send game move: %1").arg(reason));
                          });
}

void LanChatService::readPendingDatagrams()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
        const QJsonDocument document = QJsonDocument::fromJson(datagram.data());
        if (!document.isObject()) {
            continue;
        }

        handlePresence(document.object(), datagram.senderAddress());
    }
}

void LanChatService::acceptIncomingConnection()
{
    while (QTcpSocket *socket = m_tcpServer->nextPendingConnection()) {
        socket->setParent(this);
        auto *buffer = new QByteArray;
        auto *expectedSize = new quint32(0);
        connect(socket, &QObject::destroyed, this, [buffer, expectedSize] {
            delete buffer;
            delete expectedSize;
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, expectedSize] {
            buffer->append(socket->readAll());
            while (true) {
                if (*expectedSize == 0) {
                    if (buffer->size() < static_cast<int>(sizeof(quint32))) {
                        return;
                    }
                    QDataStream stream(buffer->left(sizeof(quint32)));
                    stream.setByteOrder(QDataStream::BigEndian);
                    stream >> *expectedSize;
                    buffer->remove(0, sizeof(quint32));
                }

                if (buffer->size() < static_cast<int>(*expectedSize)) {
                    return;
                }

                const QByteArray frame = buffer->left(*expectedSize);
                buffer->remove(0, *expectedSize);
                *expectedSize = 0;
                auto *frameSocket = new QTcpSocket(this);
                frameSocket->setProperty("payload", frame);
                frameSocket->setProperty("senderAddress", socket->peerAddress().toString());
                handleChatSocket(frameSocket);
                frameSocket->deleteLater();
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void LanChatService::sendPresence()
{
    if (m_localStatus == tr("Invisible")) {
        return;
    }

    sendPresenceMessage(QStringLiteral("hello"));
}

void LanChatService::pruneStalePeers()
{
    const auto now = QDateTime::currentDateTimeUtc();
    const auto ids = m_peers.keys();
    for (const QString &peerId : ids) {
        if (m_peers.value(peerId).lastSeen.msecsTo(now) > PeerTimeoutMs) {
            m_peers.remove(peerId);
            emit peerLeft(peerId, false);
        }
    }

    auto it = g_incomingTransfers.begin();
    while (it != g_incomingTransfers.end()) {
        if (it.value().lastUpdated.secsTo(now) > 3600) { // Prune abandoned transfers
            QFile::remove(it.value().tempPath);
            it = g_incomingTransfers.erase(it);
        } else {
            ++it;
        }
    }
}

void LanChatService::handlePresence(const QJsonObject &message, const QHostAddress &sender)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString peerId = message.value(QStringLiteral("id")).toString();
    if (type == QStringLiteral("publicChat")) {
        const QString fromId = message.value(QStringLiteral("fromId")).toString();
        if (fromId.isEmpty() || fromId == m_localId) {
            return;
        }
        const QString messageId = message.value(QStringLiteral("messageId")).toString();
        if (!messageId.isEmpty()) {
            const QString seenKey = QStringLiteral("%1:%2").arg(fromId, messageId);
            if (m_seenPublicMessageIds.contains(seenKey)) {
                return;
            }
            m_seenPublicMessageIds.insert(seenKey);
            if (m_seenPublicMessageIds.size() > 500) {
                m_seenPublicMessageIds.erase(m_seenPublicMessageIds.begin());
            }
        }
        emit publicMessageReceived(fromId,
                                   message.value(QStringLiteral("fromName")).toString(peerDisplayName(fromId)),
                                   decryptedMessageText(message),
                                   messageId,
                                   message.value(QStringLiteral("isHtml")).toBool(false));
        return;
    }

    if (peerId.isEmpty() || peerId == m_localId) {
        return;
    }

    if (type == QStringLiteral("bye")) {
        if (m_peers.remove(peerId) > 0) {
            emit peerLeft(peerId, true);
        }
        return;
    }

    if (type != QStringLiteral("hello")) {
        return;
    }

    ChatPeer peer;
    peer.id = peerId;
    peer.name = message.value(QStringLiteral("name")).toString(tr("Unknown"));
    peer.status = message.value(QStringLiteral("status")).toString(tr("Available"));
    peer.personalMessage = message.value(QStringLiteral("personalMessage")).toString();
    peer.themeColor = message.value(QStringLiteral("themeColor")).toString();
    peer.avatarData = QByteArray::fromBase64(message.value(QStringLiteral("avatar")).toString().toLatin1());
    peer.publicKeyData = QByteArray::fromBase64(message.value(QStringLiteral("publicKey")).toString().toLatin1());
    peer.address = sender;
    peer.port = static_cast<quint16>(message.value(QStringLiteral("port")).toInt());
    peer.lastSeen = QDateTime::currentDateTimeUtc();
    peer.publicChatOpen = message.value(QStringLiteral("publicChatOpen")).toBool(false);
    peer.protocolVersion = message.value(QStringLiteral("protocolVersion")).toInt(1);

    if (peer.port == 0) {
        return;
    }

    const bool isNewPeer = !m_peers.contains(peer.id);
    m_peers.insert(peer.id, peer);

    if (isNewPeer) {
        emit peerJoined(peer);
    } else {
        emit peerUpdated(peer);
    }
}

void LanChatService::handleChatSocket(QTcpSocket *socket)
{
    const QByteArray payloadData = socket->property("payload").toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(payloadData.isEmpty() ? socket->readAll() : payloadData);
    if (!document.isObject()) {
        return;
    }

    const QJsonObject payload = document.object();
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("chat")
        && type != QStringLiteral("file")
        && type != QStringLiteral("fileOffer")
        && type != QStringLiteral("fileAccept")
        && type != QStringLiteral("fileReject")
        && type != QStringLiteral("fileChunk")
        && type != QStringLiteral("receipt")
        && type != QStringLiteral("typing")
        && type != QStringLiteral("buzz")
        && type != QStringLiteral("game")
        && type != QStringLiteral("privateChatRequest")
        && type != QStringLiteral("privateChatReject")
        && type != QStringLiteral("manualHello")
        && type != QStringLiteral("manualHelloAck")
        && type != QStringLiteral("presenceUpdate")
        && type != QStringLiteral("presenceBye")
        && type != QStringLiteral("cancelTransfer")) {
        return;
    }

    if (type == QStringLiteral("presenceBye")) {
        const QString peerId = payload.value(QStringLiteral("id")).toString();
        if (!peerId.isEmpty() && peerId != m_localId && m_peers.remove(peerId) > 0) {
            emit peerLeft(peerId, true);
        }
        return;
    }

    if (type == QStringLiteral("presenceUpdate")) {
        const QString peerId = payload.value(QStringLiteral("id")).toString();
        if (peerId.isEmpty() || peerId == m_localId) {
            return;
        }

        const bool isNewPeer = !m_peers.contains(peerId);
        ChatPeer peer = m_peers.value(peerId);
        peer.id = peerId;
        peer.name = payload.value(QStringLiteral("name")).toString(peer.name.isEmpty() ? tr("Unknown") : peer.name);
        peer.status = payload.value(QStringLiteral("status")).toString(peer.status.isEmpty() ? tr("Available") : peer.status);
        peer.personalMessage = payload.value(QStringLiteral("personalMessage")).toString(peer.personalMessage);
        peer.themeColor = payload.value(QStringLiteral("themeColor")).toString(peer.themeColor);
        const QString avatar = payload.value(QStringLiteral("avatar")).toString();
        if (!avatar.isEmpty()) {
            peer.avatarData = QByteArray::fromBase64(avatar.toLatin1());
        }
        const QString publicKey = payload.value(QStringLiteral("publicKey")).toString();
        if (!publicKey.isEmpty()) {
            peer.publicKeyData = QByteArray::fromBase64(publicKey.toLatin1());
        }
        const QHostAddress socketAddress = socket->peerAddress();
        if (!socketAddress.isNull()) {
            peer.address = socketAddress;
        } else if (peer.address.isNull()) {
            peer.address = QHostAddress(socket->property("senderAddress").toString());
        }
        const int port = payload.value(QStringLiteral("port")).toInt(peer.port);
        if (port > 0) {
            peer.port = static_cast<quint16>(port);
        }
        peer.lastSeen = QDateTime::currentDateTimeUtc();
        peer.publicChatOpen = payload.value(QStringLiteral("publicChatOpen")).toBool(peer.publicChatOpen);
        peer.protocolVersion = payload.value(QStringLiteral("protocolVersion")).toInt(peer.protocolVersion <= 0 ? 1 : peer.protocolVersion);
        if (peer.address.isNull() || peer.port == 0) {
            return;
        }

        m_peers.insert(peer.id, peer);
        if (isNewPeer) {
            emit peerJoined(peer);
        } else {
            emit peerUpdated(peer);
        }
        return;
    }

    if (type == QStringLiteral("manualHello") || type == QStringLiteral("manualHelloAck")) {
        const QString peerId = payload.value(QStringLiteral("id")).toString();
        if (peerId.isEmpty() || peerId == m_localId) {
            return;
        }

        ChatPeer peer;
        peer.id = peerId;
        peer.name = payload.value(QStringLiteral("name")).toString(tr("Unknown"));
        peer.status = payload.value(QStringLiteral("status")).toString(tr("Available"));
        peer.personalMessage = payload.value(QStringLiteral("personalMessage")).toString();
        peer.themeColor = payload.value(QStringLiteral("themeColor")).toString();
        peer.avatarData = QByteArray::fromBase64(payload.value(QStringLiteral("avatar")).toString().toLatin1());
        peer.publicKeyData = QByteArray::fromBase64(payload.value(QStringLiteral("publicKey")).toString().toLatin1());
        peer.address = socket->peerAddress();
        if (peer.address.isNull()) {
            peer.address = QHostAddress(socket->property("senderAddress").toString());
        }
        peer.port = static_cast<quint16>(payload.value(QStringLiteral("port")).toInt());
        peer.lastSeen = QDateTime::currentDateTimeUtc();
        peer.publicChatOpen = payload.value(QStringLiteral("publicChatOpen")).toBool(false);
        peer.protocolVersion = payload.value(QStringLiteral("protocolVersion")).toInt(1);
        if (peer.address.isNull() || peer.port == 0) {
            return;
        }

        const bool isNewPeer = !m_peers.contains(peer.id);
        m_peers.insert(peer.id, peer);
        if (isNewPeer) {
            emit peerJoined(peer);
        } else {
            emit peerUpdated(peer);
        }

        if (type == QStringLiteral("manualHello")) {
            QJsonObject ack;
            ack.insert(QStringLiteral("type"), QStringLiteral("manualHelloAck"));
            ack.insert(QStringLiteral("id"), m_localId);
            ack.insert(QStringLiteral("name"), m_localName);
            ack.insert(QStringLiteral("status"), effectiveLocalStatus());
            ack.insert(QStringLiteral("personalMessage"), m_localPersonalMessage);
            ack.insert(QStringLiteral("themeColor"), m_localThemeColor);
            ack.insert(QStringLiteral("avatar"), QString::fromLatin1(m_localAvatarData.toBase64()));
            ack.insert(QStringLiteral("publicKey"), QString::fromLatin1(m_publicKeyData.toBase64()));
            ack.insert(QStringLiteral("port"), static_cast<int>(m_tcpServer->serverPort()));
            ack.insert(QStringLiteral("publicChatOpen"), m_publicChatOpen);
            ack.insert(QStringLiteral("protocolVersion"), ProtocolVersion);
            sendJsonPayloadToHost(peer.address, peer.port, ack, [] {}, [](const QString &) {});
        } else {
            const QString peerAddressKey = peer.address.toString();
            if (m_pendingManualConnectionHosts.contains(peerAddressKey)) {
                m_pendingManualConnectionHosts.remove(peerAddressKey);
            }
            emit manualConnectionSucceeded(endpointLabel(peer.address, peer.port), peer.name);
        }
        return;
    }

    const QString peerId = payload.value(QStringLiteral("fromId")).toString();
    const QString peerName = payload.value(QStringLiteral("fromName")).toString(peerDisplayName(peerId));
    const QString peerPresenceStatus = payload.value(QStringLiteral("presenceStatus")).toString();
    if (!peerId.isEmpty() && !peerPresenceStatus.isEmpty()) {
        auto peerIt = m_peers.find(peerId);
        if (peerIt != m_peers.end() && peerIt->status != peerPresenceStatus) {
            peerIt->status = peerPresenceStatus;
            peerIt->lastSeen = QDateTime::currentDateTimeUtc();
            emit peerUpdated(*peerIt);
        }
    }
    const bool privateAllowed = allowsPrivateInteraction(peerId, socket->peerAddress());
    const QString privateRejectReason = privateAllowed ? QString() : privateInteractionRejectReason();

    if (type == QStringLiteral("fileOffer")) {
        if (!privateAllowed) {
            const QString fileId = payload.value(QStringLiteral("fileId")).toString();
            if (!fileId.isEmpty()) {
                const auto peerIt = m_peers.constFind(peerId);
                if (peerIt != m_peers.constEnd()) {
                    QJsonObject reject;
                    reject.insert(QStringLiteral("type"), QStringLiteral("fileReject"));
                    reject.insert(QStringLiteral("fromId"), m_localId);
                    reject.insert(QStringLiteral("fileId"), fileId);
                    sendSilentJsonPayload(*peerIt, reject);
                }
            }
            sendPrivateChatReject(peerId, privateRejectReason);
            return;
        }
        const QString fileId = payload.value(QStringLiteral("fileId")).toString();
        const QString fileName = payload.value(QStringLiteral("fileName")).toString();
        const qint64 fileSize = payload.value(QStringLiteral("fileSize")).toVariant().toLongLong();
        const bool isImage = payload.value(QStringLiteral("isImage")).toBool(false);
        if (!peerId.isEmpty() && !fileId.isEmpty()) {
            if (isImage) {
                acceptFileTransfer(peerId, fileId);
            } else {
                emit fileTransferOffered(peerId, peerName, fileId, fileName, fileSize);
            }
        }
        return;
    }

    if (type == QStringLiteral("fileAccept")) {
        const QString fileId = payload.value(QStringLiteral("fileId")).toString();
        if (m_activeOutTransfers.contains(fileId)) {
            m_activeOutTransfers.value(fileId)->storeRelaxed(1);
        }
        return;
    }

    if (type == QStringLiteral("fileReject")) {
        const QString fileId = payload.value(QStringLiteral("fileId")).toString();
        if (m_activeOutTransfers.contains(fileId)) {
            m_activeOutTransfers.value(fileId)->storeRelaxed(2);
        }
        emit fileTransferFinished(peerId, fileId, false, tr("Declined by peer"));
        return;
    }

    if (type == QStringLiteral("cancelTransfer")) {
        const QString fileId = payload.value(QStringLiteral("fileId")).toString();
        if (m_activeOutTransfers.contains(fileId)) {
            m_activeOutTransfers.value(fileId)->storeRelaxed(2);
            emit fileTransferFinished(peerId, fileId, false, tr("Canceled by peer"));
        } else if (g_incomingTransfers.contains(fileId)) {
            QFile::remove(g_incomingTransfers[fileId].tempPath);
            g_incomingTransfers.remove(fileId);
            emit fileTransferFinished(peerId, fileId, false, tr("Canceled by peer"));
        }
        return;
    }

    if (type == QStringLiteral("buzz")) {
        if (!privateAllowed) {
            sendPrivateChatReject(peerId, privateRejectReason);
            return;
        }
        if (!peerId.isEmpty()) {
            emit buzzReceived(peerId, peerName);
        }
        return;
    }

    if (type == QStringLiteral("game")) {
        if (!privateAllowed) {
            sendPrivateChatReject(peerId, privateRejectReason);
            return;
        }
        if (!peerId.isEmpty()) {
            emit gameActionReceived(peerId,
                                    peerName,
                                    payload.value(QStringLiteral("game")).toString(),
                                    payload.value(QStringLiteral("action")).toString(),
                                    payload.value(QStringLiteral("gameId")).toString(),
                                    payload.value(QStringLiteral("value")).toString(),
                                    payload.value(QStringLiteral("board")).toString());
        }
        return;
    }

    if (type == QStringLiteral("privateChatRequest")) {
        if (!peerId.isEmpty()) {
            sendPrivateChatReject(peerId, tr("Private chat requests are no longer used. Open a private chat directly instead."));
        }
        return;
    }

    if (type == QStringLiteral("privateChatReject")) {
        if (!peerId.isEmpty()) {
            emit privateChatRejected(peerId,
                                     peerName,
                                     payload.value(QStringLiteral("reason")).toString(tr("This user is not accepting private chat requests.")));
        }
        return;
    }

    if (type == QStringLiteral("typing")) {
        if (!privateAllowed) {
            return;
        }
        if (!peerId.isEmpty()) {
            emit typingStateReceived(peerId, payload.value(QStringLiteral("isTyping")).toBool());
        }
        return;
    }

    if (type == QStringLiteral("receipt")) {
        const QString messageId = payload.value(QStringLiteral("messageId")).toString();
        const QString status = payload.value(QStringLiteral("status")).toString();
        if (!peerId.isEmpty() && !messageId.isEmpty() && !status.isEmpty()) {
            emit messageReceiptReceived(peerId, messageId, status);
        }
        return;
    }

    if (type == QStringLiteral("chat")) {
        if (!privateAllowed) {
            sendPrivateChatReject(peerId, privateRejectReason);
            return;
        }
        const QString message = decryptedPrivateMessageText(payload, peerId);
        const QString messageId = payload.value(QStringLiteral("messageId")).toString();
        const bool isHtml = payload.value(QStringLiteral("isHtml")).toBool(false);
        if (!peerId.isEmpty() && !message.trimmed().isEmpty()) {
            emit messageReceived(peerId, peerName, message, messageId, isHtml);
        }
        return;
    }

    if (type == QStringLiteral("fileChunk")) {
        if (!privateAllowed) {
            if (payload.value(QStringLiteral("chunkIndex")).toInt() == 0) {
                sendPrivateChatReject(peerId, privateRejectReason);
            }
            return;
        }
        const QString fileId = payload.value(QStringLiteral("fileId")).toString();
        const QString fileName = payload.value(QStringLiteral("fileName")).toString(tr("file"));
        const bool isImage = payload.value(QStringLiteral("isImage")).toBool(false);
        const int chunkIndex = payload.value(QStringLiteral("chunkIndex")).toInt();
        const int totalChunks = payload.value(QStringLiteral("totalChunks")).toInt();
        const QByteArray data = QByteArray::fromBase64(payload.value(QStringLiteral("data")).toString().toLatin1());

        if (fileId.isEmpty()) {
            return;
        }

        if (chunkIndex > 0 && !g_incomingTransfers.contains(fileId)) {
            return; // Transfer was canceled or is invalid, safely ignore remaining socket chunks
        }

        IncomingTransfer &transfer = g_incomingTransfers[fileId];
        transfer.lastUpdated = QDateTime::currentDateTimeUtc();

        if (chunkIndex == 0) {
            transfer.senderId = peerId;
            transfer.senderName = peerName;
            transfer.fileName = fileName;
            transfer.isImage = isImage;
            transfer.expectedChunks = totalChunks;
            transfer.chunksReceived = 0;

            QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            if (tempPath.isEmpty()) {
                tempPath = QDir::tempPath();
            }
            QDir dir(tempPath);
            dir.mkpath(QStringLiteral("BlinqMessenger_Incoming"));
            dir.cd(QStringLiteral("BlinqMessenger_Incoming"));
            transfer.tempPath = dir.filePath(fileId + QStringLiteral("_") + fileName);
            QFile::remove(transfer.tempPath);

            emit fileTransferStarted(peerId, fileId, fileName, totalChunks, false);
        }

        QFile file(transfer.tempPath);
        if (file.open(QIODevice::Append)) {
            file.write(data);
            file.close();
            transfer.chunksReceived++;
        }

        emit fileTransferProgress(peerId, fileId, transfer.chunksReceived);

        if (transfer.chunksReceived >= transfer.expectedChunks) {
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray assembledData = file.readAll();
                file.close();
                if (!peerId.isEmpty()) {
                    emit fileTransferFinished(peerId, fileId, true, QString());
                    emit fileReceived(peerId, peerName, fileName, assembledData, isImage);
                }
            }
            QFile::remove(transfer.tempPath);
            g_incomingTransfers.remove(fileId);
        }
        return;
    }

    const QString fileName = payload.value(QStringLiteral("fileName")).toString(tr("file"));
    const QByteArray data = QByteArray::fromBase64(payload.value(QStringLiteral("data")).toString().toLatin1());
    const bool isImage = payload.value(QStringLiteral("isImage")).toBool(false);
    if (!privateAllowed) {
        sendPrivateChatReject(peerId, privateRejectReason);
        return;
    }
    if (!peerId.isEmpty() && !data.isEmpty()) {
        emit fileReceived(peerId, peerName, fileName, data, isImage);
    }
}

void LanChatService::cancelTransfer(const QString &fileId)
{
    if (m_activeOutTransfers.contains(fileId)) {
        m_activeOutTransfers.value(fileId)->storeRelaxed(2);
    } else if (g_incomingTransfers.contains(fileId)) {
        const QString peerId = g_incomingTransfers[fileId].senderId;
        QFile::remove(g_incomingTransfers[fileId].tempPath);
        g_incomingTransfers.remove(fileId);
        emit fileTransferFinished(peerId, fileId, false, tr("Canceled"));

        const auto peerIt = m_peers.constFind(peerId);
        if (peerIt != m_peers.constEnd()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("type"), QStringLiteral("cancelTransfer"));
            payload.insert(QStringLiteral("fromId"), m_localId);
            payload.insert(QStringLiteral("fileId"), fileId);
            sendSilentJsonPayload(*peerIt, payload);
        }
    }
}

void LanChatService::acceptFileTransfer(const QString &peerId, const QString &fileId)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        return;
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("fileAccept"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fileId"), fileId);
    sendSilentJsonPayload(*peerIt, payload);
}

void LanChatService::rejectFileTransfer(const QString &peerId, const QString &fileId)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        return;
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("fileReject"));
    payload.insert(QStringLiteral("fromId"), m_localId);
    payload.insert(QStringLiteral("fileId"), fileId);
    sendSilentJsonPayload(*peerIt, payload);
}
void LanChatService::sendJsonPayload(const ChatPeer &peer,
                                     const QJsonObject &payload,
                                     const QString &successPeerId,
                                     const QString &successText,
                                     bool isFile,
                                     const QString &messageId,
                                     bool isHtml,
                                     const QString &filePath,
                                     bool isImage)
{
    sendJsonPayloadToHost(peer.address,
                          peer.port,
                          payload,
                          [this, successPeerId, successText, isFile, messageId, isHtml, filePath, isImage] {
                              if (isFile) {
                                  emit fileSent(successPeerId, successText, filePath, isImage);
                              } else {
                                  emit messageSent(successPeerId, successText, messageId, isHtml);
                              }
                          },
                          [this](const QString &reason) {
                              emit errorOccurred(tr("Could not send: %1").arg(reason));
                          });
}

void LanChatService::sendJsonPayloadToHost(const QHostAddress &address,
                                           quint16 port,
                                           const QJsonObject &payload,
                                           std::function<void()> onConnected,
                                           std::function<void(const QString &)> onFailed)
{
    auto *socket = new QTcpSocket(this);
    socket->setProperty("handled", false);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, this, [socket, onFailed](QAbstractSocket::SocketError) {
        if (socket->property("handled").toBool()) {
            return;
        }
        socket->setProperty("handled", true);
        onFailed(socket->errorString());
        socket->deleteLater();
    });
    QTimer::singleShot(SendTimeoutMs, socket, [socket, onFailed] {
        if (socket->property("handled").toBool()) {
            return;
        }
        if (socket->state() == QAbstractSocket::ConnectedState) {
            return;
        }
        socket->setProperty("handled", true);
        socket->abort();
        onFailed(QObject::tr("connection timed out"));
        socket->deleteLater();
    });

    socket->connectToHost(address, port);
    connect(socket, &QTcpSocket::connected, this, [socket, payload, onConnected] {
        socket->setProperty("handled", true);
        const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        QByteArray frame;
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << static_cast<quint32>(json.size());
        frame.append(json);
        socket->write(frame);
        socket->flush();
        socket->disconnectFromHost();
        onConnected();
    });
}

bool LanChatService::allowsPrivateInteraction(const QString &peerId, const QHostAddress &senderAddress) const
{
    Q_UNUSED(senderAddress);
    return !peerId.isEmpty() && m_localStatus != tr("Do Not Disturb");
}

QString LanChatService::privateInteractionRejectReason() const
{
    return tr("This user is in Do Not Disturb and is not receiving private messages.");
}

void LanChatService::sendPrivateChatReject(const QString &peerId, const QString &reason)
{
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd()) {
        return;
    }
    QJsonObject reject;
    reject.insert(QStringLiteral("type"), QStringLiteral("privateChatReject"));
    reject.insert(QStringLiteral("fromId"), m_localId);
    reject.insert(QStringLiteral("fromName"), m_localName);
    reject.insert(QStringLiteral("reason"), reason);
    sendSilentJsonPayload(*peerIt, reject);
}

void LanChatService::sendSilentJsonPayload(const ChatPeer &peer, const QJsonObject &payload)
{
    auto *socket = new QTcpSocket(this);
    socket->setProperty("handled", false);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, [socket](QAbstractSocket::SocketError) {
        socket->setProperty("handled", true);
        socket->deleteLater();
    });
    QTimer::singleShot(SendTimeoutMs, socket, [socket] {
        if (socket->property("handled").toBool()) {
            return;
        }
        if (socket->state() == QAbstractSocket::ConnectedState) {
            return;
        }
        socket->setProperty("handled", true);
        socket->abort();
        socket->deleteLater();
    });

    socket->connectToHost(peer.address, peer.port);
    connect(socket, &QTcpSocket::connected, this, [socket, payload] {
        socket->setProperty("handled", true);
        const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        QByteArray frame;
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << static_cast<quint32>(json.size());
        frame.append(json);
        socket->write(frame);
        socket->flush();
        socket->disconnectFromHost();
    });
}

void LanChatService::sendPresenceMessage(const QString &type)
{
    if (!m_tcpServer->isListening()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), type);
    payload.insert(QStringLiteral("id"), m_localId);
    payload.insert(QStringLiteral("name"), m_localName);
    payload.insert(QStringLiteral("status"), effectiveLocalStatus());
    payload.insert(QStringLiteral("personalMessage"), m_localPersonalMessage);
    payload.insert(QStringLiteral("themeColor"), m_localThemeColor);
    payload.insert(QStringLiteral("avatar"), QString::fromLatin1(m_localAvatarData.toBase64()));
    payload.insert(QStringLiteral("publicKey"), QString::fromLatin1(m_publicKeyData.toBase64()));
    payload.insert(QStringLiteral("port"), static_cast<int>(m_tcpServer->serverPort()));
    payload.insert(QStringLiteral("publicChatOpen"), m_publicChatOpen);
    payload.insert(QStringLiteral("protocolVersion"), ProtocolVersion);

    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QSet<QHostAddress> broadcastAddresses;
    broadcastAddresses.insert(QHostAddress::Broadcast);

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        const bool canBroadcast = networkInterface.flags().testFlag(QNetworkInterface::CanBroadcast);
        const bool isRunning = networkInterface.flags().testFlag(QNetworkInterface::IsRunning);
        const bool isLoopback = networkInterface.flags().testFlag(QNetworkInterface::IsLoopBack);
        if (!canBroadcast || !isRunning || isLoopback) {
            continue;
        }

        const auto entries = networkInterface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress broadcast = entry.broadcast();
            if (broadcast.protocol() == QAbstractSocket::IPv4Protocol && !broadcast.isNull()) {
                broadcastAddresses.insert(broadcast);
            }
        }
    }

    for (const QHostAddress &broadcastAddress : std::as_const(broadcastAddresses)) {
        m_udpSocket->writeDatagram(data, broadcastAddress, DiscoveryPort);
    }
}

void LanChatService::sendDirectPresenceUpdate(bool includeLargeProfile)
{
    if (!m_tcpServer->isListening() || m_peers.isEmpty() || m_localStatus == tr("Invisible")) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("presenceUpdate"));
    payload.insert(QStringLiteral("id"), m_localId);
    payload.insert(QStringLiteral("name"), m_localName);
    payload.insert(QStringLiteral("status"), effectiveLocalStatus());
    payload.insert(QStringLiteral("personalMessage"), m_localPersonalMessage);
    payload.insert(QStringLiteral("themeColor"), m_localThemeColor);
    payload.insert(QStringLiteral("port"), static_cast<int>(m_tcpServer->serverPort()));
    payload.insert(QStringLiteral("publicChatOpen"), m_publicChatOpen);
    payload.insert(QStringLiteral("protocolVersion"), ProtocolVersion);
    if (includeLargeProfile) {
        payload.insert(QStringLiteral("avatar"), QString::fromLatin1(m_localAvatarData.toBase64()));
        payload.insert(QStringLiteral("publicKey"), QString::fromLatin1(m_publicKeyData.toBase64()));
    }

    const QList<ChatPeer> peers = m_peers.values();
    for (const ChatPeer &peer : peers) {
        if (!peer.address.isNull() && peer.port > 0) {
            sendSilentJsonPayload(peer, payload);
        }
    }
}

void LanChatService::sendDirectPresenceBye()
{
    if (m_peers.isEmpty()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("presenceBye"));
    payload.insert(QStringLiteral("id"), m_localId);

    const QList<ChatPeer> peers = m_peers.values();
    for (const ChatPeer &peer : peers) {
        if (!peer.address.isNull() && peer.port > 0) {
            sendSilentJsonPayload(peer, payload);
        }
    }
}

void LanChatService::loadSettings()
{
    QSettings settings(QStringLiteral("Exe Innovate"), QStringLiteral("Blinq Messenger"));
    m_localId = settings.value(QStringLiteral("profile/id"), m_localId).toString();
    m_localName = settings.value(QStringLiteral("profile/name"), m_localName).toString();
    m_localStatus = settings.value(QStringLiteral("profile/status"), QStringLiteral("Available")).toString();
    m_localPersonalMessage = settings.value(QStringLiteral("profile/personalMessage"), tr("Hi, let's chat!")).toString();
    if (m_localPersonalMessage.trimmed().isEmpty()) {
        m_localPersonalMessage = tr("Hi, let's chat!");
    }
    m_localAvatarData = settings.value(QStringLiteral("profile/avatar")).toByteArray();
    m_privateKeyData = QByteArray::fromBase64(settings.value(QStringLiteral("profile/privateMessagePrivateKey")).toByteArray());
    m_publicKeyData = QByteArray::fromBase64(settings.value(QStringLiteral("profile/privateMessagePublicKey")).toByteArray());

    if (m_localName.trimmed().isEmpty()) {
        m_localName = QSysInfo::machineHostName();
    }
    if (m_localName.trimmed().isEmpty()) {
        m_localName = tr("Blinq Messenger user");
    }
    ensurePrivateMessageKeys();
    saveSettings();
}

void LanChatService::saveSettings() const
{
    QSettings settings(QStringLiteral("Exe Innovate"), QStringLiteral("Blinq Messenger"));
    settings.setValue(QStringLiteral("profile/id"), m_localId);
    settings.setValue(QStringLiteral("profile/name"), m_localName);
    settings.setValue(QStringLiteral("profile/status"), m_localStatus);
    settings.setValue(QStringLiteral("profile/personalMessage"), m_localPersonalMessage);
    settings.setValue(QStringLiteral("profile/avatar"), m_localAvatarData);
    settings.setValue(QStringLiteral("profile/privateMessagePrivateKey"), m_privateKeyData.toBase64());
    settings.setValue(QStringLiteral("profile/privateMessagePublicKey"), m_publicKeyData.toBase64());
}

void LanChatService::ensurePrivateMessageKeys()
{
#ifdef Q_OS_WIN
    if (!m_privateKeyData.isEmpty() && !m_publicKeyData.isEmpty()) {
        return;
    }

    QByteArray privateBlob;
    QByteArray publicBlob;
    if (generateEcdhP256KeyPair(&privateBlob, &publicBlob)) {
        m_privateKeyData = privateBlob;
        m_publicKeyData = publicBlob;
    }
#endif
}

QString LanChatService::peerDisplayName(const QString &peerId) const
{
    const auto peerIt = m_peers.constFind(peerId);
    return peerIt == m_peers.constEnd() ? tr("Unknown") : peerIt->name;
}

QString LanChatService::effectiveLocalStatus() const
{
    if (m_localIdle && m_localStatus == tr("Available")) {
        return tr("Idle");
    }
    return m_localStatus;
}

QJsonObject LanChatService::messagePayloadForPeer(const QString &message, const QString &peerId) const
{
    QJsonObject payload;
    const QByteArray plainText = message.toUtf8();
    const QByteArray cipherText = cryptMessageBytes(plainText, peerId);
    payload.insert(QStringLiteral("message"), QString::fromLatin1(cipherText.toBase64()));
    payload.insert(QStringLiteral("encrypted"), true);
    payload.insert(QStringLiteral("encoding"), QStringLiteral("utf8-peer-xor-sha256-v2"));
    payload.insert(QStringLiteral("auth"), QString::fromLatin1(messageAuthCode(cipherText, peerId).toBase64()));
    return payload;
}

QString LanChatService::decryptedMessageText(const QJsonObject &payload, const QString &peerId)
{
    const QString message = payload.value(QStringLiteral("message")).toString();
    if (!payload.value(QStringLiteral("encrypted")).toBool(false)) {
        return message;
    }

    const QByteArray cipherText = QByteArray::fromBase64(message.toLatin1());
    if (cipherText.isEmpty()) {
        return QString();
    }

    const QString encoding = payload.value(QStringLiteral("encoding")).toString();
    const QByteArray expectedAuth = QByteArray::fromBase64(payload.value(QStringLiteral("auth")).toString().toLatin1());
    if (!expectedAuth.isEmpty() && expectedAuth != messageAuthCode(cipherText, peerId)) {
        emit errorOccurred(tr("Rejected a message that failed authentication."));
        return QString();
    }

    return QString::fromUtf8(cryptMessageBytes(cipherText,
                                               encoding == QStringLiteral("utf8-xor-sha256-v1") ? QString() : peerId));
}

QByteArray LanChatService::cryptMessageBytes(const QByteArray &data, const QString &peerId) const
{
    QByteArray result;
    result.resize(data.size());

    QStringList ids = {m_localId, peerId};
    ids.removeAll(QString());
    ids.sort();
    const QByteArray peerScope = ids.join(QLatin1Char(':')).toUtf8();

    qsizetype offset = 0;
    quint32 counter = 0;
    while (offset < data.size()) {
        QByteArray seed = MessageEncryptionKey;
        seed.append(peerScope);
        seed.append(reinterpret_cast<const char *>(&counter), sizeof(counter));
        const QByteArray keyStream = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
        const qsizetype chunkSize = qMin<qsizetype>(keyStream.size(), data.size() - offset);
        for (qsizetype i = 0; i < chunkSize; ++i) {
            result[offset + i] = data[offset + i] ^ keyStream.at(i);
        }
        offset += chunkSize;
        ++counter;
    }

    return result;
}

QByteArray LanChatService::messageAuthCode(const QByteArray &cipherText, const QString &peerId) const
{
    QStringList ids = {m_localId, peerId};
    ids.removeAll(QString());
    ids.sort();

    QByteArray data = MessageEncryptionKey;
    data.append(QByteArrayLiteral(":auth:"));
    data.append(ids.join(QLatin1Char(':')).toUtf8());
    data.append(cipherText);
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QJsonObject LanChatService::privateMessagePayloadForPeer(const QString &message, const ChatPeer &peer) const
{
    QJsonObject payload;
#ifdef Q_OS_WIN
    const QByteArray sharedKey = sharedSecretForPeer(m_privateKeyData, peer.publicKeyData);
    if (!sharedKey.isEmpty()) {
        const PrivateMessageCipher encrypted = encryptPrivateMessage(sharedKey, message.toUtf8());
        if (!encrypted.cipherText.isEmpty() && !encrypted.tag.isEmpty()) {
            payload.insert(QStringLiteral("message"), QString::fromLatin1(encrypted.cipherText.toBase64()));
            payload.insert(QStringLiteral("encrypted"), true);
            payload.insert(QStringLiteral("encoding"), QStringLiteral("ecdh-p256-aes-gcm-v3"));
            payload.insert(QStringLiteral("nonce"), QString::fromLatin1(encrypted.nonce.toBase64()));
            payload.insert(QStringLiteral("auth"), QString::fromLatin1(encrypted.tag.toBase64()));
            return payload;
        }
    }
#endif

    payload.insert(QStringLiteral("message"), message);
    return payload;
}

QString LanChatService::decryptedPrivateMessageText(const QJsonObject &payload, const QString &peerId)
{
    const QString encoding = payload.value(QStringLiteral("encoding")).toString();
    if (encoding != QStringLiteral("ecdh-p256-aes-gcm-v3")) {
        return decryptedMessageText(payload, peerId);
    }

#ifdef Q_OS_WIN
    const auto peerIt = m_peers.constFind(peerId);
    if (peerIt == m_peers.constEnd() || peerIt->publicKeyData.isEmpty() || m_privateKeyData.isEmpty()) {
        return QString();
    }

    const QByteArray sharedKey = sharedSecretForPeer(m_privateKeyData, peerIt->publicKeyData);
    const QByteArray cipherText = QByteArray::fromBase64(payload.value(QStringLiteral("message")).toString().toLatin1());
    const QByteArray nonce = QByteArray::fromBase64(payload.value(QStringLiteral("nonce")).toString().toLatin1());
    const QByteArray tag = QByteArray::fromBase64(payload.value(QStringLiteral("auth")).toString().toLatin1());
    const QByteArray plainText = decryptPrivateMessage(sharedKey, nonce, cipherText, tag);
    if (plainText.isEmpty() && !cipherText.isEmpty()) {
        emit errorOccurred(tr("Rejected a private message that failed encryption authentication."));
        return QString();
    }
    return QString::fromUtf8(plainText);
#else
    Q_UNUSED(peerId);
    return QString();
#endif
}
