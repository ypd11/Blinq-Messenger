#include "settingsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_savedManualPeerAddresses(settings.manualPeerAddresses)
{
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(parent ? qMax(parent->width(), 420) : 420, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto *tabs = new QTabWidget(this);
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #d6deea; background: #ffffff; }"
        "QTabBar::tab { background: #e8eef7; color: #1f2937; border: 1px solid #cbd5e1; padding: 7px 12px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #ffffff; color: #0f2748; border-bottom-color: #ffffff; }"
        "QTabBar::tab:hover { background: #d7e8ff; color: #0f2748; }"
        "QListWidget { background:#ffffff; color:#111827; border:1px solid #d6deea; border-radius:8px; padding:4px; selection-color:#111827; }"
        "QListWidget::item { color:#111827; }"
        "QListWidget::item:selected { color:#111827; }"));
    layout->addWidget(tabs, 1);

    auto *behaviorPage = new QWidget(tabs);
    auto *behaviorLayout = new QVBoxLayout(behaviorPage);
    behaviorLayout->setContentsMargins(12, 12, 12, 12);
    behaviorLayout->setSpacing(8);
    m_notificationsCheck = new QCheckBox(tr("Show Windows notifications"), behaviorPage);
    m_directNotificationsCheck = new QCheckBox(tr("Direct message notifications"), behaviorPage);
    m_publicNotificationsCheck = new QCheckBox(tr("Public chat notifications"), behaviorPage);
    m_minimizeToTrayCheck = new QCheckBox(tr("Hide to tray when closing the main window"), behaviorPage);
    m_openChatOnMessageCheck = new QCheckBox(tr("Open chat window when a message arrives"), behaviorPage);
    m_launchWithWindowsCheck = new QCheckBox(tr("Launch with Windows hidden to tray"), behaviorPage);
    m_saveHistoryCheck = new QCheckBox(tr("Save chat history locally"), behaviorPage);
    m_showPlayingInfoCheck = new QCheckBox(tr("Show currently playing media in my profile"), behaviorPage);
    m_hideTypingIndicatorCheck = new QCheckBox(tr("Hide my typing indicator"), behaviorPage);
    m_internetSearchableCheck = new QCheckBox(tr("Allow people to find me in Internet search"), behaviorPage);
    m_awayAutoReplyCheck = new QCheckBox(tr("Send an away auto-reply"), behaviorPage);
    m_awayAutoReplyEdit = new QLineEdit(behaviorPage);
    m_awayAutoReplyEdit->setPlaceholderText(tr("Auto-reply message"));
    m_notificationsCheck->setChecked(settings.showNotifications);
    m_directNotificationsCheck->setChecked(settings.directMessageNotifications);
    m_publicNotificationsCheck->setChecked(settings.publicChatNotifications);
    m_minimizeToTrayCheck->setChecked(settings.minimizeToTray);
    m_openChatOnMessageCheck->setChecked(settings.openChatOnMessage);
    m_launchWithWindowsCheck->setChecked(settings.launchWithWindows);
    m_saveHistoryCheck->setChecked(settings.saveHistory);
    m_showPlayingInfoCheck->setChecked(settings.showPlayingInfo);
    m_hideTypingIndicatorCheck->setChecked(settings.hideTypingIndicator);
    m_internetSearchableCheck->setChecked(settings.internetSearchable);
    m_awayAutoReplyCheck->setChecked(settings.awayAutoReply);
    m_awayAutoReplyEdit->setText(settings.awayAutoReplyMessage);
    m_awayAutoReplyEdit->setEnabled(settings.awayAutoReply);
    behaviorLayout->addWidget(m_notificationsCheck);
    behaviorLayout->addWidget(m_directNotificationsCheck);
    behaviorLayout->addWidget(m_publicNotificationsCheck);
    behaviorLayout->addWidget(m_minimizeToTrayCheck);
    behaviorLayout->addWidget(m_openChatOnMessageCheck);
    behaviorLayout->addWidget(m_showPlayingInfoCheck);
    behaviorLayout->addWidget(m_hideTypingIndicatorCheck);
    behaviorLayout->addWidget(m_internetSearchableCheck);
    behaviorLayout->addWidget(m_awayAutoReplyCheck);
    behaviorLayout->addWidget(m_awayAutoReplyEdit);
    behaviorLayout->addWidget(m_launchWithWindowsCheck);
    behaviorLayout->addWidget(m_saveHistoryCheck);
    behaviorLayout->addStretch();
    connect(m_awayAutoReplyCheck, &QCheckBox::toggled, m_awayAutoReplyEdit, &QLineEdit::setEnabled);
    tabs->addTab(behaviorPage, tr("Behavior"));

    auto *blockedPage = new QWidget(tabs);
    auto *blockedLayout = new QVBoxLayout(blockedPage);
    blockedLayout->setContentsMargins(12, 12, 12, 12);
    blockedLayout->setSpacing(10);
    m_blockedList = new QListWidget(blockedPage);
    for (auto it = settings.blockedPeers.constBegin(); it != settings.blockedPeers.constEnd(); ++it) {
        auto *item = new QListWidgetItem(it.value(), m_blockedList);
        item->setData(Qt::UserRole, it.key());
        item->setToolTip(it.key());
    }
    m_blockedEmptyLabel = new QLabel(tr("No blocked users."), blockedPage);
    m_blockedEmptyLabel->setAlignment(Qt::AlignCenter);
    m_blockedEmptyLabel->setStyleSheet(QStringLiteral("color:#64748b; padding:18px;"));
    auto *unblockButton = new QPushButton(tr("Remove Selected"), blockedPage);
    auto *blockedButtons = new QWidget(blockedPage);
    auto *blockedButtonsLayout = new QHBoxLayout(blockedButtons);
    blockedButtonsLayout->setContentsMargins(0, 8, 0, 0);
    blockedButtonsLayout->addStretch();
    blockedButtonsLayout->addWidget(unblockButton);
    const auto updateBlockedEmpty = [this] {
        m_blockedEmptyLabel->setVisible(m_blockedList->count() == 0);
        m_blockedList->setVisible(m_blockedList->count() > 0);
    };
    connect(unblockButton, &QPushButton::clicked, this, [this, updateBlockedEmpty] {
        delete m_blockedList->takeItem(m_blockedList->currentRow());
        updateBlockedEmpty();
    });
    blockedLayout->addWidget(m_blockedList, 1);
    blockedLayout->addWidget(m_blockedEmptyLabel, 1);
    blockedLayout->addWidget(blockedButtons);
    updateBlockedEmpty();
    tabs->addTab(blockedPage, tr("Blocked Users"));

    if (settings.appMode != QStringLiteral("internet")) {
        auto *manualPage = new QWidget(tabs);
        auto *manualLayout = new QVBoxLayout(manualPage);
        manualLayout->setContentsMargins(12, 12, 12, 12);
        manualLayout->setSpacing(10);
        m_manualPeersList = new QListWidget(manualPage);
        m_manualPeersList->setObjectName(QStringLiteral("ManualPeersList"));
        for (const QString &address : settings.manualPeerAddresses) {
            auto *item = new QListWidgetItem(address, m_manualPeersList);
            item->setData(Qt::UserRole, address);
        }
        m_manualEmptyLabel = new QLabel(tr("No manual IP addresses saved."), manualPage);
        m_manualEmptyLabel->setObjectName(QStringLiteral("ManualEmptyLabel"));
        m_manualEmptyLabel->setAlignment(Qt::AlignCenter);
        m_manualEmptyLabel->setStyleSheet(QStringLiteral("color:#64748b; padding:18px;"));
        auto *manualButtons = new QWidget(manualPage);
        auto *manualButtonsLayout = new QHBoxLayout(manualButtons);
        manualButtonsLayout->setContentsMargins(0, 8, 0, 0);
        manualButtonsLayout->addStretch();
        auto *connectManualButton = new QPushButton(tr("Connect"), manualButtons);
        auto *addManualButton = new QPushButton(tr("Add IP"), manualButtons);
        auto *removeManualButton = new QPushButton(tr("Remove Selected"), manualButtons);
        manualButtonsLayout->addWidget(connectManualButton);
        manualButtonsLayout->addWidget(addManualButton);
        manualButtonsLayout->addWidget(removeManualButton);
        const auto updateManualEmpty = [this] {
            m_manualEmptyLabel->setVisible(m_manualPeersList->count() == 0);
            m_manualPeersList->setVisible(m_manualPeersList->count() > 0);
        };
        connect(addManualButton, &QPushButton::clicked, this, [this] {
            emit directConnectRequested();
        });
        connect(connectManualButton, &QPushButton::clicked, this, [this] {
            if (!m_manualPeersList->currentItem()) {
                return;
            }
            QString address = m_manualPeersList->currentItem()->data(Qt::UserRole).toString();
            if (address.isEmpty()) {
                address = m_manualPeersList->currentItem()->text();
            }
            emit manualPeerConnectRequested(address);
        });
        connect(removeManualButton, &QPushButton::clicked, this, [this, updateManualEmpty] {
            delete m_manualPeersList->takeItem(m_manualPeersList->currentRow());
            updateManualEmpty();
        });
        manualLayout->addWidget(m_manualPeersList, 1);
        manualLayout->addWidget(m_manualEmptyLabel, 1);
        manualLayout->addWidget(manualButtons);
        updateManualEmpty();
        tabs->addTab(manualPage, tr("Manual IPs"));
    }

    auto *resetPage = new QWidget(tabs);
    auto *resetLayout = new QVBoxLayout(resetPage);
    resetLayout->setContentsMargins(12, 12, 12, 12);
    resetLayout->setSpacing(12);

    auto *accountRow = new QWidget(resetPage);
    accountRow->setVisible(settings.appMode == QStringLiteral("internet") && !settings.internetBlinqId.isEmpty());
    auto *accountRowLayout = new QHBoxLayout(accountRow);
    accountRowLayout->setContentsMargins(0, 0, 0, 0);
    accountRowLayout->setSpacing(12);
    auto *accountText = new QLabel(tr("Delete your Blinq account from the Internet server."), accountRow);
    accountText->setWordWrap(true);
    auto *deleteAccountButton = new QPushButton(tr("Delete Blinq Account"), accountRow);
    deleteAccountButton->setStyleSheet(QStringLiteral("QPushButton { background:#fee2e2; color:#7f1d1d; border-color:#fecaca; }"
                                                       "QPushButton:hover { background:#fecaca; }"));
    accountRowLayout->addWidget(accountText, 1);
    accountRowLayout->addWidget(deleteAccountButton, 0);

    auto *passwordRow = new QWidget(resetPage);
    passwordRow->setVisible(settings.appMode == QStringLiteral("internet") && !settings.internetBlinqId.isEmpty());
    auto *passwordRowLayout = new QHBoxLayout(passwordRow);
    passwordRowLayout->setContentsMargins(0, 0, 0, 0);
    passwordRowLayout->setSpacing(12);
    auto *passwordText = new QLabel(tr("Change your Blinq account password."), passwordRow);
    passwordText->setWordWrap(true);
    auto *changePasswordButton = new QPushButton(tr("Change Password"), passwordRow);
    passwordRowLayout->addWidget(passwordText, 1);
    passwordRowLayout->addWidget(changePasswordButton, 0);

    auto *resetRow = new QWidget(resetPage);
    auto *resetRowLayout = new QHBoxLayout(resetRow);
    resetRowLayout->setContentsMargins(0, 0, 0, 0);
    resetRowLayout->setSpacing(12);
    auto *resetText = new QLabel(tr("Return Blinq Messenger to its default settings."), resetRow);
    resetText->setWordWrap(true);
    auto *resetButton = new QPushButton(tr("Reset to Defaults"), resetRow);
    resetButton->setStyleSheet(QStringLiteral("QPushButton { background:#fee2e2; color:#7f1d1d; border-color:#fecaca; }"
                                              "QPushButton:hover { background:#fecaca; }"));
    resetRowLayout->addWidget(resetText, 1);
    resetRowLayout->addWidget(resetButton, 0);

    auto *backupRow = new QWidget(resetPage);
    auto *backupRowLayout = new QHBoxLayout(backupRow);
    backupRowLayout->setContentsMargins(0, 0, 0, 0);
    backupRowLayout->setSpacing(12);
    auto *backupText = new QLabel(tr("Export your settings and chat history to a backup file."), backupRow);
    backupText->setWordWrap(true);
    auto *backupButton = new QPushButton(tr("Backup Data"), backupRow);
    backupRowLayout->addWidget(backupText, 1);
    backupRowLayout->addWidget(backupButton, 0);

    auto *restoreRow = new QWidget(resetPage);
    auto *restoreRowLayout = new QHBoxLayout(restoreRow);
    restoreRowLayout->setContentsMargins(0, 0, 0, 0);
    restoreRowLayout->setSpacing(12);
    auto *restoreText = new QLabel(tr("Import a Blinq backup file and restart the app."), restoreRow);
    restoreText->setWordWrap(true);
    auto *restoreButton = new QPushButton(tr("Restore Data"), restoreRow);
    restoreRowLayout->addWidget(restoreText, 1);
    restoreRowLayout->addWidget(restoreButton, 0);

    resetLayout->addWidget(accountRow);
    resetLayout->addWidget(passwordRow);
    resetLayout->addWidget(backupRow);
    resetLayout->addWidget(restoreRow);
    resetLayout->addWidget(resetRow);
    resetLayout->addStretch();
    connect(deleteAccountButton, &QPushButton::clicked, this, [this] {
        emit deleteBlinqAccountRequested();
    });
    connect(changePasswordButton, &QPushButton::clicked, this, [this] {
        emit changeBlinqPasswordRequested();
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        emit resetSettingsRequested();
    });
    connect(backupButton, &QPushButton::clicked, this, [this] {
        emit backupDataRequested();
    });
    connect(restoreButton, &QPushButton::clicked, this, [this] {
        emit restoreDataRequested();
    });
    tabs->addTab(resetPage, tr("Reset"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings result;
    result.showNotifications = m_notificationsCheck->isChecked();
    result.directMessageNotifications = m_directNotificationsCheck->isChecked();
    result.publicChatNotifications = m_publicNotificationsCheck->isChecked();
    result.minimizeToTray = m_minimizeToTrayCheck->isChecked();
    result.openChatOnMessage = m_openChatOnMessageCheck->isChecked();
    result.launchWithWindows = m_launchWithWindowsCheck->isChecked();
    result.saveHistory = m_saveHistoryCheck->isChecked();
    result.showPlayingInfo = m_showPlayingInfoCheck->isChecked();
    result.hideTypingIndicator = m_hideTypingIndicatorCheck->isChecked();
    result.internetSearchable = m_internetSearchableCheck->isChecked();
    result.awayAutoReply = m_awayAutoReplyCheck->isChecked();
    result.awayAutoReplyMessage = m_awayAutoReplyEdit->text().trimmed();
    if (result.awayAutoReplyMessage.isEmpty()) {
        result.awayAutoReplyMessage = tr("I'm away right now. I'll reply when I'm back.");
    }
    for (int row = 0; row < m_blockedList->count(); ++row) {
        auto *item = m_blockedList->item(row);
        result.blockedPeers.insert(item->data(Qt::UserRole).toString(), item->text());
    }
    result.manualPeerAddresses = m_savedManualPeerAddresses;
    if (m_manualPeersList) {
        result.manualPeerAddresses.clear();
        for (int row = 0; row < m_manualPeersList->count(); ++row) {
            QString addr = m_manualPeersList->item(row)->data(Qt::UserRole).toString();
            if (addr.isEmpty()) addr = m_manualPeersList->item(row)->text();
            result.manualPeerAddresses.append(addr);
        }
    }
    result.manualPeerAddresses.removeDuplicates();
    return result;
}
