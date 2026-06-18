#include "chatwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QPainter>
#include <QPainterPath>
#include <QMimeData>
#include <QMenu>
#include <QShortcut>
#include <QPushButton>
#include <QProgressBar>
#include <QRegularExpression>
#include <QRadioButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QSettings>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
QString compactCount(int value)
{
    const int safeValue = qMax(0, value);
    if (safeValue < 1000) {
        return QString::number(safeValue);
    }

    const auto compact = [](double amount, const QString &suffix) {
        const bool whole = qAbs(amount - qRound(amount)) < 0.05;
        return whole ? QStringLiteral("%1%2").arg(qRound(amount)).arg(suffix)
                     : QStringLiteral("%1%2").arg(amount, 0, 'f', 1).arg(suffix);
    };

    if (safeValue < 1000000) {
        return compact(static_cast<double>(safeValue) / 1000.0, QStringLiteral("k"));
    }
    return compact(static_cast<double>(safeValue) / 1000000.0, QStringLiteral("M"));
}

QString statusColor(const QString &status)
{
    if (status == QObject::tr("Available")) {
        return QStringLiteral("#15803d");
    }
    if (status == QObject::tr("Idle") || status == QObject::tr("Away")) {
        return QStringLiteral("#b45309");
    }
    if (status == QObject::tr("Busy") || status == QObject::tr("Do Not Disturb")) {
        return QStringLiteral("#b91c1c");
    }
    if (status == QObject::tr("Offline")) {
        return QStringLiteral("#64748b");
    }
    return QStringLiteral("#2563eb");
}

QString lastSeenText(const QDateTime &lastSeen, const QString &status)
{
    if (!lastSeen.isValid()) {
        return QObject::tr("Last seen unknown");
    }
    if (status != QObject::tr("Offline") && status != QObject::tr("Idle") && lastSeen.secsTo(QDateTime::currentDateTimeUtc()) < 20) {
        return QObject::tr("Last seen now");
    }
    return QObject::tr("Last seen %1").arg(lastSeen.toLocalTime().toString(QStringLiteral("MMM d, h:mm AP")));
}

QString displayMessageHtml(const QString &message, bool isHtml)
{
    if (isHtml) {
        QString fragment = message;
        QString styles;
        const QRegularExpression styleExpression(QStringLiteral("<style[^>]*>.*?</style>"),
                                                 QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator styleMatches = styleExpression.globalMatch(fragment);
        while (styleMatches.hasNext()) {
            styles += styleMatches.next().captured(0);
        }
        fragment.replace(QRegularExpression(QStringLiteral("<!DOCTYPE[^>]*>"), QRegularExpression::CaseInsensitiveOption), QString());
        const QRegularExpression bodyExpression(QStringLiteral("<body[^>]*>(.*)</body>"),
                                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch match = bodyExpression.match(fragment);
        if (match.hasMatch()) {
            fragment = match.captured(1);
        } else {
            fragment.replace(QRegularExpression(QStringLiteral("</?(html|head|body)[^>]*>"), QRegularExpression::CaseInsensitiveOption), QString());
        }
        fragment.replace(QLatin1Char('\n'), QString());
        styles.replace(QLatin1Char('\n'), QString());
        return styles + fragment;
    }
    return message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString messageWithEmoticons(QString message)
{
    message.replace(QStringLiteral("<3"), QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F"));
    message.replace(QStringLiteral(":D"), QString::fromUtf8("\xF0\x9F\x98\x84"));
    message.replace(QStringLiteral(":-D"), QString::fromUtf8("\xF0\x9F\x98\x84"));
    message.replace(QStringLiteral(":)"), QString::fromUtf8("\xF0\x9F\x99\x82"));
    message.replace(QStringLiteral(":-)"), QString::fromUtf8("\xF0\x9F\x99\x82"));
    message.replace(QStringLiteral(";)"), QString::fromUtf8("\xF0\x9F\x98\x89"));
    message.replace(QStringLiteral(";-)"), QString::fromUtf8("\xF0\x9F\x98\x89"));
    message.replace(QStringLiteral(":("), QString::fromUtf8("\xF0\x9F\x99\x81"));
    message.replace(QStringLiteral(":-("), QString::fromUtf8("\xF0\x9F\x99\x81"));
    return message;
}

bool isDisplayableImage(const QString &fileName, const QByteArray &data = QByteArray())
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QStringLiteral("png")
        || suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("gif")
        || suffix == QStringLiteral("bmp")
        || suffix == QStringLiteral("webp")) {
        return true;
    }

    if (!data.isEmpty()) {
        QImage image;
        return image.loadFromData(data);
    }
    return false;
}

QString chatTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("dddd MMMM d, yyyy • h:mm AP"));
}

QString receiptIconHtml(const QString &status)
{
    if (status.compare(QStringLiteral("Read"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("<span style=\"color:#1d9bf0; font-size:9pt; font-weight:600;\">&#10003;&#10003;</span>");
    }
    if (status.compare(QStringLiteral("Delivered"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("<span style=\"color:#111827; font-size:9pt; font-weight:600;\">&#10003;&#10003;</span>");
    }
    return QStringLiteral("<span style=\"color:#111827; font-size:9pt; font-weight:600;\">&#10003;</span>");
}

QString messageBlockHtml(const QString &sender,
                         const QString &contentHtml,
                         const QString &timestamp,
                         bool outgoing,
                         const QString &statusHtml = QString(),
                         const QColor &outgoingAccent = QColor())
{
    const QString nameColor = outgoing ? QStringLiteral("#075985") : QStringLiteral("#155e75");
    const QString accentColor = outgoing && outgoingAccent.isValid()
                                    ? outgoingAccent.name()
                                    : (outgoing ? QStringLiteral("#1d9bf0") : QStringLiteral("#0f766e"));
    const QString meta = statusHtml.isEmpty()
                             ? timestamp.toHtmlEscaped()
                             : QStringLiteral("%1&nbsp;&nbsp;%2").arg(timestamp.toHtmlEscaped(), statusHtml);

    if (outgoing) {
        return QStringLiteral(
                   "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                   "<tr>"
                   "<td width=\"18%\"></td>"
                   "<td width=\"82%\" align=\"right\" style=\"padding:3px 12px 6px 12px;\">"
                   "<div align=\"right\"><span style=\"color:%1; font-weight:700; font-size:9pt;\">%2</span>"
                   "&nbsp;&nbsp;<span style=\"color:#64748b; font-size:8pt;\">%3</span></div>"
                   "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                   "<td style=\"padding-right:8px;\"><div align=\"right\" style=\"color:#111827; font-size:11pt; margin-top:1px;\">%4</div></td>"
                   "<td width=\"3\" bgcolor=\"%5\"></td>"
                   "</tr></table>"
                   "</td>"
                   "</tr>"
                   "</table>")
            .arg(nameColor,
                 sender.toHtmlEscaped(),
                 meta,
                 contentHtml,
                 accentColor);
    }

    return QStringLiteral(
               "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
               "<tr>"
               "<td width=\"82%\" align=\"left\" style=\"padding:3px 12px 6px 12px;\">"
               "<div align=\"left\"><span style=\"color:%1; font-weight:700; font-size:9pt;\">%2</span>"
               "&nbsp;&nbsp;<span style=\"color:#64748b; font-size:8pt;\">%3</span></div>"
               "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
               "<td width=\"3\" bgcolor=\"%5\"></td>"
               "<td style=\"padding-left:8px;\"><div align=\"left\" style=\"color:#111827; font-size:11pt; margin-top:1px;\">%4</div></td>"
               "</tr></table>"
               "</td>"
               "<td width=\"18%\"></td>"
               "</tr>"
               "</table>")
        .arg(nameColor,
             sender.toHtmlEscaped(),
             meta,
             contentHtml,
             accentColor);
}

QString basicIncomingHtml(const QString &sender, const QString &message, bool isHtml)
{
    return messageBlockHtml(sender, displayMessageHtml(message, isHtml), chatTimestamp(), false);
}

QString basicIncomingHtml(const QString &sender,
                          const QString &message,
                          const QString &timestamp,
                          bool isHtml)
{
    return messageBlockHtml(sender, displayMessageHtml(message, isHtml), timestamp, false);
}

QString basicOutgoingHtml(const QString &message,
                          const QString &timestamp,
                          const QString &status,
                          bool isHtml,
                          const QColor &accentColor = QColor())
{
    return messageBlockHtml(QStringLiteral("Me"),
                            displayMessageHtml(message, isHtml),
                            timestamp,
                            true,
                            receiptIconHtml(status),
                            accentColor);
}

QString systemBubbleHtml(const QString &message)
{
    return QStringLiteral(
               "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
               "<tr><td align=\"center\" style=\"padding:5px 12px;\">"
               "<span style=\"color:#64748b; font-size:8pt;\"><i>%1 %2</i></span>"
               "</td></tr>"
               "</table>")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")),
             message.toHtmlEscaped());
}

void setPlainNativeWindowTitle(QWidget *window, const QString &title)
{
    window->setWindowTitle(title);
#ifdef Q_OS_WIN
    SetWindowTextW(reinterpret_cast<HWND>(window->winId()), reinterpret_cast<LPCWSTR>(title.utf16()));
#endif
}

QString imageMessageHtml(const QString &sender, const QString &fileName, const QString &filePath, bool outgoing)
{
    const QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QUrl saveUrl(QStringLiteral("lanchat-save-image:"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), filePath);
    query.addQueryItem(QStringLiteral("name"), QFileInfo(filePath).fileName());
    saveUrl.setQuery(query);

    const QString imageHtml = QStringLiteral(
                                  "<table cellspacing=\"0\" cellpadding=\"6\" bgcolor=\"%1\">"
                                  "<tr><td><img src=\"%2\" width=\"170\"></td></tr>"
                                  "<tr><td><span style=\"color:#334155; font-size:8pt;\">%3</span></td></tr>"
                                  "<tr><td><a href=\"%2\">Open</a> <span style=\"color:#94a3b8\">|</span> <a href=\"%4\">Save As...</a></td></tr>"
                                  "</table>")
                                  .arg(outgoing ? QStringLiteral("#e2e8f0") : QStringLiteral("#f1f5f9"),
                                       fileUrl,
                                       fileName.toHtmlEscaped(),
                                       saveUrl.toString());

    return messageBlockHtml(sender, imageHtml, chatTimestamp(), outgoing);
}

}

ChatWindow::ChatWindow(const QString &peerId, QWidget *parent)
    : QMainWindow(parent)
    , m_peerId(peerId)
{
    buildUi();
    connectSignals();
    loadHistory();
}

QString ChatWindow::peerId() const
{
    return m_peerId;
}

void ChatWindow::setPeerDetails(const QString &name, const QString &status, const QByteArray &avatarData, const QDateTime &lastSeen, const QColor &themeColor)
{
    m_peerName = name;
    m_avatarFrameColor = themeColor.isValid() ? themeColor : QColor(QStringLiteral("#7dd3fc"));
    setPlainNativeWindowTitle(this, tr("Chat with %1").arg(name));
    m_nameLabel->setText(name);
    m_statusLabel->setText(status);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 650; font-size: 11px;").arg(statusColor(status)));
    m_lastSeenLabel->setText(lastSeenText(lastSeen, status));
    m_avatarLabel->setPixmap(avatarPixmap(avatarData));
}

void ChatWindow::setLocalAccentColor(const QColor &color)
{
    m_localAccentColor = color.isValid() ? color : QColor(QStringLiteral("#1d9bf0"));
    updateNewMessageMarkerStyle();
}

void ChatWindow::appendIncomingMessage(const QString &sender, const QString &message, const QString &messageId, bool isHtml)
{
    noteNewMessageMarker();

    const QString timestamp = chatTimestamp();
    appendHtmlLine(basicIncomingHtml(sender, message, timestamp, isHtml));
    if (!messageId.isEmpty()) {
        MessageRecord record;
        record.sender = sender;
        record.message = message;
        record.timestamp = timestamp;
        record.outgoing = false;
        record.isHtml = isHtml;
        m_messageRecords.insert(messageId, record);
        m_messageLineIndexes.insert(messageId, m_transcriptLines.size() - 1);
    }
}

void ChatWindow::appendOutgoingMessage(const QString &message, const QString &messageId, bool isHtml)
{
    const QString timestamp = chatTimestamp();
    const QString html = basicOutgoingHtml(message, timestamp, QStringLiteral("Sent"), isHtml, m_localAccentColor);
    appendHtmlLine(html);
    if (!messageId.isEmpty()) {
        m_outgoingLineIndexes.insert(messageId, m_transcriptLines.size() - 1);
        m_messageLineIndexes.insert(messageId, m_transcriptLines.size() - 1);
        m_outgoingMessages.insert(messageId, message);
        m_outgoingTimestamps.insert(messageId, timestamp);
        m_outgoingHtmlModes.insert(messageId, isHtml);
        MessageRecord record;
        record.sender = QStringLiteral("Me");
        record.message = message;
        record.timestamp = timestamp;
        record.status = QStringLiteral("Sent");
        record.outgoing = true;
        record.isHtml = isHtml;
        m_messageRecords.insert(messageId, record);
    }
    m_messageEdit->clear();
    m_messageEdit->setFocus();

    clearNewMessageMarker();
}

void ChatWindow::updateMessageStatus(const QString &messageId, const QString &status)
{
    const auto lineIt = m_outgoingLineIndexes.constFind(messageId);
    const auto messageIt = m_outgoingMessages.constFind(messageId);
    if (lineIt == m_outgoingLineIndexes.constEnd() || messageIt == m_outgoingMessages.constEnd()) {
        return;
    }

    const int lineIndex = *lineIt;
    if (lineIndex < 0 || lineIndex >= m_transcriptLines.size()) {
        return;
    }

    const QString timestamp = m_outgoingTimestamps.value(messageId, chatTimestamp());
    auto recordIt = m_messageRecords.find(messageId);
    if (recordIt != m_messageRecords.end()) {
        recordIt->status = status;
        m_transcriptLines[lineIndex] = basicOutgoingHtml(recordIt->message,
                                                         recordIt->timestamp,
                                                         status,
                                                         recordIt->isHtml,
                                                         m_localAccentColor);
    } else {
        m_transcriptLines[lineIndex] = basicOutgoingHtml(*messageIt, timestamp, status, m_outgoingHtmlModes.value(messageId, false), m_localAccentColor);
    }
    renderTranscript();
    saveHistory();
}

void ChatWindow::appendSystemMessage(const QString &message)
{
    appendHtmlLine(systemBubbleHtml(message));
}

void ChatWindow::setPeerTyping(bool typing)
{
    m_typingLabel->setText(typing ? tr("%1 is typing...").arg(m_peerName.isEmpty() ? tr("Contact") : m_peerName) : QString());
}

void ChatWindow::appendIncomingFile(const QString &sender, const QString &fileName, const QByteArray &data)
{
    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir dir(basePath.isEmpty() ? QDir::homePath() : basePath);
    dir.mkpath(QStringLiteral("Blinq Messenger Received"));
    dir.cd(QStringLiteral("Blinq Messenger Received"));

    QString targetName = QFileInfo(fileName).fileName();
    if (targetName.isEmpty()) {
        targetName = QStringLiteral("received-file");
    }

    QString targetPath = dir.filePath(targetName);
    QFileInfo targetInfo(targetPath);
    int suffix = 1;
    while (QFileInfo::exists(targetPath)) {
        const QString stem = targetInfo.completeBaseName();
        const QString ext = targetInfo.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(targetInfo.suffix());
        targetPath = dir.filePath(QStringLiteral("%1 (%2)%3").arg(stem).arg(suffix++).arg(ext));
    }

    QFile file(targetPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();

        noteNewMessageMarker();

        if (isDisplayableImage(fileName, data)) {
            appendHtmlLine(imageMessageHtml(sender, QFileInfo(targetPath).fileName(), targetPath, false));
            return;
        }
        appendSystemMessage(tr("%1 sent %2. Saved to %3").arg(sender, QFileInfo(targetPath).fileName(), targetPath));
    } else {
        appendSystemMessage(tr("%1 sent %2, but it could not be saved.").arg(sender, fileName));
    }
}

void ChatWindow::appendFileTransferStarted(const QString &sender, const QString &fileName)
{
    appendSystemMessage(tr("%1 is sending a file: %2").arg(sender, fileName));
}

void ChatWindow::appendOutgoingFile(const QString &fileName, const QString &filePath, bool isImage)
{
    if (isImage && !filePath.isEmpty()) {
        appendHtmlLine(imageMessageHtml(tr("Me"), fileName, filePath, true));
        return;
    }
    appendSystemMessage(tr("Sent file: %1").arg(fileName));
}

void ChatWindow::setImageAttachmentsOnly(bool enabled)
{
    m_imageAttachmentsOnly = enabled;
    if (m_fileButton) {
        m_fileButton->setToolTip(enabled ? tr("Send image") : tr("Send file"));
    }
}

bool ChatWindow::shouldNotifyForIncoming() const
{
    return !isVisible() || isMinimized() || !isActiveWindow();
}

void ChatWindow::closeEvent(QCloseEvent *event)
{
    const auto dialogs = findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly);
    for (QDialog *dialog : dialogs) {
        dialog->close();
    }
    emit chatClosed();
    QMainWindow::closeEvent(event);
}

void ChatWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        if (isTranscriptAtBottom()) {
            clearNewMessageMarker();
        } else {
            updateNewMessageMarker();
        }
        emit becameActive();
    }
}

void ChatWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ChatWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            emit sendFileRequested(m_peerId, url.toLocalFile());
        }
    }
    event->acceptProposedAction();
}

bool ChatWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_messageEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Paste)) {
            const QClipboard *clipboard = QApplication::clipboard();
            const QMimeData *mimeData = clipboard->mimeData();
            if (mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                if (!image.isNull()) {
                    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                    if (tempPath.isEmpty()) tempPath = QDir::tempPath();
                    QDir dir(tempPath);
                    dir.mkpath(QStringLiteral("BlinqMessenger_Pasted"));
                    dir.cd(QStringLiteral("BlinqMessenger_Pasted"));
                    
                    QString filePath = dir.filePath(QStringLiteral("pasted_image_%1.png").arg(QDateTime::currentMSecsSinceEpoch()));
                    if (image.save(filePath, "PNG")) {
                        emit sendFileRequested(m_peerId, filePath);
                        return true;
                    }
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ChatWindow::setHistoryEnabled(bool enabled)
{
    m_historyEnabled = enabled;
}

void ChatWindow::setQueuedMessageCount(int count)
{
    if (!m_queueBanner || !m_queueLabel) {
        return;
    }

    m_queueBanner->setVisible(count > 0);
    m_queueLabel->setText(tr("%n queued offline message(s)", nullptr, count));
}

void ChatWindow::clearHistory()
{
    m_transcriptLines.clear();
    m_outgoingLineIndexes.clear();
    m_outgoingMessages.clear();
    m_outgoingTimestamps.clear();
    m_outgoingHtmlModes.clear();
    m_messageLineIndexes.clear();
    m_messageRecords.clear();
    m_transcript->clear();
    QFile::remove(historyPath());
}

void ChatWindow::buildUi()
{
    setMinimumSize(560, 440);
    resize(minimumSize());

    setAcceptDrops(true);

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *header = new QWidget(root);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);

    m_avatarLabel = new QLabel(header);
    m_avatarLabel->setFixedSize(48, 48);
    m_avatarLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    headerLayout->addWidget(m_avatarLabel);

    auto *identity = new QWidget(header);
    auto *identityLayout = new QVBoxLayout(identity);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(2);
    m_nameLabel = new QLabel(tr("Contact"), identity);
    m_nameLabel->setStyleSheet(QStringLiteral("font-weight: 700; color: #111827; font-size: 14px;"));
    m_statusLabel = new QLabel(tr("Offline"), identity);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #64748b; font-weight: 650; font-size: 11px;"));
    m_lastSeenLabel = new QLabel(tr("Last seen unknown"), identity);
    m_lastSeenLabel->setStyleSheet(QStringLiteral("color: #64748b; font-size: 10px;"));
    identityLayout->addWidget(m_nameLabel);
    identityLayout->addWidget(m_statusLabel);
    identityLayout->addWidget(m_lastSeenLabel);
    headerLayout->addWidget(identity, 1);

    layout->addWidget(header);

    m_transcript = new QTextBrowser(root);
    m_transcript->setReadOnly(true);
    m_transcript->setOpenLinks(false);
    m_transcript->setOpenExternalLinks(false);
    layout->addWidget(m_transcript, 1);

    auto *scrollUpButton = new QPushButton(m_transcript);
    scrollUpButton->setObjectName(QStringLiteral("ScrollUpButton"));
    scrollUpButton->setCursor(Qt::PointingHandCursor);
    scrollUpButton->hide();
    updateNewMessageMarkerStyle();
    auto *overlayLayout = new QVBoxLayout(m_transcript);
    overlayLayout->setContentsMargins(0, 16, 0, 0);
    overlayLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    overlayLayout->addWidget(scrollUpButton);

/*
    m_searchPrevButton = new QPushButton(tr("↑"), m_searchBanner);
    m_searchPrevButton->setFixedSize(28, 28);
    m_searchNextButton = new QPushButton(tr("↓"), m_searchBanner);
    m_searchNextButton->setFixedSize(28, 28);
    m_searchCloseButton = new QPushButton(tr("✕"), m_searchBanner);
    m_searchCloseButton->setFixedSize(28, 28);
    searchLayout->addWidget(m_searchInput, 1);
    searchLayout->addWidget(m_searchPrevButton);
    searchLayout->addWidget(m_searchNextButton);
    searchLayout->addWidget(m_searchCloseButton);
    layout->addWidget(m_searchBanner);
    m_searchBanner->hide();

*/
    m_queueBanner = new QWidget(root);
    m_queueBanner->setStyleSheet(QStringLiteral(
        "QWidget { background:#fff7ed; border:1px solid #fed7aa; border-radius:8px; }"
        "QLabel { color:#7c2d12; background:transparent; border:none; }"
        "QPushButton#QueueBannerButton { background:#fff7ed; color:#0f172a; border:1px solid #fed7aa; border-radius:8px; padding:3px 14px 5px 14px; font-weight:650; }"
        "QPushButton#QueueBannerButton:hover { background:#ffedd5; border-color:#fdba74; }"
        "QPushButton#QueueBannerButton:pressed { background:#fed7aa; }"));
    auto *queueLayout = new QHBoxLayout(m_queueBanner);
    queueLayout->setContentsMargins(10, 7, 8, 7);
    queueLayout->setSpacing(8);
    m_queueLabel = new QLabel(m_queueBanner);
    queueLayout->addWidget(m_queueLabel, 1);
    m_viewQueueButton = new QPushButton(tr("View"), m_queueBanner);
    m_viewQueueButton->setObjectName(QStringLiteral("QueueBannerButton"));
    m_viewQueueButton->setMinimumSize(78, 32);
    m_retryQueueButton = new QPushButton(tr("Retry"), m_queueBanner);
    m_retryQueueButton->setObjectName(QStringLiteral("QueueBannerButton"));
    m_retryQueueButton->setMinimumSize(78, 32);
    m_clearQueueButton = new QPushButton(tr("Cancel"), m_queueBanner);
    m_clearQueueButton->setObjectName(QStringLiteral("QueueBannerButton"));
    m_clearQueueButton->setMinimumSize(86, 32);
    queueLayout->addWidget(m_viewQueueButton);
    queueLayout->addWidget(m_retryQueueButton);
    queueLayout->addWidget(m_clearQueueButton);
    layout->addWidget(m_queueBanner);
    m_queueBanner->hide();

    m_transfersContainer = new QWidget(root);
    m_transfersLayout = new QVBoxLayout(m_transfersContainer);
    m_transfersLayout->setContentsMargins(0, 0, 0, 0);
    m_transfersLayout->setSpacing(4);
    layout->addWidget(m_transfersContainer);
    m_transfersContainer->hide();

    m_typingLabel = new QLabel(root);
    m_typingLabel->setFixedHeight(18);
    m_typingLabel->setStyleSheet(QStringLiteral("color: #64748b; font-style: italic; padding-left: 4px;"));
    layout->addWidget(m_typingLabel);

    auto *composer = new QWidget(root);
    composer->setObjectName(QStringLiteral("ChatComposerPanel"));
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(8, 6, 8, 8);
    composerLayout->setSpacing(5);

    auto *toolRow = new QWidget(composer);
    toolRow->setObjectName(QStringLiteral("ChatComposerToolRow"));
    auto *toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(4);

    m_emojiButton = new QPushButton(composer);
    m_emojiButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    m_emojiButton->setIcon(QIcon(QStringLiteral(":/icons/assets/emoji.png")));
    m_emojiButton->setIconSize(QSize(18, 18));
    m_emojiButton->setFixedSize(28, 24);
    m_emojiButton->setToolTip(tr("Insert emoji"));
    toolLayout->addWidget(m_emojiButton);

    m_fileButton = new QPushButton(composer);
    m_fileButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    m_fileButton->setIcon(QIcon(QStringLiteral(":/icons/assets/attachment.png")));
    m_fileButton->setIconSize(QSize(18, 18));
    m_fileButton->setFixedSize(28, 24);
    m_fileButton->setToolTip(tr("Send file"));
    toolLayout->addWidget(m_fileButton);

    m_htmlButton = new QPushButton(composer);
    m_htmlButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    m_htmlButton->setIcon(QIcon(QStringLiteral(":/icons/assets/html.png")));
    m_htmlButton->setIconSize(QSize(18, 18));
    m_htmlButton->setFixedSize(28, 24);
    m_htmlButton->setToolTip(tr("Rich message editor"));
    toolLayout->addWidget(m_htmlButton);

    m_activityButton = new QPushButton(composer);
    m_activityButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    m_activityButton->setIcon(QIcon(QStringLiteral(":/icons/assets/drawing.png")));
    m_activityButton->setIconSize(QSize(18, 18));
    m_activityButton->setFixedSize(28, 24);
    m_activityButton->setToolTip(tr("Drawing Pad"));
    connect(m_activityButton, &QPushButton::clicked, this, [this] {
        emit activityRequested(m_peerId, QStringLiteral("drawing"));
    });
    toolLayout->addWidget(m_activityButton);

    m_buzzButton = new QPushButton(composer);
    m_buzzButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    m_buzzButton->setIcon(QIcon(QStringLiteral(":/icons/assets/whistle.png")));
    m_buzzButton->setIconSize(QSize(18, 18));
    m_buzzButton->setFixedSize(28, 24);
    m_buzzButton->setToolTip(tr("Whistle"));
    toolLayout->addWidget(m_buzzButton);

    auto *searchButton = new QPushButton(composer);
    searchButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    searchButton->setIcon(QIcon(QStringLiteral(":/icons/assets/scan.png")));
    searchButton->setIconSize(QSize(18, 18));
    searchButton->setFixedSize(28, 24);
    searchButton->setToolTip(tr("Find in conversation"));
    connect(searchButton, &QPushButton::clicked, this, &ChatWindow::showSearchBar);
    toolLayout->addWidget(searchButton);

    toolLayout->addStretch(1);
    composerLayout->addWidget(toolRow);

    auto *separator = new QWidget(composer);
    separator->setObjectName(QStringLiteral("ChatComposerSeparator"));
    separator->setFixedHeight(1);
    composerLayout->addWidget(separator);

    auto *inputRow = new QWidget(composer);
    inputRow->setObjectName(QStringLiteral("ChatComposerInputRow"));
    auto *inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(8);

    m_messageEdit = new QLineEdit(composer);
    m_messageEdit->setObjectName(QStringLiteral("ChatMessageEdit"));
    m_messageEdit->setFixedHeight(40);
    m_messageEdit->installEventFilter(this);
    m_messageEdit->setPlaceholderText(tr("Message"));
    inputLayout->addWidget(m_messageEdit, 1);

    m_sendButton = new QPushButton(composer);
    m_sendButton->setObjectName(QStringLiteral("ChatSendButton"));
    m_sendButton->setIcon(QIcon(QStringLiteral(":/icons/assets/send.png")));
    m_sendButton->setIconSize(QSize(22, 22));
    m_sendButton->setFixedSize(34, 34);
    m_sendButton->setToolTip(tr("Send message"));
    inputLayout->addWidget(m_sendButton);
    composerLayout->addWidget(inputRow);
    layout->addWidget(composer);

    setCentralWidget(root);

}

void ChatWindow::connectSignals()
{
    m_typingIdleTimer = new QTimer(this);
    m_typingIdleTimer->setSingleShot(true);
    m_typingIdleTimer->setInterval(2500);
    connect(m_typingIdleTimer, &QTimer::timeout, this, [this] {
        setLocalTyping(false);
    });

    connect(m_sendButton, &QPushButton::clicked, this, [this] {
        const QString message = m_messageEdit->text().trimmed();
        if (!message.isEmpty()) {
            setLocalTyping(false);
            emit sendMessageRequested(m_peerId, messageWithEmoticons(message), false);
        }
    });
    connect(m_messageEdit, &QLineEdit::returnPressed, m_sendButton, &QPushButton::click);
    connect(m_messageEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty()) {
            setLocalTyping(false);
            m_typingIdleTimer->stop();
            return;
        }

        setLocalTyping(true);
        m_typingIdleTimer->start();
    });
    connect(m_emojiButton, &QPushButton::clicked, this, &ChatWindow::showEmojiDialog);
    connect(m_htmlButton, &QPushButton::clicked, this, &ChatWindow::showRichMessageDialog);

    connect(m_buzzButton, &QPushButton::clicked, this, [this] {
        emit buzzRequested(m_peerId);
    });
    
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, &ChatWindow::showSearchBar);

    connect(m_retryQueueButton, &QPushButton::clicked, this, [this] {
        emit retryQueuedMessagesRequested(m_peerId);
    });
    connect(m_viewQueueButton, &QPushButton::clicked, this, [this] {
        emit viewQueuedMessagesRequested(m_peerId);
    });
    connect(m_clearQueueButton, &QPushButton::clicked, this, [this] {
        emit clearQueuedMessagesRequested(m_peerId);
    });

    if (auto *scrollUpButton = m_transcript->findChild<QPushButton*>(QStringLiteral("ScrollUpButton"))) {
        connect(scrollUpButton, &QPushButton::clicked, this, [this, scrollUpButton] {
            m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
            clearNewMessageMarker();
        });
        connect(m_transcript->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, scrollUpButton](int value) {
            if (value >= m_transcript->verticalScrollBar()->maximum() - 5) {
                clearNewMessageMarker();
            } else if (scrollUpButton->isVisible()) {
                scrollUpButton->raise();
            }
        });
    }

    connect(m_transcript, &QTextBrowser::anchorClicked, this, &ChatWindow::handleTranscriptLink);

    connect(m_fileButton, &QPushButton::clicked, this, [this] {
        const QString filter = m_imageAttachmentsOnly
                                   ? tr("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)")
                                   : QString();
        const QString filePath = QFileDialog::getOpenFileName(this,
                                                              m_imageAttachmentsOnly ? tr("Send Image") : tr("Send File"),
                                                              QString(),
                                                              filter);
        if (!filePath.isEmpty()) {
            emit sendFileRequested(m_peerId, filePath);
        }
    });
}

void ChatWindow::handleTranscriptLink(const QUrl &url)
{
    if (url.scheme() == QStringLiteral("lanchat-save-image")) {
        const QUrlQuery query(url);
        const QString sourcePath = query.queryItemValue(QStringLiteral("path"));
        if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
            appendSystemMessage(tr("That image is no longer available."));
            return;
        }

        const QString suggestedName = query.queryItemValue(QStringLiteral("name")).isEmpty()
                                          ? QFileInfo(sourcePath).fileName()
                                          : query.queryItemValue(QStringLiteral("name"));
        const QString targetPath = QFileDialog::getSaveFileName(this, tr("Save Image As"), suggestedName);
        if (!targetPath.isEmpty() && !QFile::copy(sourcePath, targetPath)) {
            appendSystemMessage(tr("Could not save image to %1.").arg(targetPath));
        }
        return;
    }

    QDesktopServices::openUrl(url);
}

void ChatWindow::showSearchBar()
{
    if (auto *existing = findChild<QDialog*>(QStringLiteral("FindDialog"))) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        if (auto *input = existing->findChild<QLineEdit*>(QStringLiteral("FindInput"))) {
            input->setFocus();
            input->selectAll();
        }
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("FindDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Search Messages"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setFixedSize(520, 190);
    dialog->setStyleSheet(qApp->styleSheet()
                          + QStringLiteral(
                                "QWidget#FindDialogSearchRow, QWidget#FindDialogControlsRow, QWidget#FindDialogDirectionRow, QWidget#FindDialogButtonRow { background: transparent; border: none; }"
                                "QDialog#FindDialog QLabel { color:#0f172a; }"
                                "QDialog#FindDialog QCheckBox, QDialog#FindDialog QRadioButton { background: transparent; color:#0f172a; spacing:6px; }"
                                "QDialog#FindDialog QLineEdit { background:#ffffff; border:1px solid #9fb7d6; border-radius:6px; padding:6px 8px; selection-background-color:#1d9bf0; }"));

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(14, 12, 14, 20);
    layout->setSpacing(8);

    auto *queryRow = new QWidget(dialog);
    queryRow->setObjectName(QStringLiteral("FindDialogSearchRow"));
    auto *queryLayout = new QHBoxLayout(queryRow);
    queryLayout->setContentsMargins(0, 0, 0, 0);
    queryLayout->setSpacing(8);

    auto *label = new QLabel(tr("Search for:"), queryRow);
    auto *input = new QLineEdit(dialog);
    input->setObjectName(QStringLiteral("FindInput"));
    auto *findButton = new QPushButton(tr("Find Next"), dialog);
    findButton->setDefault(true);
    findButton->setEnabled(false);
    queryLayout->addWidget(label);
    queryLayout->addWidget(input, 1);
    layout->addWidget(queryRow);

    auto *controlsRow = new QWidget(dialog);
    controlsRow->setObjectName(QStringLiteral("FindDialogControlsRow"));
    auto *controlsLayout = new QHBoxLayout(controlsRow);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(14);

    auto *directionRow = new QWidget(controlsRow);
    directionRow->setObjectName(QStringLiteral("FindDialogDirectionRow"));
    auto *directionLayout = new QHBoxLayout(directionRow);
    directionLayout->setContentsMargins(0, 0, 0, 0);
    directionLayout->setSpacing(10);
    auto *directionLabel = new QLabel(tr("Direction:"), directionRow);
    auto *upRadio = new QRadioButton(tr("Up"), directionRow);
    auto *downRadio = new QRadioButton(tr("Down"), directionRow);
    downRadio->setChecked(true);
    directionLayout->addWidget(directionLabel);
    directionLayout->addWidget(upRadio);
    directionLayout->addWidget(downRadio);
    controlsLayout->addWidget(directionRow);

    auto *matchCaseCheck = new QCheckBox(tr("Match case"), controlsRow);
    auto *wrapCheck = new QCheckBox(tr("Wrap around"), controlsRow);
    controlsLayout->addWidget(matchCaseCheck);
    controlsLayout->addWidget(wrapCheck);
    controlsLayout->addStretch(1);
    layout->addWidget(controlsRow);
    layout->addStretch(1);

    auto *buttonRow = new QWidget(dialog);
    buttonRow->setObjectName(QStringLiteral("FindDialogButtonRow"));
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch(1);
    auto *cancelButton = new QPushButton(tr("Cancel"), buttonRow);
    buttonLayout->addWidget(findButton);
    buttonLayout->addWidget(cancelButton);
    layout->addWidget(buttonRow);

    const auto runSearch = [this, input, upRadio, matchCaseCheck, wrapCheck] {
        const QString query = input->text();
        if (query.isEmpty()) {
            return;
        }

        const bool backward = upRadio->isChecked();
        QTextDocument::FindFlags flags;
        if (backward) {
            flags |= QTextDocument::FindBackward;
        }
        if (matchCaseCheck->isChecked()) {
            flags |= QTextDocument::FindCaseSensitively;
        }

        const QTextCursor previousCursor = m_transcript->textCursor();
        if (m_transcript->find(query, flags)) {
            return;
        }

        if (wrapCheck->isChecked()) {
            QTextCursor cursor = m_transcript->textCursor();
            cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
            m_transcript->setTextCursor(cursor);
            if (m_transcript->find(query, flags)) {
                return;
            }
        }

        m_transcript->setTextCursor(previousCursor);
        QApplication::beep();
    };

    connect(input, &QLineEdit::textChanged, findButton, [findButton](const QString &text) {
        findButton->setEnabled(!text.trimmed().isEmpty());
    });
    connect(input, &QLineEdit::returnPressed, findButton, &QPushButton::click);
    connect(findButton, &QPushButton::clicked, dialog, runSearch);
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::close);

    dialog->show();
    input->setFocus();
}

void ChatWindow::showRichMessageDialog()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Rich Message"));
    dialog->resize(560, 360);

    auto *layout = new QVBoxLayout(dialog);
    auto *toolbar = new QWidget(dialog);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto *boldButton = new QToolButton(toolbar);
    boldButton->setText(tr("B"));
    boldButton->setCheckable(true);
    boldButton->setToolTip(tr("Bold"));
    QFont boldFont = boldButton->font();
    boldFont.setBold(true);
    boldButton->setFont(boldFont);
    toolbarLayout->addWidget(boldButton);

    auto *italicButton = new QToolButton(toolbar);
    italicButton->setText(tr("I"));
    italicButton->setCheckable(true);
    italicButton->setToolTip(tr("Italic"));
    QFont italicFont = italicButton->font();
    italicFont.setItalic(true);
    italicButton->setFont(italicFont);
    toolbarLayout->addWidget(italicButton);

    auto *underlineButton = new QToolButton(toolbar);
    underlineButton->setText(tr("U"));
    underlineButton->setCheckable(true);
    underlineButton->setToolTip(tr("Underline"));
    QFont underlineFont = underlineButton->font();
    underlineFont.setUnderline(true);
    underlineButton->setFont(underlineFont);
    toolbarLayout->addWidget(underlineButton);

    auto *fontCombo = new QFontComboBox(toolbar);
    fontCombo->setToolTip(tr("Font"));
    fontCombo->setEditable(false);
    toolbarLayout->addWidget(fontCombo, 1);

    auto *sizeCombo = new QComboBox(toolbar);
    sizeCombo->setToolTip(tr("Size"));
    const QList<int> sizes = {9, 10, 11, 12, 14, 16, 18, 22, 26, 32};
    for (int size : sizes) {
        sizeCombo->addItem(QString::number(size), size);
    }
    sizeCombo->setCurrentText(QStringLiteral("12"));
    toolbarLayout->addWidget(sizeCombo);

    auto *colorButton = new QToolButton(toolbar);
    colorButton->setText(tr("Color"));
    colorButton->setToolTip(tr("Text color"));
    toolbarLayout->addWidget(colorButton);
    layout->addWidget(toolbar);

    auto *editor = new QTextEdit(dialog);
    editor->setAcceptRichText(true);
    editor->setPlaceholderText(tr("Write your message..."));
    layout->addWidget(editor, 1);

    auto *buttons = new QWidget(dialog);
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch();

    auto *cancelButton = new QPushButton(tr("Cancel"), buttons);
    auto *sendButton = new QPushButton(tr("Send"), buttons);
    sendButton->setObjectName(QStringLiteral("DialogPrimaryButton"));
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(sendButton);
    layout->addWidget(buttons);

    const auto mergeFormat = [editor](const QTextCharFormat &format) {
        QTextCursor cursor = editor->textCursor();
        if (!cursor.hasSelection()) {
            cursor.select(QTextCursor::WordUnderCursor);
        }
        cursor.mergeCharFormat(format);
        editor->mergeCurrentCharFormat(format);
        editor->setTextCursor(cursor);
        editor->setFocus();
    };

    connect(boldButton, &QToolButton::toggled, editor, [mergeFormat](bool checked) {
        QTextCharFormat format;
        format.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        mergeFormat(format);
    });
    connect(italicButton, &QToolButton::toggled, editor, [mergeFormat](bool checked) {
        QTextCharFormat format;
        format.setFontItalic(checked);
        mergeFormat(format);
    });
    connect(underlineButton, &QToolButton::toggled, editor, [mergeFormat](bool checked) {
        QTextCharFormat format;
        format.setFontUnderline(checked);
        mergeFormat(format);
    });
    connect(fontCombo, &QFontComboBox::currentFontChanged, editor, [mergeFormat](const QFont &font) {
        QTextCharFormat format;
        format.setFontFamilies({font.family()});
        mergeFormat(format);
    });
    connect(sizeCombo, &QComboBox::currentIndexChanged, editor, [mergeFormat, sizeCombo] {
        QTextCharFormat format;
        format.setFontPointSize(sizeCombo->currentData().toDouble());
        mergeFormat(format);
    });
    connect(colorButton, &QToolButton::clicked, dialog, [this, dialog, mergeFormat] {
        const QColor color = QColorDialog::getColor(palette().color(QPalette::Text), dialog, tr("Text Color"));
        if (!color.isValid()) {
            return;
        }
        QTextCharFormat format;
        format.setForeground(color);
        mergeFormat(format);
    });
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(sendButton, &QPushButton::clicked, dialog, [this, dialog, editor] {
        if (editor->toPlainText().trimmed().isEmpty()) {
            return;
        }
        setLocalTyping(false);
        const QString html = editor->toHtml();
        emit sendMessageRequested(m_peerId, html, true);
        dialog->accept();
    });

    dialog->setStyleSheet(qApp->styleSheet()
                          + QStringLiteral(
                                "QToolButton { min-width:32px; min-height:28px; border:1px solid #cbd5e1; border-radius:6px; background:#ffffff; }"
                                "QToolButton:hover, QToolButton:checked { background:#dbeafe; border-color:#1d9bf0; }"));
    dialog->show();
    editor->setFocus();
}

void ChatWindow::showEmojiDialog()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Emoji"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    dialog->setFixedSize(380, 260);
    dialog->setStyleSheet(qApp->styleSheet()
                          + QStringLiteral(
                                "QToolButton { background: transparent; color: inherit; border: 1px solid transparent; border-radius: 7px; font-size: 16px; padding: 2px; }"
                                "QToolButton:hover { background: #d7e8ff; border-color: #98bbe5; }"));
    auto *layout = new QVBoxLayout(dialog);
    auto *tabs = new QTabWidget(dialog);
    layout->addWidget(tabs);

    const QList<QPair<QString, QStringList>> emojiGroups = {
        {tr("Faces"), {QStringLiteral("😀"), QStringLiteral("😁"), QStringLiteral("😂"), QStringLiteral("🤣"), QStringLiteral("😊"), QStringLiteral("🙂"), QStringLiteral("😉"), QStringLiteral("😍"), QStringLiteral("😘"), QStringLiteral("😎"), QStringLiteral("🤔"), QStringLiteral("😮"), QStringLiteral("😢"), QStringLiteral("😡"), QStringLiteral("🥳"), QStringLiteral("😴"), QStringLiteral("😇"), QStringLiteral("🙃"), QStringLiteral("😋"), QStringLiteral("😜"), QStringLiteral("🤓"), QStringLiteral("😬"), QStringLiteral("😭"), QStringLiteral("😤")}},
        {tr("Hands"), {QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("👏"), QStringLiteral("🙌"), QStringLiteral("🙏"), QStringLiteral("🤝"), QStringLiteral("👋"), QStringLiteral("👌"), QStringLiteral("✌️"), QStringLiteral("🤘"), QStringLiteral("💪"), QStringLiteral("✋"), QStringLiteral("🤚"), QStringLiteral("🖐️"), QStringLiteral("👊"), QStringLiteral("🤞"), QStringLiteral("☝️"), QStringLiteral("👇")}},
        {tr("Hearts"), {QStringLiteral("❤️"), QStringLiteral("🧡"), QStringLiteral("💛"), QStringLiteral("💚"), QStringLiteral("💙"), QStringLiteral("💜"), QStringLiteral("🖤"), QStringLiteral("🤍"), QStringLiteral("💔"), QStringLiteral("💕"), QStringLiteral("💞"), QStringLiteral("💓"), QStringLiteral("💗"), QStringLiteral("💖"), QStringLiteral("💘"), QStringLiteral("💯"), QStringLiteral("✨"), QStringLiteral("⭐")}},
        {tr("Food"), {QStringLiteral("☕"), QStringLiteral("🍕"), QStringLiteral("🍔"), QStringLiteral("🍟"), QStringLiteral("🌮"), QStringLiteral("🌯"), QStringLiteral("🍣"), QStringLiteral("🍜"), QStringLiteral("🍩"), QStringLiteral("🍪"), QStringLiteral("🍎"), QStringLiteral("🍓"), QStringLiteral("🍉"), QStringLiteral("🍌"), QStringLiteral("🍺"), QStringLiteral("🥤"), QStringLiteral("🍰"), QStringLiteral("🍫")}},
        {tr("Travel"), {QStringLiteral("🚗"), QStringLiteral("🚕"), QStringLiteral("🚌"), QStringLiteral("🚆"), QStringLiteral("✈️"), QStringLiteral("🚀"), QStringLiteral("🚲"), QStringLiteral("🏠"), QStringLiteral("🏢"), QStringLiteral("🏖️"), QStringLiteral("🌎"), QStringLiteral("🗺️"), QStringLiteral("⛱️"), QStringLiteral("⛰️"), QStringLiteral("🌙"), QStringLiteral("☀️"), QStringLiteral("🌧️"), QStringLiteral("❄️")}},
        {tr("Objects"), {QStringLiteral("🔥"), QStringLiteral("🎉"), QStringLiteral("✅"), QStringLiteral("❌"), QStringLiteral("⚠️"), QStringLiteral("💡"), QStringLiteral("📎"), QStringLiteral("📁"), QStringLiteral("💻"), QStringLiteral("📱"), QStringLiteral("🔒"), QStringLiteral("🔔"), QStringLiteral("📌"), QStringLiteral("📝"), QStringLiteral("📷"), QStringLiteral("🎧"), QStringLiteral("🎮"), QStringLiteral("🛠️")}}
    };

    for (const auto &group : emojiGroups) {
        auto *page = new QWidget(tabs);
        auto *grid = new QGridLayout(page);
        grid->setSpacing(6);
        for (int i = 0; i < group.second.size(); ++i) {
            auto *button = new QToolButton(page);
            button->setText(group.second.at(i));
            button->setFixedSize(34, 28);
            connect(button, &QToolButton::clicked, this, [this, emoji = group.second.at(i)] {
                m_messageEdit->insert(emoji);
                m_messageEdit->setFocus();
            });
            grid->addWidget(button, i / 6, i % 6);
        }
        tabs->addTab(page, group.first);
    }
    dialog->move(m_emojiButton->mapToGlobal(QPoint(0, -dialog->height() - 8)));
    dialog->show();
}

void ChatWindow::loadHistory()
{
    QFile file(historyPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    while (!file.atEnd()) {
        appendHtmlLine(QString::fromUtf8(file.readLine()).trimmed(), false);
    }
}

void ChatWindow::saveHistoryLine(const QString &html)
{
    if (!m_historyEnabled) {
        return;
    }

    QFile file(historyPath());
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QString line = html;
        line.replace(QLatin1Char('\n'), QString());
        file.write(line.toUtf8());
        file.write("\n");
    }
}

void ChatWindow::saveHistory()
{
    if (!m_historyEnabled) {
        return;
    }

    QFile file(historyPath());
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    for (QString line : std::as_const(m_transcriptLines)) {
        line.replace(QLatin1Char('\n'), QString());
        file.write(line.toUtf8());
        file.write("\n");
    }
}

void ChatWindow::appendHtmlLine(const QString &html, bool persist)
{
    QScrollBar *vbar = m_transcript->verticalScrollBar();
    const bool wasAtBottom = vbar->value() >= vbar->maximum() - 5;
    const int oldValue = vbar->value();

    m_transcriptLines.append(html);
    m_transcript->append(html);

    if (!wasAtBottom) {
        vbar->setValue(oldValue);
    } else {
        vbar->setValue(vbar->maximum());
    }

    if (persist) {
        saveHistoryLine(html);
    }
}

void ChatWindow::renderTranscript()
{
    m_transcript->clear();
    for (const QString &line : std::as_const(m_transcriptLines)) {
        m_transcript->append(line);
    }
    m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
}

bool ChatWindow::isTranscriptAtBottom() const
{
    const QScrollBar *vbar = m_transcript->verticalScrollBar();
    return vbar->value() >= vbar->maximum() - 5;
}

void ChatWindow::noteNewMessageMarker()
{
    if (isTranscriptAtBottom()) {
        return;
    }

    const int unread = property("unreadCount").toInt();
    setProperty("unreadCount", unread + 1);
    updateNewMessageMarker();
}

void ChatWindow::updateNewMessageMarker()
{
    auto *button = m_transcript->findChild<QPushButton*>(QStringLiteral("ScrollUpButton"));
    if (!button) {
        return;
    }

    const int unread = property("unreadCount").toInt();
    if (unread <= 0 || isTranscriptAtBottom()) {
        button->hide();
        return;
    }

    button->setText(tr("↓ %n new message(s)", nullptr, unread));
    button->show();
    button->raise();
}

void ChatWindow::clearNewMessageMarker()
{
    setProperty("unreadCount", 0);
    if (auto *button = m_transcript->findChild<QPushButton*>(QStringLiteral("ScrollUpButton"))) {
        button->hide();
    }
}

void ChatWindow::updateNewMessageMarkerStyle()
{
    if (!m_transcript) {
        return;
    }
    auto *button = m_transcript->findChild<QPushButton*>(QStringLiteral("ScrollUpButton"));
    if (!button) {
        return;
    }

    const QColor accent = m_localAccentColor.isValid() ? m_localAccentColor : QColor(QStringLiteral("#1d9bf0"));
    button->setStyleSheet(
        QStringLiteral("QPushButton#ScrollUpButton { background: #ffffff; color: %1; border: 1px solid %2; border-radius: 14px; padding: 4px 14px; font-weight: bold; }"
                       "QPushButton#ScrollUpButton:hover { background: %3; }")
            .arg(accent.name(),
                 accent.lighter(155).name(),
                 accent.lighter(195).name()));
}

void ChatWindow::setLocalTyping(bool typing)
{
    if (m_localTyping == typing) {
        return;
    }

    m_localTyping = typing;
    emit typingStateChanged(m_peerId, typing);
}

QString ChatWindow::historyPath() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::homePath() + QStringLiteral("/BlinqMessenger");
    }
    return QDir(basePath).filePath(QStringLiteral("history/%1.html").arg(safePeerFileName()));
}

QString ChatWindow::safePeerFileName() const
{
    QString result = m_peerId;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return result;
}

QPixmap ChatWindow::avatarPixmap(const QByteArray &avatarData) const
{
    QPixmap pixmap;
    if (!avatarData.isEmpty()) {
        pixmap.loadFromData(avatarData);
    }
    if (pixmap.isNull()) {
        pixmap = QPixmap(48, 48);
        pixmap.fill(QColor(QStringLiteral("#dbeafe")));
        QPainter painter(&pixmap);
        painter.setPen(QColor(QStringLiteral("#1e3a8a")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 18, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, m_peerName.left(1).toUpper());
    }
    const int frame = 3;
    const int innerSize = 48 - frame * 2;
    pixmap = pixmap.scaled(innerSize, innerSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPixmap rounded(48, 48);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient frameGradient(0, 0, 48, 48);
    const QColor accent = m_avatarFrameColor.isValid() ? m_avatarFrameColor : QColor(QStringLiteral("#7dd3fc"));
    frameGradient.setColorAt(0.0, QColor(QStringLiteral("#ffffff")));
    frameGradient.setColorAt(0.48, accent.lighter(175));
    frameGradient.setColorAt(1.0, accent);
    painter.setPen(QPen(accent.darker(115), 1));
    painter.setBrush(frameGradient);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 47, 47), 9, 9);
    QPainterPath path;
    path.addRoundedRect(QRectF(frame, frame, innerSize, innerSize), 7, 7);
    painter.setClipPath(path);
    painter.drawPixmap(frame, frame, pixmap);
    return rounded;
}

void ChatWindow::addTransferUi(const QString &fileId, const QString &fileName, int totalChunks, bool isSending)
{
    auto *row = new QWidget(m_transfersContainer);
    row->setStyleSheet(QStringLiteral("QWidget { background:#f8fafc; border:1px solid #e2e8f0; border-radius:6px; }"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 4, 8, 4);

    auto *label = new QLabel(tr("%1: %2").arg(isSending ? tr("Sending") : tr("Receiving"), fileName), row);
    label->setStyleSheet(QStringLiteral("border:none; background:transparent; font-size:11px; color:#475569;"));
    
    auto *bar = new QProgressBar(row);
    bar->setRange(0, totalChunks);
    bar->setValue(0);
    bar->setFixedHeight(18);
    bar->setTextVisible(true);
    bar->setStyleSheet(QStringLiteral(
        "QProgressBar { border:1px solid #cbd5e1; border-radius:4px; background:#ffffff; text-align:center; color:#111827; font-weight:bold; font-size:11px; }"
        "QProgressBar::chunk { background:#22c55e; border-radius:3px; }"));

    auto *cancelBtn = new QPushButton(tr("Cancel"), row);
    cancelBtn->setStyleSheet(QStringLiteral("QPushButton { background:#fee2e2; color:#ef4444; border:1px solid #fca5a5; border-radius:4px; padding:2px 8px; font-weight:bold; }"
                                            "QPushButton:hover { background:#fecaca; }"));

    connect(cancelBtn, &QPushButton::clicked, this, [this, fileId] {
        emit cancelTransferRequested(fileId);
    });

    layout->addWidget(label);
    layout->addWidget(bar, 1);
    layout->addWidget(cancelBtn);

    m_transfersLayout->addWidget(row);
    m_activeTransfers.insert(fileId, {row, bar});
    m_transfersContainer->show();
}

void ChatWindow::updateTransferUi(const QString &fileId, int currentChunk)
{
    if (m_activeTransfers.contains(fileId)) {
        m_activeTransfers[fileId].bar->setValue(currentChunk);
    }
}

void ChatWindow::removeTransferUi(const QString &fileId)
{
    if (m_activeTransfers.contains(fileId)) {
        auto ui = m_activeTransfers.take(fileId);
        ui.row->hide();
        ui.row->deleteLater();
    }
    if (m_activeTransfers.isEmpty()) {
        m_transfersContainer->hide();
    }
}
