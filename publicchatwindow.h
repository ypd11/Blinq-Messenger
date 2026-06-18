#ifndef PUBLICCHATWINDOW_H
#define PUBLICCHATWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QStringList>

#include "lanchatservice.h"

class QMenu;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextBrowser;
class QUrl;

class PublicChatWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PublicChatWindow(QWidget *parent = nullptr);
    explicit PublicChatWindow(const QString &windowTitle,
                              const QString &subtitle,
                              const QString &historyFileName,
                              bool participantActionsEnabled,
                              QWidget *parent = nullptr);

    bool eventFilter(QObject *watched, QEvent *event) override;
    void appendIncomingMessage(const QString &sender, const QString &message, const QString &messageId = QString(), bool isHtml = false);
    void appendOutgoingMessage(const QString &message, const QString &messageId = QString(), bool isHtml = false);
    void appendSystemMessage(const QString &message);
    void setParticipants(const QList<ChatPeer> &participants);
    void clearHistory();
    bool shouldNotifyForIncoming() const;
    static void clearSavedHistory();

signals:
    void sendMessageRequested(const QString &message, bool isHtml);
    void viewContactInfoRequested(const QString &peerId);
    void whistleRequested(const QString &peerId);

private:
    void buildUi();
    void connectSignals();
    void showEmojiDialog();
    void showRichMessageDialog();
    void handleTranscriptLink(const QUrl &url);
    void loadHistory();
    void saveHistoryLine(const QString &html);
    void saveHistory();
    void appendHtmlLine(const QString &html, bool persist = true);
    void renderTranscript();
    QString historyPath() const;
    static QString savedHistoryPath();

    QTextBrowser *m_transcript = nullptr;
    QListWidget *m_participantsList = nullptr;
    QLineEdit *m_messageEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_emojiButton = nullptr;
    QPushButton *m_htmlButton = nullptr;
    QMenu *m_emojiMenu = nullptr;
    QStringList m_transcriptLines;
    QHash<QString, int> m_messageLineIndexes;
    QString m_windowTitle;
    QString m_subtitle;
    QString m_historyFileName = QStringLiteral("public.html");
    bool m_participantActionsEnabled = true;
    struct MessageRecord {
        QString sender;
        QString message;
        QString timestamp;
        bool outgoing = false;
        bool isHtml = false;
    };
    QHash<QString, MessageRecord> m_messageRecords;
};

#endif // PUBLICCHATWINDOW_H
