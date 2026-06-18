#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QColor>
#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QStringList>
#include <QDateTime>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QMenu;
class QTextBrowser;
class QTimer;
class QUrl;
class QCloseEvent;
class QVBoxLayout;

class ChatWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatWindow(const QString &peerId, QWidget *parent = nullptr);

    QString peerId() const;
    void setPeerDetails(const QString &name, const QString &status, const QByteArray &avatarData, const QDateTime &lastSeen = QDateTime(), const QColor &themeColor = QColor());
    void setLocalAccentColor(const QColor &color);
    void appendIncomingMessage(const QString &sender, const QString &message, const QString &messageId = QString(), bool isHtml = false);
    void appendOutgoingMessage(const QString &message, const QString &messageId, bool isHtml = false);
    void updateMessageStatus(const QString &messageId, const QString &status);
    void appendSystemMessage(const QString &message);
    void setPeerTyping(bool typing);
    void appendIncomingFile(const QString &sender, const QString &fileName, const QByteArray &data);
    void appendFileTransferStarted(const QString &sender, const QString &fileName);
    void appendOutgoingFile(const QString &fileName, const QString &filePath = QString(), bool isImage = false);
    bool shouldNotifyForIncoming() const;
    void setHistoryEnabled(bool enabled);
    void setQueuedMessageCount(int count);
    void setImageAttachmentsOnly(bool enabled);
    void clearHistory();
    void addTransferUi(const QString &fileId, const QString &fileName, int totalChunks, bool isSending);
    void updateTransferUi(const QString &fileId, int currentChunk);
    void removeTransferUi(const QString &fileId);

signals:
    void becameActive();
    void chatClosed();
    void sendMessageRequested(const QString &peerId, const QString &message, bool isHtml);
    void sendFileRequested(const QString &peerId, const QString &filePath);
    void typingStateChanged(const QString &peerId, bool isTyping);
    void buzzRequested(const QString &peerId);
    void activityRequested(const QString &peerId, const QString &activity);
    void retryQueuedMessagesRequested(const QString &peerId);
    void viewQueuedMessagesRequested(const QString &peerId);
    void clearQueuedMessagesRequested(const QString &peerId);
    void cancelTransferRequested(const QString &fileId);

private:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void buildUi();
    void connectSignals();
    void showEmojiDialog();
    void showRichMessageDialog();
    void handleTranscriptLink(const QUrl &url);
    void showSearchBar();
    void loadHistory();
    void saveHistoryLine(const QString &html);
    void saveHistory();
    void appendHtmlLine(const QString &html, bool persist = true);
    void renderTranscript();
    bool isTranscriptAtBottom() const;
    void noteNewMessageMarker();
    void updateNewMessageMarker();
    void clearNewMessageMarker();
    void updateNewMessageMarkerStyle();
    void setLocalTyping(bool typing);
    QString historyPath() const;
    QString safePeerFileName() const;
    QPixmap avatarPixmap(const QByteArray &avatarData) const;

    QString m_peerId;
    QString m_peerName;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_lastSeenLabel = nullptr;
    QTextBrowser *m_transcript = nullptr;
    QLabel *m_typingLabel = nullptr;
    QWidget *m_queueBanner = nullptr;
    QWidget *m_transfersContainer = nullptr;
    QVBoxLayout *m_transfersLayout = nullptr;
    QLabel *m_queueLabel = nullptr;
    QPushButton *m_viewQueueButton = nullptr;
    QPushButton *m_retryQueueButton = nullptr;
    QPushButton *m_clearQueueButton = nullptr;
    QLineEdit *m_messageEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_fileButton = nullptr;
    QPushButton *m_emojiButton = nullptr;
    QPushButton *m_htmlButton = nullptr;
    QPushButton *m_activityButton = nullptr;
    QPushButton *m_buzzButton = nullptr;
    QMenu *m_emojiMenu = nullptr;
    QTimer *m_typingIdleTimer = nullptr;
    QStringList m_transcriptLines;
    QColor m_avatarFrameColor;
    QColor m_localAccentColor;
    QHash<QString, int> m_outgoingLineIndexes;
    QHash<QString, QString> m_outgoingMessages;
    QHash<QString, QString> m_outgoingTimestamps;
    QHash<QString, bool> m_outgoingHtmlModes;
    QHash<QString, int> m_messageLineIndexes;
    struct MessageRecord {
        QString sender;
        QString message;
        QString timestamp;
        QString status;
        bool outgoing = false;
        bool isHtml = false;
    };
    QHash<QString, MessageRecord> m_messageRecords;
    bool m_localTyping = false;
    bool m_historyEnabled = true;
    bool m_imageAttachmentsOnly = false;

    struct TransferUi {
        QWidget *row;
        QProgressBar *bar;
    };
    QHash<QString, TransferUi> m_activeTransfers;
};

#endif // CHATWINDOW_H
