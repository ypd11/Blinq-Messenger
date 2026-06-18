#include "publicchatwindow.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QList>
#include <QListWidget>
#include <QPair>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {
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

QString lastSeenText(const ChatPeer &peer)
{
    if (!peer.lastSeen.isValid()) {
        return QObject::tr("Last seen unknown");
    }
    if (peer.status != QObject::tr("Offline") && peer.status != QObject::tr("Idle") && peer.lastSeen.secsTo(QDateTime::currentDateTimeUtc()) < 20) {
        return QObject::tr("Last seen now");
    }
    return QObject::tr("Last seen %1").arg(peer.lastSeen.toLocalTime().toString(QStringLiteral("MMM d, h:mm AP")));
}

QString displayMessageHtml(const QString &message, bool isHtml)
{
    if (!isHtml) {
        return message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    }

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

QString chatTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("dddd MMMM d, yyyy 'at' h:mm AP"));
}

QString publicMessageHtml(const QString &sender,
                          const QString &message,
                          const QString &timestamp,
                          bool outgoing,
                          bool isHtml)
{
    const QString nameColor = outgoing ? QStringLiteral("#075985") : QStringLiteral("#155e75");
    const QString messageHtml = displayMessageHtml(message, isHtml);

    if (outgoing) {
        return QStringLiteral(
                   "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                   "<tr>"
                   "<td width=\"18%\"></td>"
                   "<td width=\"82%\" align=\"right\" style=\"padding:3px 12px 6px 12px;\">"
                   "<div align=\"right\"><span style=\"color:%1; font-weight:700; font-size:9pt;\">%2</span>"
                   "&nbsp;&nbsp;<span style=\"color:#64748b; font-size:8pt;\">%3</span></div>"
                   "<div align=\"right\" style=\"color:#111827; font-size:11pt; margin-top:1px;\">%4</div>"
                   "</td>"
                   "</tr>"
                   "</table>")
            .arg(nameColor,
                 sender.toHtmlEscaped(),
                 timestamp.toHtmlEscaped(),
                 messageHtml);
    }

    return QStringLiteral(
               "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
               "<tr>"
               "<td width=\"82%\" align=\"left\" style=\"padding:3px 12px 6px 12px;\">"
               "<div align=\"left\"><span style=\"color:%1; font-weight:700; font-size:9pt;\">%2</span>"
               "&nbsp;&nbsp;<span style=\"color:#64748b; font-size:8pt;\">%3</span></div>"
               "<div align=\"left\" style=\"color:#111827; font-size:11pt; margin-top:1px;\">%4</div>"
               "</td>"
               "<td width=\"18%\"></td>"
               "</tr>"
               "</table>")
        .arg(nameColor,
             sender.toHtmlEscaped(),
             timestamp.toHtmlEscaped(),
             messageHtml);
}

QPixmap participantAvatar(const QByteArray &avatarData, const QString &fallbackText, const QColor &frameColor)
{
    QPixmap pixmap;
    if (!avatarData.isEmpty()) {
        pixmap.loadFromData(avatarData);
    }
    constexpr int avatarSize = 48;
    constexpr int frame = 3;
    constexpr int innerSize = avatarSize - frame * 2;
    if (pixmap.isNull()) {
        pixmap = QPixmap(avatarSize, avatarSize);
        pixmap.fill(QColor(QStringLiteral("#dbeafe")));
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(QStringLiteral("#1e3a8a")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 16, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, fallbackText.left(1).toUpper());
    }
    if (pixmap.width() != pixmap.height()) {
        const int squareSize = qMin(pixmap.width(), pixmap.height());
        pixmap = pixmap.copy((pixmap.width() - squareSize) / 2, (pixmap.height() - squareSize) / 2, squareSize, squareSize);
    }
    pixmap = pixmap.scaled(innerSize, innerSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPixmap rounded(avatarSize, avatarSize);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = frameColor.isValid() ? frameColor : QColor(QStringLiteral("#7dd3fc"));
    QLinearGradient frameGradient(0, 0, avatarSize, avatarSize);
    frameGradient.setColorAt(0.0, QColor(QStringLiteral("#ffffff")));
    frameGradient.setColorAt(0.48, accent.lighter(175));
    frameGradient.setColorAt(1.0, accent);
    painter.setPen(QPen(accent.darker(115), 1));
    painter.setBrush(frameGradient);
    painter.drawRoundedRect(QRectF(0.5, 0.5, avatarSize - 1, avatarSize - 1), 9, 9);
    QPainterPath path;
    path.addRoundedRect(QRectF(frame, frame, innerSize, innerSize), 7, 7);
    painter.setClipPath(path);
    painter.drawPixmap(frame, frame, pixmap);
    return rounded;
}
}

PublicChatWindow::PublicChatWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    connectSignals();
    loadHistory();
}

PublicChatWindow::PublicChatWindow(const QString &windowTitle,
                                   const QString &subtitle,
                                   const QString &historyFileName,
                                   bool participantActionsEnabled,
                                   QWidget *parent)
    : QMainWindow(parent)
    , m_windowTitle(windowTitle)
    , m_subtitle(subtitle)
    , m_historyFileName(historyFileName.isEmpty() ? QStringLiteral("public.html") : historyFileName)
    , m_participantActionsEnabled(participantActionsEnabled)
{
    buildUi();
    connectSignals();
    loadHistory();
}

void PublicChatWindow::appendIncomingMessage(const QString &sender, const QString &message, const QString &messageId, bool isHtml)
{
    const QString timestamp = chatTimestamp();
    const QString html = publicMessageHtml(sender, message, timestamp, false, isHtml);
    if (!messageId.isEmpty()) {
        MessageRecord record;
        record.sender = sender;
        record.message = message;
        record.timestamp = timestamp;
        record.outgoing = false;
        record.isHtml = isHtml;
        m_messageRecords.insert(messageId, record);
        m_messageLineIndexes.insert(messageId, m_transcriptLines.size());
    }
    appendHtmlLine(html);
}

bool PublicChatWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_participantsList && watched == m_participantsList->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!m_participantsList->itemAt(mouseEvent->pos())) {
            m_participantsList->clearSelection();
            m_participantsList->setCurrentItem(nullptr);
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void PublicChatWindow::appendOutgoingMessage(const QString &message, const QString &messageId, bool isHtml)
{
    const QString timestamp = chatTimestamp();
    const QString html = publicMessageHtml(tr("Me"), message, timestamp, true, isHtml);
    if (!messageId.isEmpty()) {
        MessageRecord record;
        record.sender = tr("Me");
        record.message = message;
        record.timestamp = timestamp;
        record.outgoing = true;
        record.isHtml = isHtml;
        m_messageRecords.insert(messageId, record);
        m_messageLineIndexes.insert(messageId, m_transcriptLines.size());
    }
    appendHtmlLine(html);
    m_messageEdit->clear();
    m_messageEdit->setFocus();
}

void PublicChatWindow::appendSystemMessage(const QString &message)
{
    appendHtmlLine(QStringLiteral(
                       "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                       "<tr><td align=\"center\" style=\"padding:5px 12px;\">"
                       "<span style=\"color:#64748b; font-size:8pt;\"><i>%1 %2</i></span>"
                       "</td></tr>"
                       "</table>")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                            message.toHtmlEscaped()));
}

void PublicChatWindow::setParticipants(const QList<ChatPeer> &participants)
{
    const QString selectedPeerId = m_participantsList->currentItem()
                                       ? m_participantsList->currentItem()->data(Qt::UserRole).toString()
                                       : QString();
    m_participantsList->clear();
    QListWidgetItem *selectedItem = nullptr;
    for (const ChatPeer &peer : participants) {
        if (peer.status == tr("Offline")) {
            continue;
        }
        auto *item = new QListWidgetItem(m_participantsList);
        item->setData(Qt::UserRole, peer.id);
        if (peer.id == selectedPeerId) {
            selectedItem = item;
        }
        item->setToolTip(tr("%1  |  %2:%3").arg(peer.status, peer.address.toString()).arg(peer.port));
        item->setSizeHint(QSize(180, 62));

        auto *row = new QWidget(m_participantsList);
        row->setStyleSheet(QStringLiteral("QWidget { background: transparent; } QLabel { background: transparent; }"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 5, 6, 5);
        rowLayout->setSpacing(8);
        auto *avatar = new QLabel(row);
        avatar->setFixedSize(48, 48);
        avatar->setPixmap(participantAvatar(peer.avatarData, peer.name, QColor(peer.themeColor)));
        avatar->setStyleSheet(QStringLiteral("background: transparent;"));
        rowLayout->addWidget(avatar);
        auto *text = new QWidget(row);
        auto *textLayout = new QVBoxLayout(text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(1);
        auto *name = new QLabel(peer.name, text);
        name->setStyleSheet(QStringLiteral("font-weight:700; color:#111827; font-size:13px;"));
        auto *detail = new QLabel(peer.status, text);
        detail->setStyleSheet(QStringLiteral("color:%1; font-weight:650; font-size:10px;").arg(statusColor(peer.status)));
        auto *seen = new QLabel(lastSeenText(peer), text);
        seen->setStyleSheet(QStringLiteral("color:#64748b; font-size:9px;"));
        textLayout->addWidget(name);
        textLayout->addWidget(detail);
        textLayout->addWidget(seen);
        rowLayout->addWidget(text, 1);
        m_participantsList->setItemWidget(item, row);
    }
    if (selectedItem) {
        m_participantsList->setCurrentItem(selectedItem);
        selectedItem->setSelected(true);
    }
}

void PublicChatWindow::clearHistory()
{
    m_transcript->clear();
    m_transcriptLines.clear();
    m_messageLineIndexes.clear();
    m_messageRecords.clear();
    QFile::remove(historyPath());
}

bool PublicChatWindow::shouldNotifyForIncoming() const
{
    return !isVisible() || isMinimized() || !isActiveWindow();
}

void PublicChatWindow::buildUi()
{
    setWindowTitle(m_windowTitle.isEmpty() ? tr("Public Chat") : m_windowTitle);
    setMinimumSize(560, 420);
    resize(minimumSize());

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *title = new QLabel(m_subtitle.isEmpty() ? tr("Everyone on your network") : m_subtitle, root);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    layout->addWidget(title);

    auto *content = new QWidget(root);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    m_transcript = new QTextBrowser(root);
    m_transcript->setReadOnly(true);
    m_transcript->setOpenLinks(false);
    m_transcript->setOpenExternalLinks(false);
    contentLayout->addWidget(m_transcript, 1);

    auto *participantsPanel = new QWidget(content);
    auto *participantsLayout = new QVBoxLayout(participantsPanel);
    participantsLayout->setContentsMargins(0, 0, 0, 0);
    participantsLayout->setSpacing(6);
    auto *participantsLabel = new QLabel(tr("Participants"), participantsPanel);
    participantsLabel->setStyleSheet(QStringLiteral("font-weight:700; color:#334155;"));
    participantsLayout->addWidget(participantsLabel);
    m_participantsList = new QListWidget(participantsPanel);
    m_participantsList->setIconSize(QSize(32, 32));
    m_participantsList->setFixedWidth(190);
    m_participantsList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_participantsList->viewport()->installEventFilter(this);
    participantsLayout->addWidget(m_participantsList, 1);
    contentLayout->addWidget(participantsPanel);

    layout->addWidget(content, 1);

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

    m_htmlButton = new QPushButton(composer);
    m_htmlButton->setObjectName(QStringLiteral("ChatComposerToolButton"));
    m_htmlButton->setIcon(QIcon(QStringLiteral(":/icons/assets/html.png")));
    m_htmlButton->setIconSize(QSize(18, 18));
    m_htmlButton->setFixedSize(28, 24);
    m_htmlButton->setToolTip(tr("Rich message editor"));
    toolLayout->addWidget(m_htmlButton);
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
    m_messageEdit->setPlaceholderText(tr("Message"));
    inputLayout->addWidget(m_messageEdit, 1);

    m_sendButton = new QPushButton(composer);
    m_sendButton->setObjectName(QStringLiteral("ChatSendButton"));
    m_sendButton->setIcon(QIcon(QStringLiteral(":/icons/assets/send.png")));
    m_sendButton->setIconSize(QSize(22, 22));
    m_sendButton->setFixedSize(34, 34);
    m_sendButton->setToolTip(tr("Send public message"));
    inputLayout->addWidget(m_sendButton);
    composerLayout->addWidget(inputRow);

    layout->addWidget(composer);
    setCentralWidget(root);
}

void PublicChatWindow::connectSignals()
{
    connect(m_sendButton, &QPushButton::clicked, this, [this] {
        const QString message = m_messageEdit->text().trimmed();
        if (!message.isEmpty()) {
            emit sendMessageRequested(messageWithEmoticons(message), false);
        }
    });
    connect(m_messageEdit, &QLineEdit::returnPressed, m_sendButton, &QPushButton::click);
    connect(m_emojiButton, &QPushButton::clicked, this, &PublicChatWindow::showEmojiDialog);
    connect(m_htmlButton, &QPushButton::clicked, this, &PublicChatWindow::showRichMessageDialog);
    connect(m_transcript, &QTextBrowser::anchorClicked, this, &PublicChatWindow::handleTranscriptLink);
    connect(m_participantsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (!m_participantActionsEnabled) {
            return;
        }
        auto *item = m_participantsList->itemAt(position);
        if (!item) {
            return;
        }
        m_participantsList->setCurrentItem(item);
        const QString peerId = item->data(Qt::UserRole).toString();
        QMenu menu(this);
        auto *contactInfoAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/contact_info.png")), tr("View Contact Info"), this, [this, peerId] {
            emit viewContactInfoRequested(peerId);
        });
        contactInfoAction->setIconVisibleInMenu(true);
        menu.addAction(QIcon(QStringLiteral(":/icons/assets/whistle.png")), tr("Whistle"), this, [this, peerId] {
            emit whistleRequested(peerId);
        });
        menu.exec(m_participantsList->viewport()->mapToGlobal(position));
    });
}

void PublicChatWindow::handleTranscriptLink(const QUrl &url)
{
    Q_UNUSED(url);
}

void PublicChatWindow::showEmojiDialog()
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

void PublicChatWindow::showRichMessageDialog()
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
    editor->setPlaceholderText(tr("Write your public message..."));
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
        emit sendMessageRequested(editor->toHtml(), true);
        dialog->accept();
    });

    dialog->setStyleSheet(qApp->styleSheet()
                          + QStringLiteral(
                                "QToolButton { min-width:32px; min-height:28px; border:1px solid #cbd5e1; border-radius:6px; background:#ffffff; }"
                                "QToolButton:hover, QToolButton:checked { background:#dbeafe; border-color:#1d9bf0; }"));
    dialog->show();
    editor->setFocus();
}

void PublicChatWindow::loadHistory()
{
    QFile file(historyPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    while (!file.atEnd()) {
        appendHtmlLine(QString::fromUtf8(file.readLine()).trimmed(), false);
    }
}

void PublicChatWindow::saveHistoryLine(const QString &html)
{
    QFile file(historyPath());
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        file.write(html.toUtf8());
        file.write("\n");
    }
}

void PublicChatWindow::appendHtmlLine(const QString &html, bool persist)
{
    m_transcriptLines.append(html);
    m_transcript->append(html);
    m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
    if (persist) {
        saveHistoryLine(html);
    }
}

void PublicChatWindow::renderTranscript()
{
    m_transcript->clear();
    for (const QString &line : std::as_const(m_transcriptLines)) {
        m_transcript->append(line);
    }
    m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
}

void PublicChatWindow::saveHistory()
{
    QFile file(historyPath());
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }
    for (const QString &line : std::as_const(m_transcriptLines)) {
        file.write(line.toUtf8());
        file.write("\n");
    }
}

QString PublicChatWindow::historyPath() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::homePath() + QStringLiteral("/BlinqMessenger");
    }
    return QDir(basePath).filePath(QStringLiteral("history/%1").arg(m_historyFileName));
}

QString PublicChatWindow::savedHistoryPath()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::homePath() + QStringLiteral("/BlinqMessenger");
    }
    return QDir(basePath).filePath(QStringLiteral("history/public.html"));
}
void PublicChatWindow::clearSavedHistory()
{
    QFile::remove(savedHistoryPath());
}
