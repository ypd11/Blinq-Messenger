#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "chatwindow.h"
#include "internetrelayservice.h"
#include "lanchatservice.h"
#include "publicchatwindow.h"
#include "windowsmediawatcher.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractSocket>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QFontMetrics>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenuBar>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSoundEffect>
#include <QSettings>
#include <QSaveFile>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedLayout>
#include <QStatusBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOption>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>
#include <QUuid>
#include <QVersionNumber>

#include <algorithm>

namespace {
constexpr int ChatIdleTimeoutMs = 300000;
constexpr int ContactItemTypeRole = Qt::UserRole;
constexpr int ContactItemIdRole = Qt::UserRole + 1;
const QByteArray OfflineQueueKey = QByteArrayLiteral("LANChat-v1-offline-queue-key");
const QString UpdateMetadataUrl = QStringLiteral("https://raw.githubusercontent.com/ypd11/Blinq-Messenger/main/update.json");

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

class DrawingCanvas final : public QWidget
{
public:
    explicit DrawingCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_image(520, 320, QImage::Format_ARGB32_Premultiplied)
    {
        setFixedSize(m_image.size());
        setAttribute(Qt::WA_StyledBackground, false);
        m_image.fill(Qt::white);
    }

    void clear()
    {
        pushUndoState();
        m_image.fill(Qt::white);
        update();
    }

    bool canUndo() const
    {
        return !m_undoStack.isEmpty();
    }

    bool canRedo() const
    {
        return !m_redoStack.isEmpty();
    }

    void undo()
    {
        if (!canUndo()) {
            return;
        }
        m_redoStack.push_back(m_image);
        m_image = m_undoStack.takeLast();
        update();
    }

    void redo()
    {
        if (!canRedo()) {
            return;
        }
        m_undoStack.push_back(m_image);
        m_image = m_redoStack.takeLast();
        update();
    }

    void setPenColor(const QColor &color)
    {
        if (color.isValid()) {
            m_penColor = color;
            m_eraser = false;
        }
    }

    QColor penColor() const
    {
        return m_penColor;
    }

    void setBrushSize(int size)
    {
        m_brushSize = qBound(2, size, 28);
    }

    void setEraser(bool eraser)
    {
        m_eraser = eraser;
    }

    QImage image() const
    {
        return m_image;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect target = imageTargetRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#ffffff")));
        painter.drawRoundedRect(target.adjusted(0, 0, -1, -1), 8, 8);
        QPainterPath clip;
        clip.addRoundedRect(target.adjusted(1, 1, -2, -2), 7, 7);
        painter.setClipPath(clip);
        painter.drawImage(target, m_image);
        painter.setClipping(false);
        painter.setPen(QPen(QColor(QStringLiteral("#cbd5e1")), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(target.adjusted(0, 0, -1, -1), 8, 8);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        pushUndoState();
        m_redoStack.clear();
        m_drawing = true;
        m_lastPoint = imagePoint(event->position().toPoint());
        drawLineTo(m_lastPoint);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_drawing) {
            return;
        }
        drawLineTo(imagePoint(event->position().toPoint()));
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_drawing = false;
        }
    }

private:
    QRect imageTargetRect() const
    {
        return QRect(QPoint(0, 0), m_image.size());
    }

    QPoint imagePoint(const QPoint &widgetPoint) const
    {
        const QRect target = imageTargetRect();
        return QPoint(qBound(0, widgetPoint.x() - target.left(), m_image.width() - 1),
                      qBound(0, widgetPoint.y() - target.top(), m_image.height() - 1));
    }

    void pushUndoState()
    {
        m_undoStack.push_back(m_image);
        if (m_undoStack.size() > 24) {
            m_undoStack.removeFirst();
        }
    }

    void drawLineTo(const QPoint &nextPoint)
    {
        QPainter painter(&m_image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color = m_eraser ? QColor(Qt::white) : m_penColor;
        painter.setPen(QPen(color, m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(m_lastPoint, nextPoint);
        m_lastPoint = nextPoint;
    }

    QImage m_image;
    QList<QImage> m_undoStack;
    QList<QImage> m_redoStack;
    QColor m_penColor = QColor(QStringLiteral("#111827"));
    int m_brushSize = 4;
    QPoint m_lastPoint;
    bool m_drawing = false;
    bool m_eraser = false;
};

struct AppTheme
{
    QString key;
    QString label;
    QString window;
    QString panelTop;
    QString panelBottom;
    QString panelBorder;
    QString accent;
    QString accentDark;
    QString hover;
    QString selected;
    QString soft;
};

QList<AppTheme> appThemes()
{
    return {
        {QStringLiteral("msnBlue"), QStringLiteral("Sky"), QStringLiteral("#eef5ff"), QStringLiteral("#ffffff"), QStringLiteral("#c9ecff"), QStringLiteral("#7dbde5"), QStringLiteral("#1d9bf0"), QStringLiteral("#0f6fb8"), QStringLiteral("#dff2ff"), QStringLiteral("#bde3ff"), QStringLiteral("#eaf7ff")},
        {QStringLiteral("aqua"), QStringLiteral("Aqua"), QStringLiteral("#eefbf8"), QStringLiteral("#ffffff"), QStringLiteral("#bff4e8"), QStringLiteral("#74d5c3"), QStringLiteral("#0f9f8f"), QStringLiteral("#0f766e"), QStringLiteral("#dcfdf7"), QStringLiteral("#a7f3df"), QStringLiteral("#ebfffb")},
        {QStringLiteral("mint"), QStringLiteral("Mint"), QStringLiteral("#f1fbf4"), QStringLiteral("#ffffff"), QStringLiteral("#c9f3d6"), QStringLiteral("#7fd59c"), QStringLiteral("#22a45a"), QStringLiteral("#166534"), QStringLiteral("#e3fbea"), QStringLiteral("#bbf7d0"), QStringLiteral("#f0fdf4")},
        {QStringLiteral("rose"), QStringLiteral("Rose"), QStringLiteral("#fff1f5"), QStringLiteral("#ffffff"), QStringLiteral("#ffd1dc"), QStringLiteral("#f7a3b8"), QStringLiteral("#e11d48"), QStringLiteral("#9f1239"), QStringLiteral("#ffe4e6"), QStringLiteral("#fecdd3"), QStringLiteral("#fff5f7")},
        {QStringLiteral("sunset"), QStringLiteral("Sunset"), QStringLiteral("#fff6ed"), QStringLiteral("#ffffff"), QStringLiteral("#ffd6a8"), QStringLiteral("#f5a65b"), QStringLiteral("#f97316"), QStringLiteral("#c2410c"), QStringLiteral("#ffedd5"), QStringLiteral("#fed7aa"), QStringLiteral("#fff7ed")},
        {QStringLiteral("violet"), QStringLiteral("Violet"), QStringLiteral("#f7f2ff"), QStringLiteral("#ffffff"), QStringLiteral("#dec7ff"), QStringLiteral("#b897f1"), QStringLiteral("#7c3aed"), QStringLiteral("#5b21b6"), QStringLiteral("#ede9fe"), QStringLiteral("#ddd6fe"), QStringLiteral("#faf5ff")},
        {QStringLiteral("orchid"), QStringLiteral("Orchid"), QStringLiteral("#fdf4ff"), QStringLiteral("#ffffff"), QStringLiteral("#f5d0fe"), QStringLiteral("#d8a8e8"), QStringLiteral("#c026d3"), QStringLiteral("#86198f"), QStringLiteral("#fae8ff"), QStringLiteral("#f0abfc"), QStringLiteral("#fdf4ff")},
        {QStringLiteral("ruby"), QStringLiteral("Ruby"), QStringLiteral("#fff5f5"), QStringLiteral("#ffffff"), QStringLiteral("#fecaca"), QStringLiteral("#f87171"), QStringLiteral("#dc2626"), QStringLiteral("#991b1b"), QStringLiteral("#fee2e2"), QStringLiteral("#fecaca"), QStringLiteral("#fff7f7")},
        {QStringLiteral("midnight"), QStringLiteral("Midnight"), QStringLiteral("#eef2ff"), QStringLiteral("#ffffff"), QStringLiteral("#c7d2fe"), QStringLiteral("#93a3dc"), QStringLiteral("#4338ca"), QStringLiteral("#312e81"), QStringLiteral("#e0e7ff"), QStringLiteral("#c7d2fe"), QStringLiteral("#f5f7ff")},
        {QStringLiteral("graphite"), QStringLiteral("Graphite"), QStringLiteral("#f2f5f8"), QStringLiteral("#ffffff"), QStringLiteral("#d7dee8"), QStringLiteral("#a8b4c4"), QStringLiteral("#475569"), QStringLiteral("#1f2937"), QStringLiteral("#e2e8f0"), QStringLiteral("#cbd5e1"), QStringLiteral("#f8fafc")}
    };
}

AppTheme appThemeForKey(const QString &key)
{
    const QList<AppTheme> themes = appThemes();
    for (const AppTheme &theme : themes) {
        if (theme.key == key) {
            return theme;
        }
    }
    return themes.first();
}

QIcon themeSwatchIcon(const AppTheme &theme)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, 18, 18);
    gradient.setColorAt(0.0, QColor(theme.panelTop));
    gradient.setColorAt(0.52, QColor(theme.panelBottom));
    gradient.setColorAt(1.0, QColor(theme.accent));
    painter.setPen(QColor(theme.panelBorder));
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(1, 1, 16, 16), 4, 4);
    return QIcon(pixmap);
}

QByteArray cryptLocalSettingsBytes(const QByteArray &data)
{
    QByteArray result;
    result.resize(data.size());

    qsizetype offset = 0;
    quint32 counter = 0;
    while (offset < data.size()) {
        QByteArray seed = OfflineQueueKey;
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

QString encryptLocalSecret(const QString &secret)
{
    if (secret.isEmpty()) {
        return QString();
    }
    return QString::fromLatin1(cryptLocalSettingsBytes(secret.toUtf8()).toBase64());
}

QString decryptLocalSecret(const QString &stored)
{
    if (stored.isEmpty()) {
        return QString();
    }
    return QString::fromUtf8(cryptLocalSettingsBytes(QByteArray::fromBase64(stored.toLatin1())));
}

bool isNewerVersion(const QString &latest, const QString &current)
{
    const QVersionNumber latestVersion = QVersionNumber::fromString(latest.trimmed());
    const QVersionNumber currentVersion = QVersionNumber::fromString(current.trimmed());
    if (!latestVersion.isNull() && !currentVersion.isNull()) {
        return QVersionNumber::compare(latestVersion, currentVersion) > 0;
    }
    return QString::localeAwareCompare(latest.trimmed(), current.trimmed()) > 0;
}

QString appDataPath()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::homePath() + QStringLiteral("/BlinqMessenger");
    }
    return basePath;
}

QJsonObject settingsToJson()
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    QJsonObject values;
    for (const QString &key : settings.allKeys()) {
        values.insert(key, QJsonValue::fromVariant(settings.value(key)));
    }
    return values;
}

void restoreSettingsFromJson(const QJsonObject &values)
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    settings.clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        settings.setValue(it.key(), it.value().toVariant());
    }
    settings.sync();
}

QJsonArray historyToJson()
{
    QJsonArray files;
    const QDir historyDir(QDir(appDataPath()).filePath(QStringLiteral("history")));
    if (!historyDir.exists()) {
        return files;
    }

    const QFileInfoList entries = historyDir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("name"), entry.fileName());
        object.insert(QStringLiteral("data"), QString::fromLatin1(file.readAll().toBase64()));
        files.append(object);
    }
    return files;
}

bool restoreHistoryFromJson(const QJsonArray &files)
{
    QDir baseDir(appDataPath());
    if (!baseDir.exists() && !baseDir.mkpath(QStringLiteral("."))) {
        return false;
    }

    QDir historyDir(baseDir.filePath(QStringLiteral("history")));
    if (historyDir.exists() && !historyDir.removeRecursively()) {
        return false;
    }
    if (!baseDir.mkpath(QStringLiteral("history"))) {
        return false;
    }
    historyDir = QDir(baseDir.filePath(QStringLiteral("history")));

    for (const QJsonValue &value : files) {
        const QJsonObject object = value.toObject();
        const QString safeName = QFileInfo(object.value(QStringLiteral("name")).toString()).fileName();
        if (safeName.isEmpty()) {
            continue;
        }
        QSaveFile file(historyDir.filePath(safeName));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1()));
        if (!file.commit()) {
            return false;
        }
    }
    return true;
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
    if (status == QObject::tr("Invisible")) {
        return QStringLiteral("#64748b");
    }
    return QStringLiteral("#2563eb");
}

QString lastSeenText(const ChatPeer &peer)
{
    if (peer.lastSeen.isValid()) {
        const qint64 seconds = peer.lastSeen.secsTo(QDateTime::currentDateTimeUtc());
        if (peer.status != QObject::tr("Offline") && peer.status != QObject::tr("Idle") && seconds < 20) {
            return QObject::tr("Last seen now");
        }
        return QObject::tr("Last seen %1").arg(peer.lastSeen.toLocalTime().toString(QStringLiteral("MMM d, h:mm AP")));
    }
    return QObject::tr("Last seen unknown");
}

QPixmap addMsnReflection(const QPixmap &source)
{
    const int w = source.width();
    const int h = source.height();
    const int refHeight = h / 2;

    QPixmap result(w, h + refHeight);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.drawPixmap(0, 0, source);

    QImage img = source.toImage().flipped(Qt::Vertical);
    QPixmap reflection = QPixmap::fromImage(img).copy(0, 0, w, refHeight);

    QPixmap alphaMask(w, refHeight);
    alphaMask.fill(Qt::transparent);
    QPainter alphaPainter(&alphaMask);
    QLinearGradient gradient(0, 0, 0, refHeight);
    gradient.setColorAt(0.0, QColor(0, 0, 0, 100)); // Start partially visible
    gradient.setColorAt(1.0, QColor(0, 0, 0, 0));   // Fade to completely transparent
    alphaPainter.fillRect(alphaMask.rect(), gradient);
    alphaPainter.end();

    QPainter refPainter(&reflection);
    refPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    refPainter.drawPixmap(0, 0, alphaMask);
    refPainter.end();

    painter.drawPixmap(0, h, reflection);
    return result;
}

QIcon statusDotIcon(const QString &status)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(statusColor(status)));
    painter.drawEllipse(4, 4, 8, 8);
    return QIcon(pixmap);
}

QPixmap disclosurePixmap(bool collapsed, const QColor &color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    QPolygonF triangle;
    if (collapsed) {
        triangle << QPointF(4, 2) << QPointF(9, 6) << QPointF(4, 10);
    } else {
        triangle << QPointF(2, 4) << QPointF(10, 4) << QPointF(6, 9);
    }
    painter.drawPolygon(triangle);
    return pixmap;
}

QString sanitizedGroupName(const QString &name)
{
    QString result = name.trimmed();
    result.replace(QLatin1Char('/'), QLatin1Char('-'));
    result.replace(QLatin1Char('\\'), QLatin1Char('-'));
    return result;
}
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <mmsystem.h>

namespace {
void playWaveFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    PlaySoundW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}
}
#endif

MarqueeLabel::MarqueeLabel(QWidget *parent)
    : QLabel(parent)
    , m_timer(new QTimer(this))
{
    setTextFormat(Qt::PlainText);
    setWordWrap(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_timer, &QTimer::timeout, this, [this] {
        const int textWidth = fontMetrics().horizontalAdvance(m_text);
        if (textWidth <= contentsRect().width()) {
            m_offset = 0;
            m_timer->stop();
            update();
            return;
        }
        m_offset = (m_offset + 1) % (textWidth + 36);
        update();
    });
}

void MarqueeLabel::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    m_offset = 0;
    QLabel::setText(text);
    updateAnimation();
    update();
}

void MarqueeLabel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    const QRect rect = contentsRect().adjusted(6, 0, -6, 0);
    const int textWidth = fontMetrics().horizontalAdvance(m_text);
    painter.setPen(palette().color(QPalette::WindowText));
    if (textWidth <= rect.width()) {
        painter.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, m_text);
        return;
    }

    painter.setClipRect(rect);
    const int gap = 36;
    int x = rect.left() - m_offset;
    while (x < rect.right()) {
        painter.drawText(QRect(x, rect.top(), textWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
        x += textWidth + gap;
    }
}

void MarqueeLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    updateAnimation();
}

void MarqueeLabel::showEvent(QShowEvent *event)
{
    QLabel::showEvent(event);
    updateAnimation();
}

void MarqueeLabel::hideEvent(QHideEvent *event)
{
    QLabel::hideEvent(event);
    m_timer->stop();
}

void MarqueeLabel::updateAnimation()
{
    const bool shouldScroll = isVisible() && fontMetrics().horizontalAdvance(m_text) > contentsRect().adjusted(6, 0, -6, 0).width();
    if (shouldScroll && !m_timer->isActive()) {
        m_timer->start(35);
    } else if (!shouldScroll && m_timer->isActive()) {
        m_timer->stop();
        m_offset = 0;
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_chatService(new LanChatService(this))
    , m_internetRelay(new InternetRelayService(this))
{
    ui->setupUi(this);
    loadSettings();
    if (isInternetMode()) {
        m_peers.clear();
        m_knownPeers.clear();
        m_offlineMessages.clear();
    }
    QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    m_chatService->setLocalThemeColor(appThemeForKey(themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString()).accent);
    buildUi();
    buildMenus();
    buildTrayIcon();
    connectSignals();
    updateLocalProfile();
    rebuildContactList();
    updateEmptyContactsLabel();
    updateContactsLabel();
    if (m_manualPersonalMessage.isEmpty()) {
        m_manualPersonalMessage = m_chatService->localPersonalMessage().startsWith(tr("Now playing: "))
                                      ? tr("Hi, let's chat!")
                                      : m_chatService->localPersonalMessage();
    }
    if (auto *pmEdit = findChild<QLineEdit*>(QStringLiteral("LocalPersonalMessageEdit"))) {
        pmEdit->setText(m_manualPersonalMessage);
    }
    if (!m_settings.showPlayingInfo) {
        m_chatService->setLocalPersonalMessage(m_manualPersonalMessage);
    }
    m_mediaWatcher = new WindowsMediaWatcher(this);
    connect(m_mediaWatcher, &WindowsMediaWatcher::mediaTextChanged, this, [this](const QString &text) {
        m_currentMediaText = text.trimmed();
        const bool showMedia = m_settings.showPlayingInfo && !m_currentMediaText.isEmpty();
        if (m_mediaLabel) {
            m_mediaLabel->setText(showMedia ? tr("Playing: %1").arg(m_currentMediaText) : QString());
            m_mediaLabel->setVisible(showMedia);
        }
        if (!showMedia) {
            m_chatService->setLocalPersonalMessage(m_manualPersonalMessage);
            if (isInternetMode() && m_internetRelay->isAuthenticated()) {
                m_internetRelay->setPresence(m_statusCombo->currentText(), m_manualPersonalMessage);
            }
        } else {
            m_chatService->setLocalPersonalMessage(tr("Now playing: %1").arg(m_currentMediaText));
            if (isInternetMode() && m_internetRelay->isAuthenticated()) {
                m_internetRelay->setPresence(m_statusCombo->currentText(), tr("Now playing: %1").arg(m_currentMediaText));
            }
        }
    });
    m_mediaWatcher->start();
    m_chatIdleTimer = new QTimer(this);
    m_chatIdleTimer->setSingleShot(true);
    m_chatIdleTimer->setInterval(ChatIdleTimeoutMs);
    connect(m_chatIdleTimer, &QTimer::timeout, this, &MainWindow::markChatIdle);
    m_networkStatusTimer = new QTimer(this);
    m_networkStatusTimer->setInterval(5000);
    connect(m_networkStatusTimer, &QTimer::timeout, this, &MainWindow::updateNetworkStatus);
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &message) {
        if (message.isEmpty()) {
            updateNetworkStatus();
        }
    });
    m_networkStatusTimer->start();
    QTimer::singleShot(250, this, [this] {
        setupSounds();
    });
    QTimer::singleShot(650, this, &MainWindow::showWelcomeDialog);
    if (isInternetMode()) {
        QTimer::singleShot(300, this, [this] {
            m_internetRelay->connectToServer(m_settings.internetServerHost,
                                             static_cast<quint16>(m_settings.internetServerPort));
            if (!m_settings.internetAuthToken.isEmpty()) {
                m_internetRelay->resume(m_settings.internetAuthToken);
            } else {
                showInternetSignInDialog();
            }
        });
    } else {
        QTimer::singleShot(300, m_chatService, &LanChatService::start);
    }
    QTimer::singleShot(2200, this, [this] {
        if (isInternetMode()) {
            return;
        }
        const QStringList addresses = rememberedPeerAddresses();
        if (!addresses.isEmpty()) {
            startBackgroundConnectionAttempts(addresses,
                                              tr("Checking saved contact address(es)..."),
                                              false);
        }
    });
    QTimer::singleShot(8000, this, [this] {
        for (const QString &peerId : m_knownPeers.keys()) {
            m_seenOnlineNotifications.insert(peerId);
        }
        m_suppressSignInNotifications = false;
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_peerList && watched == m_peerList->viewport() && event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (m_draggedPeerId.isEmpty()) {
            return QMainWindow::eventFilter(watched, event);
        }

        QListWidgetItem *targetItem = m_peerList->itemAt(dropEvent->position().toPoint());
        QString groupKey;
        if (targetItem) {
            if (targetItem->data(ContactItemTypeRole).toString() == QStringLiteral("group")) {
                groupKey = targetItem->data(ContactItemIdRole).toString();
            } else if (targetItem->data(ContactItemTypeRole).toString() == QStringLiteral("peer")) {
                const QString targetPeerId = targetItem->data(ContactItemIdRole).toString();
                groupKey = contactGroupKey(m_knownPeers.value(targetPeerId, m_peers.value(targetPeerId)));
            }
        }

        if (!groupKey.isEmpty()) {
            if (groupKey == QStringLiteral("favorites")) {
                m_favoritePeers.insert(m_draggedPeerId);
            } else if (groupKey.startsWith(QStringLiteral("custom:"))) {
                m_peerGroups.insert(m_draggedPeerId, contactCustomGroupName(groupKey));
            } else {
                m_peerGroups.remove(m_draggedPeerId);
            }
            rebuildContactList();
            saveSettings();
            dropEvent->acceptProposedAction();
            m_draggedPeerId.clear();
            return true;
        }
    }

    if (m_peerList && watched == m_peerList->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_peerList->itemAt(mouseEvent->pos());
        if (!item) {
            m_draggedPeerId.clear();
            m_peerList->clearSelection();
            m_peerList->setCurrentItem(nullptr);
        } else if (item->data(ContactItemTypeRole).toString() == QStringLiteral("group") && mouseEvent->button() == Qt::LeftButton) {
            m_draggedPeerId.clear();
            const QString groupKey = item->data(ContactItemIdRole).toString();
            m_groupCollapsed.insert(groupKey, !m_groupCollapsed.value(groupKey, false));
            rebuildContactList();
            saveSettings();
            return true;
        } else if (item->data(ContactItemTypeRole).toString() == QStringLiteral("peer")) {
            m_draggedPeerId = item->data(ContactItemIdRole).toString();
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Blinq Messenger"));
    resize(380, 590);
    setFixedSize(380, 590);
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    applyTheme();

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *profile = new QWidget(root);
    profile->setObjectName(QStringLiteral("ProfilePanel"));
    auto *profileLayout = new QHBoxLayout(profile);
    profileLayout->setContentsMargins(10, 10, 10, 10);
    profileLayout->setSpacing(10);

    m_localAvatar = new QLabel(profile);
    m_localAvatar->setObjectName(QStringLiteral("LocalAvatar"));
    m_localAvatar->setFixedSize(64, 64);
    m_localAvatar->setAlignment(Qt::AlignCenter);
    profileLayout->addWidget(m_localAvatar, 0, Qt::AlignTop);

    auto *profileText = new QWidget(profile);
    profileText->setObjectName(QStringLiteral("ProfileInfoPanel"));
    auto *profileTextLayout = new QVBoxLayout(profileText);
    profileTextLayout->setContentsMargins(8, 5, 8, 5);
    profileTextLayout->setSpacing(3);
    profileTextLayout->setAlignment(Qt::AlignVCenter);

    m_localName = new QLabel(profileText);
    m_localName->setObjectName(QStringLiteral("LocalNameLabel"));
    profileTextLayout->addWidget(m_localName);

    auto *pmEdit = new QLineEdit(profileText);
    pmEdit->setObjectName(QStringLiteral("LocalPersonalMessageEdit"));
    pmEdit->setPlaceholderText(tr("Share a personal message..."));
    pmEdit->setFocusPolicy(Qt::ClickFocus);
    profileTextLayout->addWidget(pmEdit);

    m_mediaLabel = new MarqueeLabel(profileText);
    m_mediaLabel->setObjectName(QStringLiteral("NowPlayingLabel"));
    m_mediaLabel->setFixedHeight(22);
    m_mediaLabel->hide();
    profileTextLayout->addWidget(m_mediaLabel);

    m_statusCombo = new QComboBox(profileText);
    m_statusCombo->addItems({tr("Available"), tr("Busy"), tr("Away"), tr("Do Not Disturb"), tr("Invisible")});
    m_statusCombo->setItemData(m_statusCombo->findText(tr("Invisible")),
                               tr("Stops announcing you on the LAN. You can still receive direct messages from contacts that already know your address."),
                               Qt::ToolTipRole);
    m_statusCombo->hide();
    profileTextLayout->addWidget(m_statusCombo);

    auto *statusButton = new QPushButton(profileText);
    statusButton->setObjectName(QStringLiteral("StatusButton"));
    statusButton->setFlat(true);
    statusButton->setCursor(Qt::PointingHandCursor);
    statusButton->setFocusPolicy(Qt::NoFocus);
    auto *statusMenu = new QMenu(statusButton);
    const QStringList statuses = {tr("Available"), tr("Busy"), tr("Away"), tr("Do Not Disturb"), tr("Invisible")};
    for (const QString &status : statuses) {
        auto *action = statusMenu->addAction(statusDotIcon(status), status);
        if (status == tr("Invisible")) {
            action->setToolTip(tr("Stops announcing you on the LAN. You can still receive direct messages from contacts that already know your address."));
        }
        connect(action, &QAction::triggered, this, [this, status] {
            m_statusCombo->setCurrentText(status);
        });
    }
    statusMenu->addSeparator();
    statusMenu->addAction(tr("Custom..."), this, [this] {
        bool ok = false;
        const QString status = QInputDialog::getText(this,
                                                     tr("Custom Status"),
                                                     tr("Status message:"),
                                                     QLineEdit::Normal,
                                                     m_statusCombo->currentText(),
                                                     &ok).trimmed();
        if (!ok || status.isEmpty()) {
            return;
        }
        if (m_statusCombo->findText(status) < 0) {
            m_statusCombo->addItem(status);
        }
        m_statusCombo->setCurrentText(status);
    });
    statusButton->setMenu(statusMenu);
    profileTextLayout->addWidget(statusButton);

    const auto updateStatusButton = [statusButton](const QString &text) {
        statusButton->setText(text);
        statusButton->setIcon(statusDotIcon(text));
    };
    connect(m_statusCombo, &QComboBox::currentTextChanged, statusButton, updateStatusButton);
    updateStatusButton(m_statusCombo->currentText());
    profileLayout->addWidget(profileText, 1);

    auto *displayNameButton = new QPushButton(profile);
    displayNameButton->setObjectName(QStringLiteral("ProfileNameButton"));
    displayNameButton->setIcon(QIcon(QStringLiteral(":/icons/assets/display_name.png")));
    displayNameButton->setIconSize(QSize(15, 15));
    displayNameButton->setFixedSize(26, 26);
    displayNameButton->setFlat(true);
    displayNameButton->setCursor(Qt::PointingHandCursor);
    displayNameButton->setFocusPolicy(Qt::NoFocus);
    displayNameButton->setToolTip(tr("Change display name"));
    connect(displayNameButton, &QPushButton::clicked, this, &MainWindow::changeDisplayName);
    profileLayout->addWidget(displayNameButton, 0, Qt::AlignTop);

    layout->addWidget(profile);

    auto *contactsHeader = new QWidget(root);
    contactsHeader->setObjectName(QStringLiteral("ContactsHeader"));
    auto *contactsHeaderLayout = new QHBoxLayout(contactsHeader);
    contactsHeaderLayout->setContentsMargins(0, 0, 0, 0);
    contactsHeaderLayout->setSpacing(8);
    m_contactsLabel = new QLabel(root);
    m_contactsLabel->setObjectName(QStringLiteral("ContactsLabel"));
    m_contactsTotalLabel = new QLabel(root);
    m_contactsTotalLabel->setObjectName(QStringLiteral("ContactsTotalLabel"));
    m_contactsTotalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    contactsHeaderLayout->addWidget(m_contactsLabel, 1);
    contactsHeaderLayout->addWidget(m_contactsTotalLabel, 0);
    layout->addWidget(contactsHeader);

    m_peerList = new QListWidget(root);
    m_peerList->setIconSize(QSize(48, 48));
    m_peerList->setUniformItemSizes(false);
    m_peerList->setSpacing(2);
    m_peerList->setDragEnabled(true);
    m_peerList->setAcceptDrops(true);
    m_peerList->viewport()->setAcceptDrops(true);
    m_peerList->setDropIndicatorShown(true);
    m_peerList->setDragDropMode(QAbstractItemView::DragDrop);
    m_peerList->viewport()->installEventFilter(this);

    auto *contactsStack = new QWidget(root);
    auto *contactsStackLayout = new QStackedLayout(contactsStack);
    contactsStackLayout->setStackingMode(QStackedLayout::StackAll);
    contactsStackLayout->setContentsMargins(0, 0, 0, 0);
    contactsStackLayout->addWidget(m_peerList);
    m_emptyContactsLabel = new QLabel(tr("Loading Blinq Messenger..."), contactsStack);
    m_emptyContactsLabel->setAlignment(Qt::AlignCenter);
    m_emptyContactsLabel->setObjectName(QStringLiteral("EmptyContactsLabel"));
    contactsStackLayout->addWidget(m_emptyContactsLabel);
    layout->addWidget(contactsStack, 1);

    setCentralWidget(root);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:1 #edf6ff);"
        " border-top: 1px solid #cbdff2; color: #334155; padding: 2px 8px; }"
        "QStatusBar::item { border: none; }"
        "QLabel#NetworkStatusLabel { color: #334155; font-size: 11px; font-weight: 650; background: transparent; padding: 2px 0px; }"));
    m_networkStatus = new QLabel(statusBar());
    m_networkStatus->setObjectName(QStringLiteral("NetworkStatusLabel"));
    m_networkStatus->setTextFormat(Qt::PlainText);
    m_networkStatus->setMinimumHeight(20);
    statusBar()->addWidget(m_networkStatus, 1);
    updateNetworkStatus();
    m_peerList->setFocus(Qt::OtherFocusReason);
    updateEmptyContactsLabel();
    updateContactsLabel();
}

void MainWindow::buildMenus()
{
    auto *profileMenu = menuBar()->addMenu(tr("&Profile"));
    auto *changeName = profileMenu->addAction(QIcon(QStringLiteral(":/icons/assets/display_name.png")), tr("Change Display Name"));
    auto *changeAvatar = profileMenu->addAction(QIcon(QStringLiteral(":/icons/assets/avatar.png")), tr("Set Avatar"));
    auto *settingsAction = profileMenu->addAction(QIcon(QStringLiteral(":/icons/assets/settings.png")), tr("Settings"));
    auto *switchModeAction = profileMenu->addAction(QIcon(QStringLiteral(":/icons/assets/reconnect.png")),
                                                    isInternetMode() ? tr("Switch to LAN Mode") : tr("Switch to Internet Mode"));
    profileMenu->addSeparator();
    auto *quit = profileMenu->addAction(QIcon(QStringLiteral(":/icons/assets/exit.png")), tr("Quit"));

    auto *chatMenu = menuBar()->addMenu(tr("&Chat"));
    auto *publicChatAction = chatMenu->addAction(QIcon(QStringLiteral(":/icons/assets/public_chat.png")), tr("Public Chat"));
    auto *addInternetContactAction = chatMenu->addAction(QIcon(QStringLiteral(":/icons/assets/add.png")), tr("Add Contact..."));
    auto *clearPublicHistoryAction = chatMenu->addAction(QIcon(QStringLiteral(":/icons/assets/clear_history.png")), tr("Clear Public Chat History"));
    auto *directConnectAction = chatMenu->addAction(QIcon(QStringLiteral(":/icons/assets/direct_connect.png")), tr("Direct Connect by IP"));
    publicChatAction->setVisible(!isInternetMode());
    clearPublicHistoryAction->setVisible(!isInternetMode());
    directConnectAction->setVisible(!isInternetMode());
    addInternetContactAction->setVisible(isInternetMode());
    m_muteSoundsAction = chatMenu->addAction(QIcon(m_settings.muteSounds ? QStringLiteral(":/icons/assets/unmute.png") : QStringLiteral(":/icons/assets/mute.png")),
                                             m_settings.muteSounds ? tr("Unmute Sounds") : tr("Mute Sounds"));
    auto *lanToolsSeparator = chatMenu->addSeparator();
    lanToolsSeparator->setVisible(!isInternetMode());
    auto *manageGroupsAction = chatMenu->addAction(QIcon(QStringLiteral(":/icons/assets/groups.png")), tr("Manage Contact Groups..."));
    manageGroupsAction->setVisible(!isInternetMode());
    manageGroupsAction->setIconVisibleInMenu(true);
    auto *networkHelpMenu = chatMenu->addMenu(QIcon(QStringLiteral(":/icons/assets/connection_info.png")), tr("Network Help"));
    networkHelpMenu->menuAction()->setVisible(!isInternetMode());
    auto *networkDiagnosticsAction = networkHelpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/connection_info.png")), tr("Network Diagnostics"));
    auto *copyInviteAction = networkHelpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/copy.png")), tr("Copy Connection Invite"));
    auto *reconnectSavedAction = networkHelpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/reconnect.png")), tr("Reconnect Saved Contacts"));
    auto *scanNetworkAction = networkHelpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/scan.png")), tr("Scan Local Network"));
    auto *firewallHelperAction = networkHelpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/firewall.png")), tr("Firewall Setup Helper"));
    networkHelpMenu->menuAction()->setIconVisibleInMenu(true);
    networkDiagnosticsAction->setIconVisibleInMenu(true);
    copyInviteAction->setIconVisibleInMenu(true);
    reconnectSavedAction->setIconVisibleInMenu(true);
    scanNetworkAction->setIconVisibleInMenu(true);
    firewallHelperAction->setIconVisibleInMenu(true);
    auto *afterLanToolsSeparator = chatMenu->addSeparator();
    afterLanToolsSeparator->setVisible(!isInternetMode());

    auto *themeMenu = menuBar()->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    const QString currentThemeKey = themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString();
    const QString effectiveThemeKey = appThemeForKey(currentThemeKey).key;
    for (const AppTheme &theme : appThemes()) {
        auto *themeAction = themeMenu->addAction(themeSwatchIcon(theme), theme.label);
        themeAction->setCheckable(true);
        themeAction->setChecked(theme.key == effectiveThemeKey);
        themeAction->setData(theme.key);
        themeGroup->addAction(themeAction);
    }
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *getHelp = helpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/help.png")), tr("Get Help"));
    auto *checkUpdates = helpMenu->addAction(tr("Check for Updates"));
    auto *about = helpMenu->addAction(QIcon(QStringLiteral(":/icons/assets/about.png")), tr("About Blinq Messenger"));

    connect(changeName, &QAction::triggered, this, &MainWindow::changeDisplayName);
    connect(changeAvatar, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this,
                                                          tr("Set Avatar"),
                                                          QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty()) {
            if (isInternetMode()) {
                QImage image(path);
                if (image.isNull()) {
                    statusBar()->showMessage(tr("Could not load that avatar image."), 7000);
                    return;
                }
                const int squareSize = qMin(image.width(), image.height());
                const QRect cropRect((image.width() - squareSize) / 2, (image.height() - squareSize) / 2, squareSize, squareSize);
                image = image.copy(cropRect).scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                QByteArray bytes;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "PNG");
                QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
                const QString themeKey = themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString();
                m_internetRelay->setProfile(m_settings.internetDisplayName, bytes, appThemeForKey(themeKey).accent);
                updateLocalProfile();
                return;
            }
            m_chatService->setLocalAvatar(path);
            updateLocalProfile();
        }
    });
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(switchModeAction, &QAction::triggered, this, &MainWindow::switchAppMode);
    connect(quit, &QAction::triggered, this, [this] {
        m_reallyQuit = true;
        qApp->quit();
    });
    connect(publicChatAction, &QAction::triggered, this, &MainWindow::openPublicChat);
    connect(addInternetContactAction, &QAction::triggered, this, &MainWindow::showAddInternetContactDialog);
    connect(directConnectAction, &QAction::triggered, this, &MainWindow::showDirectConnectDialog);
    connect(clearPublicHistoryAction, &QAction::triggered, this, [this] {
        if (m_publicChatWindow) {
            m_publicChatWindow->clearHistory();
        } else {
            PublicChatWindow::clearSavedHistory();
        }
        statusBar()->showMessage(tr("Public chat history cleared."), 5000);
    });
    connect(m_muteSoundsAction, &QAction::triggered, this, [this] {
        setSoundsMuted(!m_settings.muteSounds);
    });
    connect(themeGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
        const QString themeKey = action->data().toString();
        settings.setValue(QStringLiteral("app/themeName"), themeKey);
        settings.remove(QStringLiteral("app/themeGradient"));
        settings.remove(QStringLiteral("app/customThemeColor"));
        m_chatService->setLocalThemeColor(appThemeForKey(themeKey).accent);
        if (isInternetMode() && m_internetRelay->isAuthenticated()) {
            const QString displayName = m_settings.internetDisplayName.isEmpty()
                                            ? m_internetRelay->self().displayName
                                            : m_settings.internetDisplayName;
            m_internetRelay->setProfile(displayName, QByteArray(), appThemeForKey(themeKey).accent);
        }
        applyTheme();
    });
    connect(copyInviteAction, &QAction::triggered, this, &MainWindow::showConnectionInviteDialog);
    connect(reconnectSavedAction, &QAction::triggered, this, &MainWindow::reconnectSavedContacts);
    connect(scanNetworkAction, &QAction::triggered, this, &MainWindow::scanLocalSubnet);
    connect(firewallHelperAction, &QAction::triggered, this, &MainWindow::showFirewallHelperDialog);
    connect(manageGroupsAction, &QAction::triggered, this, &MainWindow::showGroupManager);
    connect(networkDiagnosticsAction, &QAction::triggered, this, &MainWindow::showConnectionInfo);
    connect(getHelp, &QAction::triggered, this, &MainWindow::showHelp);
    connect(checkUpdates, &QAction::triggered, this, &MainWindow::showUpdateDialog);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildTrayIcon()
{
    m_trayMenu = new QMenu(this);
    auto *showAction = m_trayMenu->addAction(QIcon(QStringLiteral(":/icons/assets/appicon.ico")), tr("Show Blinq Messenger"));
    auto *connectionInfoAction = m_trayMenu->addAction(QIcon(QStringLiteral(":/icons/assets/connection_info.png")), tr("Network Diagnostics"));
    auto *settingsAction = m_trayMenu->addAction(QIcon(QStringLiteral(":/icons/assets/settings.png")), tr("Settings"));
    auto *statusMenu = m_trayMenu->addMenu(tr("Status"));
    const QStringList statuses = {tr("Available"), tr("Busy"), tr("Away"), tr("Do Not Disturb"), tr("Invisible")};
    for (const QString &status : statuses) {
        auto *action = statusMenu->addAction(statusDotIcon(status), status);
        connect(action, &QAction::triggered, this, [this, status] {
            if (m_statusCombo->findText(status) < 0) {
                m_statusCombo->addItem(status);
            }
            m_statusCombo->setCurrentText(status);
        });
    }
    statusMenu->addSeparator();
    statusMenu->addAction(tr("Custom..."), this, [this] {
        bool ok = false;
        const QString status = QInputDialog::getText(this,
                                                     tr("Custom Status"),
                                                     tr("Status message:"),
                                                     QLineEdit::Normal,
                                                     m_statusCombo->currentText(),
                                                     &ok).trimmed();
        if (!ok || status.isEmpty()) {
            return;
        }
        if (m_statusCombo->findText(status) < 0) {
            m_statusCombo->addItem(status);
        }
        m_statusCombo->setCurrentText(status);
    });
    m_trayMenu->addSeparator();
    auto *quitAction = m_trayMenu->addAction(QIcon(QStringLiteral(":/icons/assets/exit.png")), tr("Quit"));

    connect(showAction, &QAction::triggered, this, [this] {
        showFromTray();
    });
    connect(connectionInfoAction, &QAction::triggered, this, &MainWindow::showConnectionInfo);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(quitAction, &QAction::triggered, this, [this] {
        m_reallyQuit = true;
        qApp->quit();
    });

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayMenu);
    updateTrayTooltip();
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showFromTray();
        }
    });
    connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, &MainWindow::openPendingNotification);
}

void MainWindow::connectSignals()
{
    connect(m_statusCombo, &QComboBox::currentTextChanged, m_chatService, &LanChatService::setLocalStatus);
    connect(m_statusCombo, &QComboBox::currentTextChanged, this, [this](const QString &status) {
        if (isInternetMode() && m_internetRelay->isAuthenticated()) {
            m_internetRelay->setPresence(status, m_manualPersonalMessage);
        }
    });
    if (auto *pmEdit = findChild<QLineEdit*>(QStringLiteral("LocalPersonalMessageEdit"))) {
        connect(pmEdit, &QLineEdit::editingFinished, this, [this, pmEdit] {
            m_manualPersonalMessage = pmEdit->text().trimmed();
            if (m_manualPersonalMessage.isEmpty()) {
                m_manualPersonalMessage = tr("Hi, let's chat!");
                pmEdit->setText(m_manualPersonalMessage);
            }
            saveSettings();
            if (m_currentMediaText.isEmpty()) {
                m_chatService->setLocalPersonalMessage(m_manualPersonalMessage);
            }
            if (isInternetMode() && m_internetRelay->isAuthenticated()) {
                m_internetRelay->setPresence(m_statusCombo->currentText(), m_manualPersonalMessage);
            }
        });
        connect(pmEdit, &QLineEdit::returnPressed, pmEdit, &QLineEdit::clearFocus);
    }
    connect(m_statusCombo, &QComboBox::currentTextChanged, this, &MainWindow::updateTrayTooltip);
    connect(m_chatService, &LanChatService::peerJoined, this, [this](const ChatPeer &peer) {
        const bool alreadyKnown = m_knownPeers.contains(peer.id);
        upsertPeer(peer);
        if (!alreadyKnown) {
            showSignInNotification(peer);
        }
    });
    connect(m_chatService, &LanChatService::peerUpdated, this, &MainWindow::upsertPeer);
    connect(m_chatService, &LanChatService::peerLeft, this, &MainWindow::removePeer);
    connect(m_chatService, &LanChatService::messageReceived, this, [this](const QString &peerId, const QString &peerName, const QString &message, const QString &messageId, bool isHtml) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        auto *window = chatWindowFor(peerId);
        markChatActive();
        const bool notify = window->shouldNotifyForIncoming();
        
        if (notify) {
            auto unreadCounts = property("unreadCounts").toHash();
            const int count = unreadCounts.value(peerId).toInt() + 1;
            unreadCounts.insert(peerId, count);
            setProperty("unreadCounts", unreadCounts);
            if (m_peers.contains(peerId)) {
                upsertPeer(m_peers.value(peerId));
            }
        }
        
        window->appendIncomingMessage(peerName, message, messageId, isHtml);
        
        if (!messageId.isEmpty()) {
            if (notify) {
                m_chatService->sendMessageReceipt(peerId, messageId, QStringLiteral("Delivered"));
                m_pendingReadReceipts[peerId].append(messageId);
            } else {
                m_chatService->sendMessageReceipt(peerId, messageId, QStringLiteral("Read"));
            }
        }
        maybeSendAwayAutoReply(peerId, message, isHtml);
        if (m_settings.openChatOnMessage) {
            window->showNormal();
            window->raise();
            window->activateWindow();
            playReceivedSound();
        } else if (notify) {
            QString notificationMessage = message;
            if (isHtml) {
                QTextDocument document;
                document.setHtml(message);
                notificationMessage = document.toPlainText();
            }
            if (showIncomingNotification(m_peers.value(peerId), notificationMessage)) {
                playNotificationSound();
            } else {
                playReceivedSound();
            }
        } else {
            playReceivedSound();
        }
    });
    connect(m_chatService, &LanChatService::messageSent, this, [this](const QString &peerId, const QString &message, const QString &messageId, bool isHtml) {
        markChatActive();
        chatWindowFor(peerId)->appendOutgoingMessage(message, messageId, isHtml);
        
        playSentSound();
    });
    connect(m_chatService, &LanChatService::messageReceiptReceived, this, [this](const QString &peerId, const QString &messageId, const QString &status) {
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->updateMessageStatus(messageId, status);
        }
    });
    connect(m_chatService, &LanChatService::typingStateReceived, this, [this](const QString &peerId, bool isTyping) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        if (isTyping) {
            m_typingPeers.insert(peerId);
        } else {
            m_typingPeers.remove(peerId);
        }
        if (m_knownPeers.contains(peerId)) {
            rebuildContactList();
        }
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->setPeerTyping(isTyping);
        }
    });
    connect(m_chatService, &LanChatService::fileReceived, this, [this](const QString &peerId, const QString &peerName, const QString &fileName, const QByteArray &data, bool isImage) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        auto *window = chatWindowFor(peerId);
        markChatActive();
        const bool notify = window->shouldNotifyForIncoming();

        if (notify) {
            auto unreadCounts = property("unreadCounts").toHash();
            const int count = unreadCounts.value(peerId).toInt() + 1;
            unreadCounts.insert(peerId, count);
            setProperty("unreadCounts", unreadCounts);
            if (m_peers.contains(peerId)) {
                upsertPeer(m_peers.value(peerId));
            }
        }

        if (!isImage) {
            window->appendFileTransferStarted(peerName, fileName);
        }
        window->appendIncomingFile(peerName, fileName, data);
        if (m_settings.openChatOnMessage) {
            window->showNormal();
            window->raise();
            window->activateWindow();
            playReceivedSound();
        } else if (notify) {
            if (showIncomingNotification(m_peers.value(peerId), isImage ? tr("Sent an image: %1").arg(fileName) : tr("Sent a file: %1").arg(fileName))) {
                playNotificationSound();
            } else {
                playReceivedSound();
            }
        } else {
            playReceivedSound();
        }
    });
    connect(m_chatService, &LanChatService::fileSent, this, [this](const QString &peerId, const QString &fileName, const QString &filePath, bool isImage) {
        markChatActive();
        chatWindowFor(peerId)->appendOutgoingFile(fileName, filePath, isImage);
        playSentSound();
    });
    connect(m_chatService, &LanChatService::buzzReceived, this, [this](const QString &peerId, const QString &peerName) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        if (!allowRateLimitedAction(QStringLiteral("incoming-buzz:%1").arg(peerId), 2500)) {
            return;
        }
        if (showIncomingNotification(m_peers.value(peerId), tr("%1 is whistling at you.").arg(peerName))) {
            playWhistleSound();
        } else {
            playWhistleSound();
        }
    });
    connect(m_chatService, &LanChatService::buzzSent, this, [this](const QString &peerId) {
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->appendSystemMessage(tr("You whistled."));
        }
        playWhistleSound();
    });
    connect(m_chatService, &LanChatService::statusChanged, this, [this] {
        m_isLoading = false;
        updateEmptyContactsLabel();
        updateNetworkStatus();
        if (m_trayIcon) {
            updateTrayTooltip();
        }
    });
    connect(m_chatService, &LanChatService::fileTooLarge, this, [this](const QString &fileName, int maxSizeMb) {
        QMessageBox::information(dialogParent(),
                                 tr("File Too Large"),
                                 tr("%1 is too large to send.\n\nThe current limit is %2 MB.").arg(fileName).arg(maxSizeMb));
    });
    connect(m_chatService, &LanChatService::manualConnectionSucceeded, this, [this](const QString &address, const QString &peerName) {
        const bool backgroundAttempt = m_backgroundConnectionAttempts > 0;
        if (backgroundAttempt) {
            --m_backgroundConnectionAttempts;
        }
        if (!m_settings.manualPeerAddresses.contains(address)) {
            m_settings.manualPeerAddresses.append(address);
            saveSettings();
        }
        statusBar()->showMessage(tr("Connected to %1 at %2.").arg(peerName.isEmpty() ? tr("Blinq Messenger") : peerName, address), 5000);
        updateNetworkStatus();
        const QString peerId = peerIdForManualConnection(address, peerName);
        const bool shouldOpenChat = (!backgroundAttempt && m_openNextManualConnectionChat)
                                    || (backgroundAttempt && m_openFirstBackgroundConnectionChat);
        if (shouldOpenChat && !peerId.isEmpty()) {
            m_openFirstBackgroundConnectionChat = false;
            openChat(peerId);
        }
        if (!backgroundAttempt) {
            m_openNextManualConnectionChat = true;
        }
    });
    connect(m_chatService, &LanChatService::manualConnectionFailed, this, [this](const QString &address, const QString &reason) {
        if (m_backgroundConnectionAttempts > 0) {
            --m_backgroundConnectionAttempts;
            if (m_backgroundConnectionAttempts == 0) {
                m_openNextManualConnectionChat = true;
                m_openFirstBackgroundConnectionChat = false;
                statusBar()->showMessage(tr("Background network checks finished."), 5000);
            }
            return;
        }
        m_openNextManualConnectionChat = true;
        if (m_directConnectDialogsOpen > 0) {
            return;
        }
        QMessageBox::warning(dialogParent(),
                             tr("Direct Connect"),
                             tr("Could not connect to %1.\n\n%2").arg(address, reason));
    });
    connect(m_chatService, &LanChatService::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 7000);
    });
    connect(m_internetRelay, &InternetRelayService::authenticated, this, [this](const QString &token, const InternetRelayPeer &self) {
        m_settings.internetAuthToken = token;
        m_settings.internetBlinqId = self.blinqId;
        m_settings.internetDisplayName = self.displayName;
        saveSettings();
        updateLocalProfile();
        m_isLoading = false;
        rebuildInternetContacts();
        updateNetworkStatus();
        statusBar()->showMessage(tr("Signed in as %1.").arg(self.blinqId), 6000);
        QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
        const QString themeKey = themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString();
        m_internetRelay->setProfile(m_settings.internetDisplayName, QByteArray(), appThemeForKey(themeKey).accent);
        const QString internetPersonalMessage = m_settings.showPlayingInfo && !m_currentMediaText.isEmpty()
                                                    ? tr("Now playing: %1").arg(m_currentMediaText)
                                                    : m_manualPersonalMessage;
        m_internetRelay->setPresence(m_statusCombo->currentText(), internetPersonalMessage);
        m_internetRelay->setSearchable(m_settings.internetSearchable);
        QTimer::singleShot(300, this, [this] {
            for (const InternetContactRequest &request : m_internetRelay->contactRequests()) {
                const QString name = request.fromUser.displayName.isEmpty() ? request.fromUser.blinqId : request.fromUser.displayName;
                if (QMessageBox::question(dialogParent(),
                                          tr("Contact Request"),
                                          tr("%1 wants to add you as a contact.").arg(name),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::Yes)
                    == QMessageBox::Yes) {
                    m_internetRelay->acceptContact(request.id);
                } else {
                    m_internetRelay->rejectContact(request.id);
                }
            }
        });
    });
    connect(m_internetRelay, &InternetRelayService::disconnectedFromServer, this, [this] {
        if (isInternetMode() && !m_reallyQuit) {
            handleInternetServerUnavailable(tr("Disconnected from the Blinq Internet server."));
            return;
        }
        statusBar()->showMessage(tr("Disconnected from Blinq server."), 5000);
        for (auto it = m_knownPeers.begin(); it != m_knownPeers.end(); ++it) {
            it->status = tr("Offline");
        }
        m_peers.clear();
        rebuildContactList();
        updateContactsLabel();
        updateEmptyContactsLabel();
    });
    connect(m_internetRelay, &InternetRelayService::connectionFailed, this, [this](const QString &reason) {
        statusBar()->showMessage(tr("Blinq server connection failed: %1").arg(reason), 7000);
        if (isInternetMode() && !m_internetRelay->isAuthenticated() && !m_authDialogOpen) {
            showInternetSignInDialog();
        }
    });
    connect(m_internetRelay, &InternetRelayService::serverUnavailable, this, [this](const QString &reason) {
        handleInternetServerUnavailable(tr("Could not connect to the Blinq Internet server:\n%1").arg(reason));
    });
    connect(m_internetRelay, &InternetRelayService::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 5000);
    });
    connect(m_internetRelay, &InternetRelayService::accountDeleted, this, [this] {
        m_settings.internetAuthToken.clear();
        m_settings.internetBlinqId.clear();
        m_settings.internetDisplayName.clear();
        m_settings.appMode = QStringLiteral("lan");
        saveSettings();
        restartApplication();
    });
    connect(m_internetRelay, &InternetRelayService::contactsChanged, this, &MainWindow::rebuildInternetContacts);
    connect(m_internetRelay, &InternetRelayService::contactRequestReceived, this, [this](const InternetContactRequest &request) {
        const QString name = request.fromUser.displayName.isEmpty() ? request.fromUser.blinqId : request.fromUser.displayName;
        if (QMessageBox::question(dialogParent(),
                                  tr("Contact Request"),
                                  tr("%1 wants to add you as a contact.").arg(name),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::Yes)
            == QMessageBox::Yes) {
            m_internetRelay->acceptContact(request.id);
        } else {
            m_internetRelay->rejectContact(request.id);
        }
    });
    connect(m_internetRelay, &InternetRelayService::presenceReceived, this, [this](const InternetRelayPeer &peer) {
        if (peer.id == m_internetRelay->localId()) {
            m_settings.internetDisplayName = peer.displayName;
            saveSettings();
            updateLocalProfile();
            return;
        }
        upsertInternetPeer(peer);
        rebuildContactList();
        updateContactsLabel();
        updateEmptyContactsLabel();
    });
    connect(m_internetRelay, &InternetRelayService::messageReceived, this, [this](const QString &peerId, const QString &peerName, const QString &message, const QString &messageId, bool isHtml) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        auto *window = chatWindowFor(peerId);
        const bool notify = window->shouldNotifyForIncoming();
        if (notify) {
            auto unreadCounts = property("unreadCounts").toHash();
            const int count = unreadCounts.value(peerId).toInt() + 1;
            unreadCounts.insert(peerId, count);
            setProperty("unreadCounts", unreadCounts);
            rebuildContactList();
        }
        window->appendIncomingMessage(peerName, message, messageId, isHtml);
        if (!messageId.isEmpty()) {
            m_internetRelay->sendReceipt(peerId, messageId, notify ? QStringLiteral("Delivered") : QStringLiteral("Read"));
            if (notify) {
                m_pendingReadReceipts[peerId].append(messageId);
            }
        }
        const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
        if (notify && showIncomingNotification(peer, message)) {
            playNotificationSound();
        } else {
            playReceivedSound();
        }
    });
    connect(m_internetRelay, &InternetRelayService::imageReceived, this, [this](const QString &peerId, const QString &peerName, const QString &fileName, const QByteArray &data) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        auto *window = chatWindowFor(peerId);
        const bool notify = window->shouldNotifyForIncoming();
        if (notify) {
            auto unreadCounts = property("unreadCounts").toHash();
            const int count = unreadCounts.value(peerId).toInt() + 1;
            unreadCounts.insert(peerId, count);
            setProperty("unreadCounts", unreadCounts);
            rebuildContactList();
        }
        window->appendIncomingFile(peerName, fileName, data);
        const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
        if (notify && showIncomingNotification(peer, tr("Sent an image: %1").arg(fileName))) {
            playNotificationSound();
        } else {
            playReceivedSound();
        }
    });
    connect(m_internetRelay, &InternetRelayService::messageSent, this, [this](const QString &peerId, const QString &message, const QString &messageId, bool isHtml) {
        chatWindowFor(peerId)->appendOutgoingMessage(message, messageId, isHtml);
        playSentSound();
    });
    connect(m_internetRelay, &InternetRelayService::imageSent, this, [this](const QString &peerId, const QString &fileName, const QString &filePath) {
        chatWindowFor(peerId)->appendOutgoingFile(fileName, filePath, true);
        playSentSound();
    });
    connect(m_internetRelay, &InternetRelayService::typingStateReceived, this, [this](const QString &peerId, bool isTyping) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        if (isTyping) {
            m_typingPeers.insert(peerId);
        } else {
            m_typingPeers.remove(peerId);
        }
        rebuildContactList();
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->setPeerTyping(isTyping);
        }
    });
    connect(m_internetRelay, &InternetRelayService::receiptReceived, this, [this](const QString &peerId, const QString &messageId, const QString &status) {
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->updateMessageStatus(messageId, status);
        }
    });
    connect(m_internetRelay, &InternetRelayService::buzzReceived, this, [this](const QString &peerId, const QString &peerName) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        if (!allowRateLimitedAction(QStringLiteral("incoming-internet-buzz:%1").arg(peerId), 2500)) {
            return;
        }
        const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
        if (showIncomingNotification(peer, tr("%1 is whistling at you.").arg(peerName))) {
            playWhistleSound();
        } else {
            playWhistleSound();
        }
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->appendSystemMessage(tr("%1 whistled.").arg(peerName));
        }
    });
    connect(m_internetRelay, &InternetRelayService::buzzSent, this, [this](const QString &peerId) {
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->appendSystemMessage(tr("You whistled."));
        }
        playWhistleSound();
    });

    connect(m_chatService, &LanChatService::fileTransferOffered, this, [this](const QString &peerId, const QString &peerName, const QString &fileId, const QString &fileName, qint64 fileSize) {
        QString sizeStr;
        if (fileSize < 1024 * 1024) {
            sizeStr = tr("%1 KB").arg(fileSize / 1024);
        } else {
            sizeStr = tr("%1 MB").arg(static_cast<double>(fileSize) / (1024.0 * 1024.0));
        }
        const QString text = tr("%1 wants to send a file:\n%2 (%3)\n\nAccept this file?").arg(peerName, fileName, sizeStr);
        
        QMetaObject::invokeMethod(this, [this, peerId, fileId, text] {
            if (QMessageBox::question(dialogParent(), tr("Incoming File"), text, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                m_chatService->acceptFileTransfer(peerId, fileId);
            } else {
                m_chatService->rejectFileTransfer(peerId, fileId);
            }
        }, Qt::QueuedConnection);
    });
    connect(m_chatService, &LanChatService::fileTransferStarted, this, [this](const QString &peerId, const QString &fileId, const QString &fileName, int totalChunks, bool isSending) {
        if (auto *window = chatWindowFor(peerId)) {
            window->addTransferUi(fileId, fileName, totalChunks, isSending);
        }
    });
    connect(m_chatService, &LanChatService::fileTransferProgress, this, [this](const QString &peerId, const QString &fileId, int currentChunk) {
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->updateTransferUi(fileId, currentChunk);
        }
    });
    connect(m_chatService, &LanChatService::fileTransferFinished, this, [this](const QString &peerId, const QString &fileId, bool success, const QString &error) {
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->removeTransferUi(fileId);
            if (!success && !error.isEmpty()) {
                window->appendSystemMessage(tr("File transfer canceled: %1").arg(error));
            }
        }
    });

    connect(m_peerList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item && item->data(ContactItemTypeRole).toString() == QStringLiteral("peer")) {
            openChat(item->data(ContactItemIdRole).toString());
        }
    });
    m_peerList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_peerList, &QListWidget::customContextMenuRequested, this, &MainWindow::showPeerContextMenu);
    connect(m_chatService, &LanChatService::publicMessageReceived, this, [this](const QString &peerId, const QString &peerName, const QString &message, const QString &messageId, bool isHtml) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        if (!allowRateLimitedAction(QStringLiteral("incoming-public:%1").arg(peerId), 600)) {
            return;
        }
        auto *window = m_publicChatWindow;
        if (!window || !m_peers.contains(peerId)) {
            return;
        }
        const bool notify = window->shouldNotifyForIncoming();
        window->appendIncomingMessage(peerName, message, messageId, isHtml);
        if (m_settings.openChatOnMessage) {
            window->showNormal();
            window->raise();
            window->activateWindow();
            playReceivedSound();
        } else if (notify) {
            QString notificationMessage = message;
            if (isHtml) {
                QTextDocument document;
                document.setHtml(message);
                notificationMessage = document.toPlainText();
            }
            if (showPublicIncomingNotification(peerName, notificationMessage)) {
                playNotificationSound();
            } else {
                playReceivedSound();
            }
        } else {
            playReceivedSound();
        }
    });
    connect(m_chatService, &LanChatService::publicMessageSent, this, [this](const QString &message, const QString &messageId, bool isHtml) {
        openPublicChat();
        m_publicChatWindow->appendOutgoingMessage(message, messageId, isHtml);
        playSentSound();
    });
    connect(m_chatService, &LanChatService::privateChatRejected, this, [this](const QString &peerId, const QString &peerName, const QString &reason) {
        if (m_blockedPeers.contains(peerId)) {
            return;
        }
        const QString name = peerName.isEmpty() ? tr("This contact") : peerName;
        if (auto *window = m_chatWindows.value(peerId, nullptr)) {
            window->appendSystemMessage(tr("%1 rejected the private interaction: %2").arg(name, reason));
        }
        statusBar()->showMessage(tr("%1 rejected the private interaction.").arg(name), 6000);
        if (!m_chatWindows.contains(peerId)) {
            QMessageBox::information(dialogParent(),
                                     tr("Private Chat Not Available"),
                                     tr("%1 rejected the private interaction.\n\n%2").arg(name, reason));
        }
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (auto *pmEdit = findChild<QLineEdit*>(QStringLiteral("LocalPersonalMessageEdit"))) {
        m_manualPersonalMessage = pmEdit->text().trimmed().isEmpty() ? tr("Hi, let's chat!") : pmEdit->text().trimmed();
        saveSettings();
        if (m_currentMediaText.isEmpty() || !m_settings.showPlayingInfo) {
            m_chatService->setLocalPersonalMessage(m_manualPersonalMessage);
            if (isInternetMode() && m_internetRelay->isAuthenticated()) {
                m_internetRelay->setPresence(m_statusCombo->currentText(), m_manualPersonalMessage);
            }
        }
    }
    if (m_reallyQuit || !m_settings.minimizeToTray || !m_trayIcon || !m_trayIcon->isVisible()) {
        event->accept();
        m_reallyQuit = true;
        qApp->quit();
        return;
    }

    hide();
    event->ignore();
}

void MainWindow::loadSettings()
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    m_settings.showNotifications = settings.value(QStringLiteral("app/showNotifications"), true).toBool();
    m_settings.directMessageNotifications = settings.value(QStringLiteral("app/directMessageNotifications"), true).toBool();
    m_settings.publicChatNotifications = settings.value(QStringLiteral("app/publicChatNotifications"), true).toBool();
    m_settings.minimizeToTray = settings.value(QStringLiteral("app/minimizeToTray"), true).toBool();
    m_settings.openChatOnMessage = settings.value(QStringLiteral("app/openChatOnMessage"), false).toBool();
    m_settings.launchWithWindows = settings.value(QStringLiteral("app/launchWithWindows"), false).toBool();
    m_settings.saveHistory = settings.value(QStringLiteral("app/saveHistory"), true).toBool();
    m_settings.muteSounds = settings.value(QStringLiteral("app/muteSounds"), false).toBool();
    m_settings.showPlayingInfo = settings.value(QStringLiteral("app/showPlayingInfo"), true).toBool();
    m_settings.hideTypingIndicator = settings.value(QStringLiteral("app/hideTypingIndicator"), false).toBool();
    m_settings.internetSearchable = settings.value(QStringLiteral("internet/searchable"), true).toBool();
    m_settings.awayAutoReply = settings.value(QStringLiteral("app/awayAutoReply"), false).toBool();
    m_settings.awayAutoReplyMessage = settings.value(QStringLiteral("app/awayAutoReplyMessage"),
                                                     tr("I'm away right now. I'll reply when I'm back.")).toString();
    if (m_settings.awayAutoReplyMessage.trimmed().isEmpty()) {
        m_settings.awayAutoReplyMessage = tr("I'm away right now. I'll reply when I'm back.");
    }
    m_manualPersonalMessage = settings.value(QStringLiteral("app/manualPersonalMessage")).toString();
    m_settings.appMode = settings.value(QStringLiteral("app/mode"), QStringLiteral("lan")).toString() == QStringLiteral("internet")
                             ? QStringLiteral("internet")
                             : QStringLiteral("lan");
    m_settings.internetServerHost = settings.value(QStringLiteral("internet/serverHost"), QStringLiteral("66.154.104.66")).toString();
    m_settings.internetServerPort = settings.value(QStringLiteral("internet/serverPort"), 45476).toInt();
    if (m_settings.internetServerPort < 1 || m_settings.internetServerPort > 65535) {
        m_settings.internetServerPort = 45476;
    }
    m_settings.internetAuthToken = decryptLocalSecret(settings.value(QStringLiteral("internet/authTokenEncrypted")).toString());
    if (m_settings.internetAuthToken.isEmpty()) {
        m_settings.internetAuthToken = settings.value(QStringLiteral("internet/authToken")).toString();
        if (!m_settings.internetAuthToken.isEmpty()) {
            settings.setValue(QStringLiteral("internet/authTokenEncrypted"), encryptLocalSecret(m_settings.internetAuthToken));
            settings.remove(QStringLiteral("internet/authToken"));
        }
    }
    m_settings.internetBlinqId = settings.value(QStringLiteral("internet/blinqId")).toString();
    m_settings.internetDisplayName = settings.value(QStringLiteral("internet/displayName")).toString();
    m_settings.manualPeerAddresses = settings.value(QStringLiteral("app/manualPeerAddresses")).toStringList();
    m_settings.manualPeerAddresses.removeDuplicates();
    const QStringList blockedPeers = settings.value(QStringLiteral("app/blockedPeers")).toStringList();
    m_blockedPeers.clear();
    m_blockedPeerNames.clear();
    for (const QString &peerId : blockedPeers) {
        m_blockedPeers.insert(peerId);
        const QString name = settings.value(QStringLiteral("app/blockedPeerNames/%1").arg(peerId), peerId).toString();
        m_blockedPeerNames.insert(peerId, name);
        m_settings.blockedPeers.insert(peerId, name);
    }
    m_groupCollapsed.clear();
    for (const QString &groupKey : settings.value(QStringLiteral("app/collapsedContactGroups")).toStringList()) {
        m_groupCollapsed.insert(groupKey, true);
    }
    m_favoritePeers.clear();
    for (const QString &peerId : settings.value(QStringLiteral("app/favoritePeers")).toStringList()) {
        if (!peerId.isEmpty()) {
            m_favoritePeers.insert(peerId);
        }
    }
    m_customContactGroups = settings.value(QStringLiteral("app/customContactGroups")).toStringList();
    m_customContactGroups.removeDuplicates();
    m_peerGroups.clear();
    for (const QString &peerId : settings.value(QStringLiteral("app/peerGroupIds")).toStringList()) {
        const QString groupName = settings.value(QStringLiteral("app/peerGroups/%1").arg(peerId)).toString();
        if (!peerId.isEmpty() && !groupName.isEmpty() && m_customContactGroups.contains(groupName)) {
            m_peerGroups.insert(peerId, groupName);
        }
    }
    if (!isInternetMode()) {
        loadKnownPeers(settings);
        loadOfflineMessages(settings);
    }
}

void MainWindow::saveSettings() const
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    settings.setValue(QStringLiteral("app/showNotifications"), m_settings.showNotifications);
    settings.setValue(QStringLiteral("app/directMessageNotifications"), m_settings.directMessageNotifications);
    settings.setValue(QStringLiteral("app/publicChatNotifications"), m_settings.publicChatNotifications);
    settings.setValue(QStringLiteral("app/minimizeToTray"), m_settings.minimizeToTray);
    settings.setValue(QStringLiteral("app/openChatOnMessage"), m_settings.openChatOnMessage);
    settings.setValue(QStringLiteral("app/launchWithWindows"), m_settings.launchWithWindows);
    settings.setValue(QStringLiteral("app/saveHistory"), m_settings.saveHistory);
    settings.setValue(QStringLiteral("app/muteSounds"), m_settings.muteSounds);
    settings.setValue(QStringLiteral("app/showPlayingInfo"), m_settings.showPlayingInfo);
    settings.setValue(QStringLiteral("app/hideTypingIndicator"), m_settings.hideTypingIndicator);
    settings.setValue(QStringLiteral("internet/searchable"), m_settings.internetSearchable);
    settings.setValue(QStringLiteral("app/awayAutoReply"), m_settings.awayAutoReply);
    settings.setValue(QStringLiteral("app/awayAutoReplyMessage"), m_settings.awayAutoReplyMessage);
    settings.setValue(QStringLiteral("app/manualPersonalMessage"), m_manualPersonalMessage);
    settings.setValue(QStringLiteral("app/mode"), m_settings.appMode);
    settings.setValue(QStringLiteral("internet/serverHost"), m_settings.internetServerHost);
    settings.setValue(QStringLiteral("internet/serverPort"), m_settings.internetServerPort);
    if (m_settings.internetAuthToken.isEmpty()) {
        settings.remove(QStringLiteral("internet/authTokenEncrypted"));
    } else {
        settings.setValue(QStringLiteral("internet/authTokenEncrypted"), encryptLocalSecret(m_settings.internetAuthToken));
    }
    settings.remove(QStringLiteral("internet/authToken"));
    settings.setValue(QStringLiteral("internet/blinqId"), m_settings.internetBlinqId);
    settings.setValue(QStringLiteral("internet/displayName"), m_settings.internetDisplayName);
    settings.setValue(QStringLiteral("app/manualPeerAddresses"), m_settings.manualPeerAddresses);
    settings.remove(QStringLiteral("app/allowPublicPrivateChatRequests"));
    settings.remove(QStringLiteral("app/privateChatTrustLevel"));
    settings.remove(QStringLiteral("app/trustedPeers"));
    QStringList blockedPeers;
    for (const QString &peerId : m_blockedPeers) {
        blockedPeers.append(peerId);
        settings.setValue(QStringLiteral("app/blockedPeerNames/%1").arg(peerId), m_blockedPeerNames.value(peerId, peerId));
    }
    settings.setValue(QStringLiteral("app/blockedPeers"), blockedPeers);
    QStringList collapsedGroups;
    for (auto it = m_groupCollapsed.constBegin(); it != m_groupCollapsed.constEnd(); ++it) {
        if (it.value()) {
            collapsedGroups.append(it.key());
        }
    }
    settings.setValue(QStringLiteral("app/collapsedContactGroups"), collapsedGroups);
    settings.setValue(QStringLiteral("app/favoritePeers"), QStringList(m_favoritePeers.values()));
    settings.setValue(QStringLiteral("app/customContactGroups"), m_customContactGroups);
    settings.setValue(QStringLiteral("app/peerGroupIds"), QStringList(m_peerGroups.keys()));
    settings.remove(QStringLiteral("app/peerGroups"));
    for (auto it = m_peerGroups.constBegin(); it != m_peerGroups.constEnd(); ++it) {
        settings.setValue(QStringLiteral("app/peerGroups/%1").arg(it.key()), it.value());
    }
    if (!isInternetMode()) {
        saveKnownPeers(settings);
        saveOfflineMessages(settings);
    }
    applyLaunchWithWindowsSetting();
}

void MainWindow::applyLaunchWithWindowsSetting() const
{
#ifdef Q_OS_WIN
    QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
    if (m_settings.launchWithWindows) {
        const QString command = QStringLiteral("\"%1\" --startup").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        runKey.setValue(QStringLiteral("Blinq Messenger"), command);
        runKey.remove(QStringLiteral("LANChat"));
    } else {
        runKey.remove(QStringLiteral("LANChat"));
        runKey.remove(QStringLiteral("Blinq Messenger"));
    }
#endif
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(appStyleSheet());
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    const QColor localAccent(appThemeForKey(settings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString()).accent);
    for (ChatWindow *window : std::as_const(m_chatWindows)) {
        if (window) {
            window->setLocalAccentColor(localAccent);
        }
    }
    const QList<QWidget *> topLevels = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevels) {
        if (!widget) {
            continue;
        }
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        const QList<QWidget *> children = widget->findChildren<QWidget *>();
        for (QWidget *child : children) {
            child->style()->unpolish(child);
            child->style()->polish(child);
            child->update();
        }
        widget->update();
    }
}

QString MainWindow::appStyleSheet() const
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    const QString themeKey = settings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString();
    const AppTheme theme = appThemeForKey(themeKey);

    return QStringLiteral(
        "QMainWindow, QDialog, QWidget { background: %1; color: #111827; font-family: 'Segoe UI'; font-size: 9.5pt; }"
        "QWidget#ProfilePanel { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %2, stop:0.58 %3, stop:1 %9); border: 1px solid %4; border-radius: 8px; }"
        "QWidget#ProfileInfoPanel { background: transparent; border: none; }"
        "QLabel#LocalAvatar { background: rgba(255,255,255,220); border: 1px solid %4; border-radius: 6px; padding: 3px; }"
        "QLabel#LocalNameLabel { color: #0f172a; font-size: 18px; font-weight: 750; background: transparent; }"
        "QLineEdit#LocalPersonalMessageEdit { background: transparent; border: 1px solid transparent; padding: 1px 2px; color: #334155; font-style: italic; font-size: 11px; border-radius: 4px; }"
        "QLineEdit#LocalPersonalMessageEdit:hover, QLineEdit#LocalPersonalMessageEdit:focus { border: 1px solid %5; background: rgba(255,255,255,165); }"
        "QLabel#NowPlayingLabel { color: %6; font-size: 10.5px; font-weight: 650; background: %9; border: 1px solid %4; border-radius: 5px; padding: 2px 5px; }"
        "QPushButton#StatusButton { text-align: left; background: transparent; border: none; outline: none; padding: 1px 0px; color: %6; font-weight: 700; font-size: 11px; }"
        "QPushButton#StatusButton:hover, QPushButton#StatusButton:pressed, QPushButton#StatusButton:open { color: #0f172a; background: transparent; border: none; outline: none; }"
        "QPushButton#StatusButton::menu-indicator { image: none; width: 0px; }"
        "QPushButton#ProfileNameButton { background: transparent; border: 1px solid transparent; border-radius: 13px; padding: 4px; }"
        "QPushButton#ProfileNameButton:hover { background: %7; border-color: %4; }"
        "QPushButton#ProfileNameButton:pressed { background: %8; border-color: %5; }"
        "QMenuBar { background: #ffffff; border-bottom: 1px solid #dbe3ef; padding: 2px; }"
        "QMenuBar::item { padding: 7px 12px; border-radius: 6px; }"
        "QMenuBar::item:selected { background: %7; color: #0f2748; }"
        "QMenu { background: #ffffff; color: #111827; border: 1px solid #ffffff; border-radius: 0px; padding: 6px; }"
        "QMenu::item { padding: 8px 28px 8px 38px; border-radius: 6px; }"
        "QMenu::icon { left: 10px; }"
        "QMenu::item:selected { background: %7; color: #0f2748; }"
        "QComboBox { background: #ffffff; border: 1px solid #b9c5d6; border-radius: 9px; padding: 8px 10px; }"
        "QComboBox:hover { border-color: %5; }"
        "QComboBox::drop-down { width: 28px; border: none; }"
        "QComboBox QLineEdit { selection-background-color: %5; selection-color: #ffffff; }"
        "QComboBox QAbstractItemView { background: #ffffff; color: #111827; selection-background-color: %8; border: 1px solid #cbd5e1; }"
        "QTabWidget::pane { border: 1px solid #d6deea; background: #ffffff; }"
        "QTabBar::tab { background: #e8eef7; color: #1f2937; border: 1px solid #cbd5e1; padding: 7px 12px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #ffffff; color: #0f2748; border-bottom-color: #ffffff; }"
        "QTabBar::tab:hover { background: #d7e8ff; color: #0f2748; }"
        "QTabBar::tab:disabled { color: #64748b; }"
        "QWidget#ContactsHeader { background: transparent; border: none; }"
        "QLabel#ContactsLabel { color: #111827; font-weight: 750; }"
        "QLabel#ContactsTotalLabel { color: #64748b; font-weight: 650; font-size: 11px; }"
        "QLabel#EmptyContactsLabel { color: #64748b; font-size: 15px; font-weight: 650; }"
        "QListWidget { background: transparent; color: #111827; border: none; border-radius: 0px; outline: 0; padding: 0px; selection-color: #111827; }"
        "QListWidget::item { color: #111827; border: none; }"
        "QListWidget::item:hover { background: %7; border-radius: 8px; margin: 2px; }"
        "QListWidget::item:selected { background: %8; color: #111827; border-radius: 8px; margin: 2px; }"
        "QPushButton { background: %7; color: #0f2748; border: 1px solid %4; border-radius: 9px; padding: 8px 16px; font-weight: 650; }"
        "QPushButton:hover { background: %8; border-color: %5; }"
        "QPushButton:pressed { background: %3; }"
        "QPushButton:disabled { background: #e2e8f0; border-color: #cbd5e1; color: #64748b; }"
        "QWidget#ChatComposerPanel { background: #ffffff; border: 1px solid #cbd5e1; border-radius: 8px; }"
        "QWidget#ChatComposerToolRow, QWidget#ChatComposerInputRow { background: transparent; border: none; }"
        "QWidget#ChatComposerSeparator { background: #d6deea; border: none; }"
        "QPushButton#ChatComposerToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 0px; }"
        "QPushButton#ChatComposerToolButton:hover { background: %7; border-color: transparent; }"
        "QPushButton#ChatComposerToolButton:pressed { background: %8; border-color: transparent; }"
        "QLineEdit#ChatMessageEdit { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 6px 2px; selection-background-color: %5; selection-color: #ffffff; }"
        "QLineEdit#ChatMessageEdit:focus { border-color: transparent; }"
        "QPushButton#ChatToolButton { background: #ffffff; border: 1px solid %4; border-radius: 20px; padding: 0px; }"
        "QPushButton#ChatToolButton:hover { background: %7; border-color: %5; }"
        "QPushButton#ChatToolButton:pressed { background: %8; }"
        "QPushButton#ChatSendButton { background: %6; border: none; border-radius: 17px; padding: 0px; }"
        "QPushButton#ChatSendButton:hover { background: %5; border: none; }"
        "QPushButton#ChatSendButton:pressed { background: %6; border: none; }"
        "QTextEdit, QPlainTextEdit, QLineEdit, QTextBrowser { background: #ffffff; color: #111827; border: 1px solid #cbd5e1; border-radius: 8px; padding: 8px; selection-background-color: %5; selection-color: #ffffff; }"
        "QTextEdit:focus, QPlainTextEdit:focus, QLineEdit:focus, QTextBrowser:focus { border-color: %5; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: #cbd5e1; border-radius: 5px; min-height: 28px; }"
        "QScrollBar::handle:vertical:hover { background: #94a3b8; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QLineEdit { background: #ffffff; color: #111827; border: 1px solid #cbd5e1; border-radius: 8px; padding: 7px; selection-background-color: %5; selection-color: #ffffff; }"
        "QListWidget { background: #ffffff; color: #111827; border: 1px solid #d6deea; border-radius: 8px; selection-color: #111827; }"
        "QListWidget::item { color: #111827; }"
        "QListWidget::item:selected { color: #111827; }")
        .arg(theme.window,
             theme.panelTop,
             theme.panelBottom,
             theme.panelBorder,
             theme.accent,
             theme.accentDark,
             theme.hover,
             theme.selected,
             theme.soft);
}

void MainWindow::upsertPeer(const ChatPeer &peer)
{
    if (m_blockedPeers.contains(peer.id)) {
        return;
    }

    const auto knownIds = m_knownPeers.keys();
    for (const QString &knownId : knownIds) {
        if (knownId == peer.id || m_peers.contains(knownId)) {
            continue;
        }

        const ChatPeer knownPeer = m_knownPeers.value(knownId);
        if (!knownPeer.name.isEmpty() && knownPeer.name == peer.name) {
            if (auto *oldItem = m_peerItems.take(knownId)) {
                delete oldItem;
            }
            if (auto *window = m_chatWindows.take(knownId)) {
                window->close();
                window->deleteLater();
            }
            m_knownPeers.remove(knownId);
        }
    }

    m_peers.insert(peer.id, peer);
    m_knownPeers.insert(peer.id, peer);
    saveSettings();
    rebuildContactList();

    if (auto *window = m_chatWindows.value(peer.id, nullptr)) {
        window->setPeerDetails(peer.name, peer.status, peer.avatarData, peer.lastSeen, QColor(peer.themeColor));
    }
    updatePublicChatParticipants();
    updateEmptyContactsLabel();
    updateContactsLabel();
    updateNetworkStatus();
    flushPendingOfflineMessages(peer.id);
}

void MainWindow::removePeer(const QString &peerId, bool appClosed)
{
    ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    m_peers.remove(peerId);
    m_typingPeers.remove(peerId);
    m_seenOnlineNotifications.remove(peerId);
    peer.status = appClosed ? tr("Offline") : tr("Idle");
    peer.lastSeen = QDateTime::currentDateTime();
    m_knownPeers.insert(peerId, peer);
    saveSettings();
    rebuildContactList();
    if (auto *window = m_chatWindows.value(peerId, nullptr)) {
        const QString fallbackName = appClosed ? tr("Offline contact") : tr("Idle contact");
        window->setPeerDetails(peer.name.isEmpty() ? fallbackName : peer.name, peer.status, peer.avatarData, peer.lastSeen, QColor(peer.themeColor));
    }
    updatePublicChatParticipants();
    updateEmptyContactsLabel();
    updateContactsLabel();
    updateNetworkStatus();
}

void MainWindow::openChat(const QString &peerId)
{
    if (peerId.isEmpty()) {
        return;
    }

    auto *window = chatWindowFor(peerId);
    markChatActive();
    window->showNormal();
    window->raise();
    window->activateWindow();
    window->setFocus();
    sendPendingReadReceipts(peerId);
}

void MainWindow::openPublicChat()
{
    if (!m_publicChatWindow) {
        m_publicChatWindow = new PublicChatWindow();
        m_publicChatWindow->setAttribute(Qt::WA_DeleteOnClose);
        m_chatService->setPublicChatOpen(true);
        connect(m_publicChatWindow, &QObject::destroyed, this, [this] {
            m_publicChatWindow = nullptr;
            m_chatService->setPublicChatOpen(false);
        });
        connect(m_publicChatWindow, &PublicChatWindow::sendMessageRequested, this, [this](const QString &message, bool isHtml) {
            if (!allowRateLimitedAction(QStringLiteral("outgoing-public"), 500)) {
                statusBar()->showMessage(tr("Slow down a little before sending another public message."), 3000);
                return;
            }
            m_chatService->sendPublicMessage(message, isHtml);
        });
        connect(m_publicChatWindow, &PublicChatWindow::viewContactInfoRequested, this, &MainWindow::showContactInfo);
        connect(m_publicChatWindow, &PublicChatWindow::whistleRequested, this, [this](const QString &peerId) {
            if (peerId.isEmpty()) {
                return;
            }
            if (!m_peers.contains(peerId)) {
                statusBar()->showMessage(tr("That contact is no longer available."), 5000);
                updatePublicChatParticipants();
                return;
            }
            const ChatPeer peer = m_peers.value(peerId);
            if (peer.status == tr("Do Not Disturb")) {
                statusBar()->showMessage(tr("%1 is in Do Not Disturb. Whistle was not sent.").arg(peer.name.isEmpty() ? tr("That contact") : peer.name), 5000);
                return;
            }
            if (!allowRateLimitedAction(QStringLiteral("public-whistle-%1").arg(peerId), 1500)) {
                statusBar()->showMessage(tr("Slow down a little before whistling again."), 3000);
                return;
            }
            makeAvailableForOutgoingPrivateAction();
            m_chatService->sendBuzz(peerId);
            statusBar()->showMessage(tr("Whistle sent."), 3000);
        });
        updatePublicChatParticipants();
    }
    m_publicChatWindow->showNormal();
    m_publicChatWindow->raise();
    m_publicChatWindow->activateWindow();
}

void MainWindow::showInternetSignInDialog()
{
    if (!isInternetMode() || m_internetRelay->isAuthenticated()) {
        return;
    }
    if (m_authDialogOpen) {
        return;
    }
    m_authDialogOpen = true;

    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Sign in to Blinq"));
    dialog.setStyleSheet(appStyleSheet());
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *tabs = new QTabWidget(&dialog);
    auto *signInPage = new QWidget(tabs);
    auto *signInForm = new QFormLayout(signInPage);
    signInForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *signInId = new QLineEdit(m_settings.internetBlinqId, signInPage);
    signInId->setPlaceholderText(tr("username@blinqm.net"));
    auto *signInPassword = new QLineEdit(signInPage);
    signInPassword->setEchoMode(QLineEdit::Password);
    signInForm->addRow(tr("Blinq ID"), signInId);
    signInForm->addRow(tr("Password"), signInPassword);
    tabs->addTab(signInPage, tr("Sign In"));

    auto *createPage = new QWidget(tabs);
    auto *createForm = new QFormLayout(createPage);
    createForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *createUsername = new QLineEdit(createPage);
    createUsername->setPlaceholderText(tr("username"));
    auto *createDisplayName = new QLineEdit(m_chatService->localName(), createPage);
    auto *createPassword = new QLineEdit(createPage);
    createPassword->setEchoMode(QLineEdit::Password);
    auto *confirmPassword = new QLineEdit(createPage);
    confirmPassword->setEchoMode(QLineEdit::Password);
    createForm->addRow(tr("Blinq ID"), createUsername);
    createForm->addRow(QString(), new QLabel(tr("@blinqm.net"), createPage));
    createForm->addRow(tr("Display name"), createDisplayName);
    createForm->addRow(tr("Password"), createPassword);
    createForm->addRow(tr("Confirm"), confirmPassword);
    tabs->addTab(createPage, tr("Create Account"));
    layout->addWidget(tabs);

    auto *statusLabel = new QLabel(tr("Internet Mode uses your Blinq ID for contacts and messages."), &dialog);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet(QStringLiteral("color:#475569;"));
    layout->addWidget(statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *submitButton = buttons->addButton(tr("Continue"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    const auto normalizeId = [](QString value) {
        value = value.trimmed().toLower();
        if (!value.contains(QLatin1Char('@'))) {
            value += QStringLiteral("@blinqm.net");
        }
        return value;
    };

    connect(submitButton, &QPushButton::clicked, &dialog, [this, tabs, signInId, signInPassword, createUsername, createDisplayName, createPassword, confirmPassword, statusLabel, submitButton, normalizeId] {
        if (m_internetRelay->isAuthenticated()) {
            return;
        }
        submitButton->setEnabled(false);
        statusLabel->setText(tr("Contacting Blinq server..."));
        m_internetRelay->connectToServer(m_settings.internetServerHost,
                                         static_cast<quint16>(m_settings.internetServerPort));
        if (tabs->currentIndex() == 0) {
            m_settings.internetBlinqId = normalizeId(signInId->text());
            saveSettings();
            m_internetRelay->login(m_settings.internetBlinqId, signInPassword->text());
            return;
        }

        const QString username = createUsername->text().trimmed().toLower();
        static const QRegularExpression usernamePattern(QStringLiteral("^[a-z0-9._]{3,32}$"));
        if (!usernamePattern.match(username).hasMatch()) {
            statusLabel->setText(tr("Use 3-32 lowercase letters, numbers, dots, or underscores."));
            submitButton->setEnabled(true);
            return;
        }
        if (createPassword->text().size() < 8 || createPassword->text() != confirmPassword->text()) {
            statusLabel->setText(tr("Passwords must match and be at least 8 characters."));
            submitButton->setEnabled(true);
            return;
        }
        m_settings.internetBlinqId = QStringLiteral("%1@blinqm.net").arg(username);
        saveSettings();
        m_internetRelay->signUp(username, createPassword->text(), createDisplayName->text());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QMetaObject::Connection authedConnection;
    QMetaObject::Connection failedConnection;
    authedConnection = connect(m_internetRelay, &InternetRelayService::authenticated, &dialog, [&dialog](const QString &, const InternetRelayPeer &) {
        dialog.accept();
    });
    failedConnection = connect(m_internetRelay, &InternetRelayService::connectionFailed, &dialog, [statusLabel, submitButton](const QString &reason) {
        statusLabel->setText(QObject::tr("Sign-in failed: %1").arg(reason));
        submitButton->setEnabled(true);
    });
    connect(&dialog, &QObject::destroyed, this, [this, authedConnection, failedConnection] {
        m_authDialogOpen = false;
        disconnect(authedConnection);
        disconnect(failedConnection);
    });

    dialog.resize(430, dialog.sizeHint().height());
    dialog.exec();
}

void MainWindow::showAddInternetContactDialog()
{
    if (!isInternetMode()) {
        return;
    }
    if (!m_internetRelay->isAuthenticated()) {
        showInternetSignInDialog();
        return;
    }

    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Add Internet Contact"));
    dialog.setStyleSheet(appStyleSheet());
    dialog.resize(520, 430);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *intro = new QLabel(tr("Search by display name or Blinq ID, then send a contact request. Results only include people who are not already in your contacts."), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *searchRow = new QWidget(&dialog);
    auto *searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(8);
    auto *searchEdit = new QLineEdit(searchRow);
    searchEdit->setPlaceholderText(tr("Name or Blinq ID"));
    auto *searchButton = new QPushButton(tr("Search"), searchRow);
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(searchButton);
    layout->addWidget(searchRow);

    auto *resultsList = new QListWidget(&dialog);
    resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(resultsList, 1);

    auto *statusLabel = new QLabel(tr("Enter at least 2 characters to search."), &dialog);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    layout->addWidget(statusLabel);

    auto *manualLabel = new QLabel(tr("Already know their Blinq ID?"), &dialog);
    manualLabel->setStyleSheet(QStringLiteral("font-weight:650;"));
    layout->addWidget(manualLabel);

    auto *manualRow = new QWidget(&dialog);
    auto *manualLayout = new QHBoxLayout(manualRow);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->setSpacing(8);
    auto *manualEdit = new QLineEdit(manualRow);
    manualEdit->setPlaceholderText(tr("username@blinqm.net"));
    auto *manualButton = new QPushButton(tr("Add by ID"), manualRow);
    manualLayout->addWidget(manualEdit, 1);
    manualLayout->addWidget(manualButton);
    layout->addWidget(manualRow);

    auto *buttonRow = new QWidget(&dialog);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    buttonLayout->addStretch();
    auto *requestButton = new QPushButton(tr("Send Request"), buttonRow);
    requestButton->setEnabled(false);
    auto *closeButton = new QPushButton(tr("Close"), buttonRow);
    buttonLayout->addWidget(requestButton);
    buttonLayout->addWidget(closeButton);
    layout->addWidget(buttonRow);

    QString lastQuery;
    const auto sendRequest = [this, statusLabel](const QString &blinqId) {
        const QString trimmed = blinqId.trimmed();
        if (trimmed.isEmpty()) {
            statusLabel->setText(tr("Enter a Blinq ID first."));
            return;
        }
        m_internetRelay->addContact(trimmed);
        statusLabel->setText(tr("Contact request sent to %1.").arg(trimmed));
        statusBar()->showMessage(tr("Contact request sent."), 5000);
    };

    const auto runSearch = [this, searchEdit, resultsList, statusLabel, &lastQuery] {
        const QString query = searchEdit->text().trimmed();
        if (query.size() < 2) {
            resultsList->clear();
            statusLabel->setText(tr("Enter at least 2 characters to search."));
            return;
        }
        if (!allowRateLimitedAction(QStringLiteral("internet-user-search"), 2000)) {
            statusLabel->setText(tr("Please wait a moment before searching again."));
            return;
        }
        lastQuery = query;
        resultsList->clear();
        statusLabel->setText(tr("Searching..."));
        m_internetRelay->searchUsers(query);
    };

    connect(searchButton, &QPushButton::clicked, &dialog, runSearch);
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, runSearch);
    connect(resultsList, &QListWidget::itemSelectionChanged, &dialog, [resultsList, requestButton] {
        requestButton->setEnabled(resultsList->currentItem() != nullptr);
    });
    connect(resultsList, &QListWidget::itemDoubleClicked, &dialog, [sendRequest](QListWidgetItem *item) {
        if (item) {
            sendRequest(item->data(Qt::UserRole).toString());
        }
    });
    connect(requestButton, &QPushButton::clicked, &dialog, [resultsList, sendRequest] {
        if (auto *item = resultsList->currentItem()) {
            sendRequest(item->data(Qt::UserRole).toString());
        }
    });
    connect(manualButton, &QPushButton::clicked, &dialog, [manualEdit, sendRequest] {
        sendRequest(manualEdit->text());
    });
    connect(manualEdit, &QLineEdit::returnPressed, manualButton, &QPushButton::click);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(m_internetRelay, &InternetRelayService::userSearchResultsReceived, &dialog, [resultsList, statusLabel, requestButton, &lastQuery](const QString &query, const QList<InternetUserSearchResult> &results) {
        if (query != lastQuery) {
            return;
        }
        resultsList->clear();
        requestButton->setEnabled(false);
        for (const InternetUserSearchResult &result : results) {
            const InternetRelayPeer user = result.user;
            const QString displayName = user.displayName.isEmpty() ? user.blinqId : user.displayName;
            auto *item = new QListWidgetItem(QStringLiteral("%1  <%2>").arg(displayName, user.blinqId), resultsList);
            item->setData(Qt::UserRole, user.blinqId);
            item->setToolTip(user.blinqId);
        }
        statusLabel->setText(results.isEmpty()
                                 ? QObject::tr("No searchable users found.")
                                 : QObject::tr("%n user(s) found.", nullptr, results.size()));
    });

    dialog.exec();
}

void MainWindow::switchAppMode()
{
    const bool switchingToInternet = !isInternetMode();
    const QString targetMode = switchingToInternet ? tr("Internet Mode") : tr("LAN Mode");
    if (QMessageBox::question(dialogParent(),
                              tr("Switch Mode"),
                              tr("Switch to %1? Blinq Messenger will restart.").arg(targetMode),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes)
        != QMessageBox::Yes) {
        return;
    }

    m_settings.appMode = switchingToInternet ? QStringLiteral("internet") : QStringLiteral("lan");
    saveSettings();
    restartApplication();
}

void MainWindow::deleteBlinqAccount()
{
    if (!isInternetMode()) {
        return;
    }
    if (!m_internetRelay->isAuthenticated()) {
        showInternetSignInDialog();
        return;
    }

    const QString blinqId = m_settings.internetBlinqId.isEmpty() ? m_internetRelay->self().blinqId : m_settings.internetBlinqId;
    if (QMessageBox::question(dialogParent(),
                              tr("Delete Blinq Account"),
                              tr("Delete %1?\n\nThis permanently removes your Blinq account, contacts, and server-side messages. Blinq Messenger will restart in LAN Mode.")
                                  .arg(blinqId.isEmpty() ? tr("this account") : blinqId),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    bool ok = false;
    const QString password = QInputDialog::getText(dialogParent(),
                                                  tr("Confirm Password"),
                                                  tr("Enter your Blinq account password to delete %1:").arg(blinqId.isEmpty() ? tr("this account") : blinqId),
                                                  QLineEdit::Password,
                                                  QString(),
                                                  &ok);
    if (!ok || password.isEmpty()) {
        return;
    }

    m_internetRelay->deleteAccount(password);
    statusBar()->showMessage(tr("Deleting Blinq account..."), 5000);
}

void MainWindow::changeBlinqPassword()
{
    if (!isInternetMode()) {
        return;
    }
    if (!m_internetRelay->isAuthenticated()) {
        showInternetSignInDialog();
        return;
    }

    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Change Blinq Password"));
    dialog.setStyleSheet(appStyleSheet());
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *currentPassword = new QLineEdit(&dialog);
    currentPassword->setEchoMode(QLineEdit::Password);
    auto *newPassword = new QLineEdit(&dialog);
    newPassword->setEchoMode(QLineEdit::Password);
    auto *confirmPassword = new QLineEdit(&dialog);
    confirmPassword->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Current password"), currentPassword);
    form->addRow(tr("New password"), newPassword);
    form->addRow(tr("Confirm new password"), confirmPassword);
    layout->addLayout(form);

    auto *statusLabel = new QLabel(&dialog);
    statusLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *changeButton = buttons->addButton(tr("Change Password"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(changeButton, &QPushButton::clicked, &dialog, [this, currentPassword, newPassword, confirmPassword, statusLabel, changeButton] {
        const QString current = currentPassword->text();
        const QString next = newPassword->text();
        if (current.isEmpty()) {
            statusLabel->setText(tr("Enter your current password."));
            return;
        }
        if (next.size() < 8) {
            statusLabel->setText(tr("New password must be at least 8 characters."));
            return;
        }
        if (next != confirmPassword->text()) {
            statusLabel->setText(tr("New passwords do not match."));
            return;
        }
        changeButton->setEnabled(false);
        statusLabel->setText(tr("Changing password..."));
        m_internetRelay->changePassword(current, next);
    });
    connect(m_internetRelay, &InternetRelayService::passwordChanged, &dialog, [this, &dialog] {
        QMessageBox::information(&dialog,
                                 tr("Password Changed"),
                                 tr("Your Blinq account password was changed."));
        dialog.accept();
        statusBar()->showMessage(tr("Blinq password changed."), 5000);
    });
    connect(m_internetRelay, &InternetRelayService::errorOccurred, &dialog, [statusLabel, changeButton](const QString &message) {
        statusLabel->setText(message);
        changeButton->setEnabled(true);
    });

    dialog.resize(430, dialog.sizeHint().height());
    dialog.exec();
}

void MainWindow::handleInternetServerUnavailable(const QString &reason)
{
    if (!isInternetMode() || m_reallyQuit) {
        return;
    }

    const QString detail = reason.trimmed().isEmpty()
                               ? tr("The Blinq Internet server is not available.")
                               : reason.trimmed();
    QMessageBox::warning(dialogParent(),
                         tr("Internet Mode Unavailable"),
                         tr("%1\n\nBlinq Messenger will restart in LAN Mode so you can keep using local chat.")
                             .arg(detail));
    m_settings.appMode = QStringLiteral("lan");
    saveSettings();
    restartApplication();
}

void MainWindow::restartApplication()
{
    m_reallyQuit = true;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
    qApp->quit();
}

void MainWindow::changeDisplayName()
{
    QInputDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Display Name"));
    dialog.setLabelText(tr("What name do you want to go by?"));
    dialog.setTextEchoMode(QLineEdit::Normal);
    dialog.setTextValue(isInternetMode()
                            ? (m_settings.internetDisplayName.isEmpty() ? m_internetRelay->self().displayName : m_settings.internetDisplayName)
                            : m_chatService->localName());
    dialog.resize(width(), 160);
    if (dialog.exec() == QDialog::Accepted) {
        if (isInternetMode()) {
            const QString name = dialog.textValue().trimmed();
            if (!name.isEmpty()) {
                m_settings.internetDisplayName = name;
                saveSettings();
                m_internetRelay->setProfile(name, QByteArray());
                updateLocalProfile();
            }
            return;
        }
        m_chatService->setLocalName(dialog.textValue());
        updateLocalProfile();
    }
}

void MainWindow::updatePublicChatParticipants()
{
    if (m_publicChatWindow) {
        QList<ChatPeer> participants;
        for (const ChatPeer &peer : m_peers) {
            if (peer.publicChatOpen) {
                participants.append(peer);
            }
        }
        m_publicChatWindow->setParticipants(participants);
    }
}

void MainWindow::rebuildInternetContacts()
{
    if (!isInternetMode()) {
        return;
    }

    m_peers.clear();
    m_knownPeers.clear();
    m_internetPeers.clear();
    for (const InternetRelayPeer &internetPeer : m_internetRelay->contacts()) {
        upsertInternetPeer(internetPeer);
    }
    rebuildContactList();
    updateEmptyContactsLabel();
    updateContactsLabel();
}

void MainWindow::upsertInternetPeer(const InternetRelayPeer &internetPeer)
{
    if (internetPeer.id.isEmpty()) {
        return;
    }
    m_internetPeers.insert(internetPeer.id, internetPeer);
    const ChatPeer peer = chatPeerFromInternetPeer(internetPeer);
    m_knownPeers.insert(peer.id, peer);
    if (peer.status != tr("Offline")) {
        m_peers.insert(peer.id, peer);
    } else {
        m_peers.remove(peer.id);
    }
    if (auto *window = m_chatWindows.value(peer.id, nullptr)) {
        window->setPeerDetails(peer.name, peer.status, peer.avatarData, peer.lastSeen, QColor(peer.themeColor));
    }
}

bool MainWindow::isInternetMode() const
{
    return m_settings.appMode == QStringLiteral("internet");
}

bool MainWindow::isInternetPeer(const QString &peerId) const
{
    return isInternetMode() && m_internetPeers.contains(peerId);
}

ChatPeer MainWindow::chatPeerFromInternetPeer(const InternetRelayPeer &internetPeer) const
{
    ChatPeer peer;
    peer.id = internetPeer.id;
    peer.name = internetPeer.displayName.isEmpty() ? internetPeer.blinqId : internetPeer.displayName;
    peer.status = internetPeer.status.isEmpty() ? tr("Offline") : internetPeer.status;
    peer.personalMessage = peer.status == tr("Offline")
                               ? QString()
                               : (internetPeer.personalMessage.isEmpty() ? internetPeer.blinqId : internetPeer.personalMessage);
    peer.themeColor = internetPeer.themeColor;
    peer.avatarData = QByteArray::fromBase64(internetPeer.avatar.toLatin1());
    peer.lastSeen = QDateTime::currentDateTimeUtc();
    peer.protocolVersion = 100;
    peer.publicChatOpen = false;
    return peer;
}

void MainWindow::queueOfflineMessage(const QString &peerId, const QString &message, bool isHtml)
{
    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    if (peer.status == tr("Do Not Disturb")) {
        return;
    }

    PendingOfflineMessage pending;
    pending.message = message;
    pending.isHtml = isHtml;
    pending.queuedAt = QDateTime::currentDateTimeUtc();
    m_offlineMessages[peerId].append(pending);
    if (auto *window = m_chatWindows.value(peerId, nullptr)) {
        window->setQueuedMessageCount(m_offlineMessages.value(peerId).size());
    }
    saveSettings();
}

void MainWindow::flushPendingOfflineMessages(const QString &peerId)
{
    if (!m_peers.contains(peerId) || !m_offlineMessages.contains(peerId)) {
        return;
    }
    if (m_peers.value(peerId).status == tr("Do Not Disturb")) {
        const int removed = m_offlineMessages.take(peerId).size();
        saveSettings();
        if (auto *existingWindow = m_chatWindows.value(peerId, nullptr)) {
            existingWindow->setQueuedMessageCount(0);
            if (removed > 0) {
                existingWindow->appendSystemMessage(tr("Queued messages were canceled because this contact is in Do Not Disturb."));
            }
        }
        return;
    }

    const QList<PendingOfflineMessage> pendingMessages = m_offlineMessages.take(peerId);
    saveSettings();
    if (auto *existingWindow = m_chatWindows.value(peerId, nullptr)) {
        existingWindow->setQueuedMessageCount(0);
    }
    if (pendingMessages.isEmpty()) {
        return;
    }

    auto *window = m_chatWindows.value(peerId, nullptr);
    if (window) {
        window->appendSystemMessage(tr("Sending %1 queued offline message(s)...").arg(pendingMessages.size()));
    }

    for (const PendingOfflineMessage &pending : pendingMessages) {
        m_chatService->sendMessage(peerId, pending.message, pending.isHtml);
    }
}

void MainWindow::showQueuedMessages(const QString &peerId, ChatWindow *parentWindow)
{
    const QList<PendingOfflineMessage> pendingMessages = m_offlineMessages.value(peerId);
    if (pendingMessages.isEmpty()) {
        if (parentWindow) {
            parentWindow->appendSystemMessage(tr("There are no queued messages to view."));
        }
        return;
    }

    auto *dialog = new QDialog(parentWindow ? static_cast<QWidget *>(parentWindow) : dialogParent());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Queued Messages"));
    dialog->resize(430, 320);
    dialog->setStyleSheet(appStyleSheet());

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    auto *title = new QLabel(tr("Queued for %1").arg(peer.name.isEmpty() ? tr("this contact") : peer.name), dialog);
    title->setStyleSheet(QStringLiteral("font-size:15px; font-weight:700; background:transparent; border:none;"));
    layout->addWidget(title);

    auto *text = new QTextEdit(dialog);
    text->setObjectName(QStringLiteral("QueuedMessagesPreview"));
    text->setReadOnly(true);
    text->setStyleSheet(QStringLiteral(
        "QTextEdit#QueuedMessagesPreview { background:#ffffff; border:1px solid #cbd5e1; border-radius:8px; padding:10px; }"));
    QStringList entries;
    int index = 1;
    for (const PendingOfflineMessage &pending : pendingMessages) {
        QString message = pending.message;
        if (pending.isHtml) {
            QTextDocument document;
            document.setHtml(message);
            message = document.toPlainText();
        }
        const QString queuedAt = pending.queuedAt.toLocalTime().toString(QStringLiteral("MMM d, yyyy h:mm AP"));
        const QString type = pending.isHtml ? tr("Rich message") : tr("Plain text");
        entries << QStringLiteral(
                       "<tr>"
                       "<td style=\"padding:8px 0 4px 0; color:#0f2748; font-weight:700;\">Message %1</td>"
                       "</tr>"
                       "<tr>"
                       "<td style=\"padding:0 0 6px 0; color:#64748b; font-size:8.5pt;\">Queued %2 - %3</td>"
                       "</tr>"
                       "<tr>"
                       "<td style=\"padding:0 0 3px 0; color:#111827; font-size:10pt;\">%4</td>"
                       "</tr>"
                       "<tr><td style=\"border-bottom:1px solid #e2e8f0; height:0px; padding:0;\"></td></tr>")
                       .arg(index++)
                       .arg(queuedAt.toHtmlEscaped())
                       .arg(type.toHtmlEscaped())
                       .arg(message.trimmed().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }
    text->setHtml(QStringLiteral(
        "<html><body style=\"font-family:'Segoe UI'; background:#ffffff; margin:0;\">"
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">%1</table>"
        "</body></html>").arg(entries.join(QString())));
    layout->addWidget(text, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *closeButton = new QPushButton(tr("Close"), dialog);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool MainWindow::shouldSendAwayAutoReply(const QString &peerId, const QString &incomingMessage, bool isHtml) const
{
    if (!m_settings.awayAutoReply || peerId.isEmpty() || !m_peers.contains(peerId)) {
        return false;
    }

    const QString localStatus = m_chatService->effectiveLocalStatus();
    if (localStatus == tr("Available") || localStatus == tr("Invisible") || localStatus == tr("Do Not Disturb")) {
        return false;
    }

    QString plainText = incomingMessage;
    if (isHtml) {
        QTextDocument document;
        document.setHtml(incomingMessage);
        plainText = document.toPlainText();
    }
    if (plainText.trimmed().startsWith(QStringLiteral("[Auto-reply]"), Qt::CaseInsensitive)) {
        return false;
    }

    const QDateTime lastReply = m_autoReplySentAt.value(peerId);
    return !lastReply.isValid() || lastReply.secsTo(QDateTime::currentDateTimeUtc()) >= 300;
}

void MainWindow::maybeSendAwayAutoReply(const QString &peerId, const QString &incomingMessage, bool isHtml)
{
    if (!shouldSendAwayAutoReply(peerId, incomingMessage, isHtml)) {
        return;
    }

    const QString reply = tr("[Auto-reply] %1").arg(m_settings.awayAutoReplyMessage.trimmed());
    m_autoReplySentAt.insert(peerId, QDateTime::currentDateTimeUtc());
    m_chatService->sendMessage(peerId, reply, false);
}

void MainWindow::loadOfflineMessages(const QSettings &settings)
{
    m_offlineMessages.clear();
    QByteArray stored = settings.value(QStringLiteral("app/offlineMessagesEncrypted")).toByteArray();
    if (!stored.isEmpty()) {
        stored = cryptLocalSettingsBytes(QByteArray::fromBase64(stored));
    } else {
        stored = settings.value(QStringLiteral("app/offlineMessages")).toByteArray();
    }
    const QJsonDocument document = QJsonDocument::fromJson(stored);
    if (!document.isArray()) {
        return;
    }

    const QJsonArray messages = document.array();
    for (const QJsonValue &value : messages) {
        const QJsonObject object = value.toObject();
        const QString peerId = object.value(QStringLiteral("peerId")).toString();
        const QString message = object.value(QStringLiteral("message")).toString();
        if (peerId.isEmpty() || message.trimmed().isEmpty()) {
            continue;
        }

        PendingOfflineMessage pending;
        pending.message = message;
        pending.isHtml = object.value(QStringLiteral("isHtml")).toBool(false);
        pending.queuedAt = QDateTime::fromString(object.value(QStringLiteral("queuedAt")).toString(), Qt::ISODate);
        if (!pending.queuedAt.isValid()) {
            pending.queuedAt = QDateTime::currentDateTimeUtc();
        }
        m_offlineMessages[peerId].append(pending);
    }
}

void MainWindow::saveOfflineMessages(QSettings &settings) const
{
    QJsonArray messages;
    for (auto it = m_offlineMessages.constBegin(); it != m_offlineMessages.constEnd(); ++it) {
        for (const PendingOfflineMessage &pending : it.value()) {
            QJsonObject object;
            object.insert(QStringLiteral("peerId"), it.key());
            object.insert(QStringLiteral("message"), pending.message);
            object.insert(QStringLiteral("isHtml"), pending.isHtml);
            object.insert(QStringLiteral("queuedAt"), pending.queuedAt.toUTC().toString(Qt::ISODate));
            messages.append(object);
        }
    }
    const QByteArray json = QJsonDocument(messages).toJson(QJsonDocument::Compact);
    settings.setValue(QStringLiteral("app/offlineMessagesEncrypted"), QString::fromLatin1(cryptLocalSettingsBytes(json).toBase64()));
    settings.remove(QStringLiteral("app/offlineMessages"));
}

void MainWindow::loadKnownPeers(const QSettings &settings)
{
    m_knownPeers.clear();
    const QByteArray stored = settings.value(QStringLiteral("app/knownPeers")).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(stored);
    if (!document.isArray()) {
        return;
    }

    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        ChatPeer peer;
        peer.id = object.value(QStringLiteral("id")).toString();
        peer.name = object.value(QStringLiteral("name")).toString();
        peer.status = tr("Offline");
        peer.personalMessage = object.value(QStringLiteral("personalMessage")).toString();
        peer.themeColor = object.value(QStringLiteral("themeColor")).toString();
        peer.avatarData = QByteArray::fromBase64(object.value(QStringLiteral("avatar")).toString().toLatin1());
        peer.address = QHostAddress(object.value(QStringLiteral("address")).toString());
        peer.port = static_cast<quint16>(object.value(QStringLiteral("port")).toInt());
        peer.protocolVersion = object.value(QStringLiteral("protocolVersion")).toInt(3);
        peer.lastSeen = QDateTime::fromString(object.value(QStringLiteral("lastSeen")).toString(), Qt::ISODate);
        if (!peer.lastSeen.isValid()) {
            peer.lastSeen = QDateTime::currentDateTimeUtc();
        }
        if (!peer.id.isEmpty() && !peer.name.isEmpty() && !peer.address.isNull() && peer.port > 0 && !m_blockedPeers.contains(peer.id)) {
            m_knownPeers.insert(peer.id, peer);
        }
    }
}

void MainWindow::saveKnownPeers(QSettings &settings) const
{
    QJsonArray peers;
    for (const ChatPeer &peer : m_knownPeers) {
        if (peer.id.isEmpty() || peer.address.isNull() || peer.port == 0 || m_blockedPeers.contains(peer.id)) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("id"), peer.id);
        object.insert(QStringLiteral("name"), peer.name);
        object.insert(QStringLiteral("personalMessage"), peer.personalMessage);
        object.insert(QStringLiteral("themeColor"), peer.themeColor);
        object.insert(QStringLiteral("avatar"), QString::fromLatin1(peer.avatarData.toBase64()));
        object.insert(QStringLiteral("address"), peer.address.toString());
        object.insert(QStringLiteral("port"), static_cast<int>(peer.port));
        object.insert(QStringLiteral("protocolVersion"), peer.protocolVersion);
        object.insert(QStringLiteral("lastSeen"), peer.lastSeen.toUTC().toString(Qt::ISODate));
        peers.append(object);
    }
    settings.setValue(QStringLiteral("app/knownPeers"), QJsonDocument(peers).toJson(QJsonDocument::Compact));
}

QStringList MainWindow::rememberedPeerAddresses() const
{
    QStringList addresses = m_settings.manualPeerAddresses;
    for (const ChatPeer &peer : m_knownPeers) {
        if (!peer.address.isNull() && peer.port > 0) {
            addresses.append(QStringLiteral("%1:%2").arg(peer.address.toString()).arg(peer.port));
        }
    }
    addresses.removeDuplicates();
    return addresses;
}

QStringList MainWindow::localIpv4Addresses() const
{
    QStringList result;
    const auto addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            result.append(address.toString());
        }
    }
    return result;
}

QString MainWindow::connectionInviteText() const
{
    const QStringList addresses = localIpv4Addresses();
    const QString endpoint = addresses.isEmpty()
                                 ? QStringLiteral("IP:%1").arg(m_chatService->directTcpPort())
                                 : QStringLiteral("%1:%2").arg(addresses.first()).arg(m_chatService->directTcpPort());
    return tr("Blinq Messenger invite\nName: %1\nConnect: %2\n\nPaste the Connect value into Chat > Direct Connect by IP.")
        .arg(m_chatService->localName(), endpoint);
}

void MainWindow::startBackgroundConnectionAttempts(const QStringList &addresses, const QString &label, bool openFirstChat)
{
    QStringList uniqueAddresses = addresses;
    uniqueAddresses.removeAll(QString());
    uniqueAddresses.removeDuplicates();
    if (uniqueAddresses.isEmpty()) {
        statusBar()->showMessage(tr("No saved addresses to check."), 5000);
        return;
    }

    m_backgroundConnectionAttempts += uniqueAddresses.size();
    m_openFirstBackgroundConnectionChat = openFirstChat;
    m_openNextManualConnectionChat = false;
    statusBar()->showMessage(label, 7000);
    int delayMs = 0;
    for (const QString &address : uniqueAddresses) {
        QTimer::singleShot(delayMs, this, [this, address] {
            m_chatService->connectToAddress(address);
        });
        delayMs += 250;
    }
}

bool MainWindow::allowRateLimitedAction(const QString &key, int cooldownMs)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime previous = m_lastRateLimitedActions.value(key);
    if (previous.isValid() && previous.msecsTo(now) < cooldownMs) {
        return false;
    }

    m_lastRateLimitedActions.insert(key, now);
    return true;
}

void MainWindow::showNetworkDiagnostics()
{
    showConnectionInfo();
}

QWidget *MainWindow::dialogParent() const
{
    QWidget *active = QApplication::activeWindow();
    if (active) {
        return active;
    }
    return isActiveWindow() ? const_cast<MainWindow *>(this) : nullptr;
}

void MainWindow::showDirectConnectDialog()
{
    auto *dialog = new QDialog(dialogParent());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Direct Connect"));
    dialog->resize(width(), 150);
    ++m_directConnectDialogsOpen;
    connect(dialog, &QObject::destroyed, this, [this] {
        m_directConnectDialogsOpen = qMax(0, m_directConnectDialogsOpen - 1);
    });

    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(tr("Enter an IPv4 address, IP:port, or paste a Blinq connection invite."), dialog);
    auto *addressEdit = new QComboBox(dialog);
    addressEdit->setEditable(true);
    QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    const AppTheme theme = appThemeForKey(themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString());
    if (QLineEdit *lineEdit = addressEdit->lineEdit()) {
        QPalette palette = lineEdit->palette();
        palette.setColor(QPalette::Highlight, QColor(theme.accent));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        lineEdit->setPalette(palette);
    }
    const auto peers = m_chatService->peers();
    for (const QString &addr : m_settings.manualPeerAddresses) {
        QString displayText = addr;
        for (const ChatPeer &p : peers) {
            const QString peerAddress = QStringLiteral("%1:%2").arg(p.address.toString()).arg(p.port);
            if (addr == peerAddress || addr == p.address.toString()) {
                displayText = QStringLiteral("%1 (%2)").arg(addr, p.name);
                break;
            }
        }
        addressEdit->addItem(displayText, addr);
    }
    addressEdit->setPlaceholderText(tr("Example: 192.168.1.25 or 192.168.1.25:45460"));
    auto *statusLabel = new QLabel(dialog);
    statusLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *connectButton = buttons->addButton(tr("Check Connection"), QDialogButtonBox::AcceptRole);
    layout->addWidget(label);
    layout->addWidget(addressEdit);
    layout->addWidget(statusLabel);
    layout->addWidget(buttons);

    connect(connectButton, &QPushButton::clicked, dialog, [this, addressEdit, statusLabel, connectButton] {
        const QString address = addressEdit->currentText().trimmed();
        if (address.isEmpty()) {
            statusLabel->setText(tr("Enter an IP address first."));
            return;
        }
        connectButton->setEnabled(false);
        statusLabel->setText(tr("Checking %1...").arg(address));
        m_chatService->connectToAddress(address);
    });
    connect(m_chatService, &LanChatService::manualConnectionSucceeded, dialog, [dialog] {
        dialog->accept();
    });
    connect(m_chatService, &LanChatService::manualConnectionFailed, dialog, [statusLabel, connectButton](const QString &, const QString &reason) {
        statusLabel->setText(reason);
        connectButton->setEnabled(true);
    });
    connect(addressEdit->lineEdit(), &QLineEdit::returnPressed, connectButton, &QPushButton::click);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->show();
    addressEdit->setFocus();
}

void MainWindow::showConnectionInviteDialog()
{
    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Connection Invite"));
    dialog.resize(460, 260);

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Send this invite to someone on the same reachable network. They can paste it into Direct Connect."), &dialog);
    label->setWordWrap(true);
    auto *inviteText = new QTextEdit(&dialog);
    inviteText->setReadOnly(true);
    inviteText->setPlainText(connectionInviteText());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *copyButton = buttons->addButton(tr("Copy Invite"), QDialogButtonBox::ActionRole);
    layout->addWidget(label);
    layout->addWidget(inviteText, 1);
    layout->addWidget(buttons);

    connect(copyButton, &QPushButton::clicked, &dialog, [inviteText] {
        QApplication::clipboard()->setText(inviteText->toPlainText());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::showFirewallHelperDialog()
{
    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Firewall Setup Helper"));
    dialog.resize(520, 360);

    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(tr(
        "For Blinq Messenger to work reliably on a LAN, Windows Firewall should allow the app on Private networks.\n\n"
        "Recommended checks:\n"
        "- Open Windows Security > Firewall & network protection.\n"
        "- Choose Allow an app through firewall.\n"
        "- Make sure Blinq Messenger is allowed on Private networks.\n"
        "- If the network is marked Public, change it to Private only when you trust it.\n\n"
        "Discovery uses local UDP broadcast. Private messages, drawings, and files use direct TCP connections. Some Guest Wi-Fi, VPN, school, hotel, or corporate networks block device-to-device traffic even when the app is allowed through the firewall."));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *openFirewallButton = buttons->addButton(tr("Open Firewall Settings"), QDialogButtonBox::ActionRole);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);

    connect(openFirewallButton, &QPushButton::clicked, &dialog, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:network-firewall")));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::reconnectSavedContacts()
{
    const QStringList addresses = rememberedPeerAddresses();
    if (addresses.isEmpty()) {
        QMessageBox::information(dialogParent(),
                                 tr("Reconnect Saved Contacts"),
                                 tr("There are no saved manual or remembered contact addresses yet."));
        return;
    }
    startBackgroundConnectionAttempts(addresses,
                                      tr("Checking %1 saved contact address(es)...").arg(addresses.size()),
                                      false);
}

void MainWindow::scanLocalSubnet()
{
    const QStringList localAddresses = localIpv4Addresses();
    if (localAddresses.isEmpty()) {
        QMessageBox::warning(dialogParent(),
                             tr("Scan Local Network"),
                             tr("No local IPv4 address was found."));
        return;
    }

    const QString localAddress = localAddresses.first();
    const QStringList parts = localAddress.split(QLatin1Char('.'));
    if (parts.size() != 4) {
        return;
    }
    const QString prefix = QStringLiteral("%1.%2.%3.").arg(parts.at(0), parts.at(1), parts.at(2));
    if (QMessageBox::question(dialogParent(),
                              tr("Scan Local Network"),
                              tr("Blinq Messenger will check nearby addresses on %1x using its direct-connect port range.\n\n"
                                 "This can help when UDP discovery is blocked, but some managed networks may dislike scans.\n\n"
                                 "Start the scan?")
                                  .arg(prefix))
        != QMessageBox::Yes) {
        return;
    }

    QStringList addresses;
    for (int host = 1; host <= 254; ++host) {
        const QString address = QStringLiteral("%1%2").arg(prefix).arg(host);
        if (address != localAddress) {
            addresses.append(address);
        }
    }
    startBackgroundConnectionAttempts(addresses,
                                      tr("Scanning %1 local address(es)...").arg(addresses.size()),
                                      false);
}

void MainWindow::sendPendingReadReceipts(const QString &peerId)
{
    const QStringList messageIds = m_pendingReadReceipts.take(peerId);
    for (const QString &messageId : messageIds) {
        if (isInternetPeer(peerId)) {
            m_internetRelay->sendReceipt(peerId, messageId, QStringLiteral("Read"));
        } else {
            m_chatService->sendMessageReceipt(peerId, messageId, QStringLiteral("Read"));
        }
    }
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(m_settings, this);
    if (auto *list = dialog.findChild<QListWidget*>(QStringLiteral("ManualPeersList"))) {
        const auto peers = m_chatService->peers();
        for (int i = 0; i < list->count(); ++i) {
            const QString address = list->item(i)->data(Qt::UserRole).toString();
            for (const ChatPeer &p : peers) {
                const QString peerAddress = QStringLiteral("%1:%2").arg(p.address.toString()).arg(p.port);
                if (address == peerAddress || address == p.address.toString()) {
                    list->item(i)->setText(QStringLiteral("%1 (%2)").arg(address, p.name));
                    break;
                }
            }
        }
    }
    
    dialog.setStyleSheet(appStyleSheet());
    connect(&dialog, &SettingsDialog::directConnectRequested, this, [this] {
        showDirectConnectDialog();
    });
    connect(&dialog, &SettingsDialog::manualPeerConnectRequested, this, [this](const QString &address) {
        m_openNextManualConnectionChat = true;
        m_chatService->connectToAddress(address);
    });
    connect(&dialog, &SettingsDialog::deleteBlinqAccountRequested, this, [this, &dialog] {
        dialog.reject();
        deleteBlinqAccount();
    });
    connect(&dialog, &SettingsDialog::changeBlinqPasswordRequested, this, [this, &dialog] {
        dialog.reject();
        changeBlinqPassword();
    });
    connect(&dialog, &SettingsDialog::backupDataRequested, this, [this, &dialog] {
        backupAppData();
        dialog.reject();
    });
    connect(&dialog, &SettingsDialog::restoreDataRequested, this, [this, &dialog] {
        dialog.reject();
        restoreAppData();
    });
    connect(m_chatService, &LanChatService::manualConnectionSucceeded, &dialog, [&dialog](const QString &address, const QString &peerName) {
        if (auto *list = dialog.findChild<QListWidget*>(QStringLiteral("ManualPeersList"))) {
            bool exists = false;
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->data(Qt::UserRole).toString() == address || list->item(i)->text() == address) {
                    list->item(i)->setData(Qt::UserRole, address);
                    list->item(i)->setText(peerName.isEmpty() ? address : QStringLiteral("%1 (%2)").arg(address, peerName));
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                auto *item = new QListWidgetItem(peerName.isEmpty() ? address : QStringLiteral("%1 (%2)").arg(address, peerName), list);
                item->setData(Qt::UserRole, address);
                if (auto *emptyLabel = dialog.findChild<QLabel*>(QStringLiteral("ManualEmptyLabel"))) {
                    emptyLabel->setVisible(false);
                    list->setVisible(true);
                }
            }
        }
    });
    connect(&dialog, &SettingsDialog::resetSettingsRequested, this, [this, &dialog] {
        QDialog confirm(&dialog);
        confirm.setWindowTitle(tr("Reset Settings"));
        auto *confirmLayout = new QVBoxLayout(&confirm);
        auto *message = new QLabel(tr("This will reset Blinq Messenger to its default settings. Your preferences, profile, saved lists, queued local data, and chat history will be cleared.\n\nBlinq Messenger will restart automatically when the reset is complete.\n\nDo you want to continue?"), &confirm);
        message->setWordWrap(true);
        confirmLayout->addWidget(message);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &confirm);
        auto *resetButton = buttons->addButton(tr("Reset"), QDialogButtonBox::DestructiveRole);
        resetButton->setStyleSheet(QStringLiteral("QPushButton { background:#fee2e2; color:#7f1d1d; border-color:#fecaca; }"
                                                  "QPushButton:hover { background:#fecaca; }"));
        connect(buttons, &QDialogButtonBox::rejected, &confirm, &QDialog::reject);
        connect(resetButton, &QPushButton::clicked, &confirm, &QDialog::accept);
        confirmLayout->addWidget(buttons);
        if (confirm.exec() != QDialog::Accepted) {
            return;
        }

        resetAppSettings();
        dialog.reject();
        restartApplication();
    });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const AppSettings previousSettings = m_settings;
    const bool wasHidingTypingIndicator = previousSettings.hideTypingIndicator;
    AppSettings updatedSettings = dialog.settings();
    updatedSettings.appMode = previousSettings.appMode;
    updatedSettings.internetServerHost = previousSettings.internetServerHost;
    updatedSettings.internetServerPort = previousSettings.internetServerPort;
    updatedSettings.internetAuthToken = previousSettings.internetAuthToken;
    updatedSettings.internetBlinqId = previousSettings.internetBlinqId;
    updatedSettings.internetDisplayName = previousSettings.internetDisplayName;
    m_settings = updatedSettings;
    if (!wasHidingTypingIndicator && m_settings.hideTypingIndicator) {
        for (const QString &peerId : m_peers.keys()) {
            m_chatService->sendTypingState(peerId, false);
        }
    }
    m_blockedPeers.clear();
    m_blockedPeerNames.clear();
    for (auto it = m_settings.blockedPeers.constBegin(); it != m_settings.blockedPeers.constEnd(); ++it) {
        m_blockedPeers.insert(it.key());
        m_blockedPeerNames.insert(it.key(), it.value());
    }
    saveSettings();
    applyTheme();
    const bool showMedia = m_settings.showPlayingInfo && !m_currentMediaText.isEmpty();
    if (m_mediaLabel) {
        m_mediaLabel->setText(showMedia ? tr("Playing: %1").arg(m_currentMediaText) : QString());
        m_mediaLabel->setVisible(showMedia);
    }
    m_chatService->setLocalPersonalMessage(showMedia ? tr("Now playing: %1").arg(m_currentMediaText) : m_manualPersonalMessage);
    if (isInternetMode()) {
        if (m_internetRelay->isAuthenticated()) {
            m_internetRelay->setSearchable(m_settings.internetSearchable);
        }
        rebuildInternetContacts();
        updateNetworkStatus();
    } else {
        const auto peers = m_peers.values();
        for (const ChatPeer &peer : peers) {
            upsertPeer(peer);
        }
    }
    for (ChatWindow *window : std::as_const(m_chatWindows)) {
        window->setHistoryEnabled(m_settings.saveHistory);
    }
    updateTrayTooltip();
}

void MainWindow::showGroupManager()
{
    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Manage Contact Groups"));
    dialog.resize(330, 360);
    dialog.setStyleSheet(appStyleSheet());

    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    auto *buttonRow = new QHBoxLayout();
    auto *addButton = new QPushButton(tr("New..."), &dialog);
    auto *renameButton = new QPushButton(tr("Rename..."), &dialog);
    auto *deleteButton = new QPushButton(tr("Delete"), &dialog);
    auto *closeButton = new QPushButton(tr("Close"), &dialog);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(renameButton);
    buttonRow->addWidget(deleteButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    const QStringList reservedNames = {tr("Favorites"), tr("Online"), tr("Away / Idle"), tr("Busy"), tr("Offline")};
    auto isReservedName = [&reservedNames](const QString &name) {
        for (const QString &reserved : reservedNames) {
            if (reserved.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    auto existingGroupName = [this](const QString &name, const QString &except = QString()) {
        for (const QString &existing : std::as_const(m_customContactGroups)) {
            if (existing.compare(name, Qt::CaseInsensitive) == 0 && existing.compare(except, Qt::CaseInsensitive) != 0) {
                return existing;
            }
        }
        return QString();
    };
    auto selectedGroupName = [list] {
        QListWidgetItem *item = list->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    auto refreshList = [&] {
        const QString selected = selectedGroupName();
        list->clear();
        for (const QString &groupName : std::as_const(m_customContactGroups)) {
            int count = 0;
            for (auto it = m_peerGroups.constBegin(); it != m_peerGroups.constEnd(); ++it) {
                if (it.value() == groupName) {
                    ++count;
                }
            }
            auto *item = new QListWidgetItem(tr("%1 (%2 contacts)").arg(groupName).arg(count),
                                             list);
            item->setData(Qt::UserRole, groupName);
            if (groupName == selected) {
                list->setCurrentItem(item);
            }
        }
        if (!list->currentItem() && list->count() > 0) {
            list->setCurrentRow(0);
        }
    };
    auto updateButtons = [&] {
        const bool hasSelection = list->currentItem() != nullptr;
        renameButton->setEnabled(hasSelection);
        deleteButton->setEnabled(hasSelection);
    };

    connect(addButton, &QPushButton::clicked, &dialog, [&, this] {
        bool ok = false;
        const QString groupName = sanitizedGroupName(QInputDialog::getText(&dialog,
                                                                           tr("New Contact Group"),
                                                                           tr("Group name:"),
                                                                           QLineEdit::Normal,
                                                                           QString(),
                                                                           &ok));
        if (!ok || groupName.isEmpty()) {
            return;
        }
        if (isReservedName(groupName)) {
            QMessageBox::information(&dialog, tr("Reserved Name"), tr("That name is used by a built-in group."));
            return;
        }
        if (!existingGroupName(groupName).isEmpty()) {
            QMessageBox::information(&dialog, tr("Group Exists"), tr("A group with that name already exists."));
            return;
        }
        m_customContactGroups.append(groupName);
        refreshList();
        rebuildContactList();
        saveSettings();
    });

    connect(renameButton, &QPushButton::clicked, &dialog, [&, this] {
        const QString oldName = selectedGroupName();
        if (oldName.isEmpty()) {
            return;
        }
        bool ok = false;
        const QString newName = sanitizedGroupName(QInputDialog::getText(&dialog,
                                                                         tr("Rename Group"),
                                                                         tr("Group name:"),
                                                                         QLineEdit::Normal,
                                                                         oldName,
                                                                         &ok));
        if (!ok || newName.isEmpty() || newName == oldName) {
            return;
        }
        if (isReservedName(newName)) {
            QMessageBox::information(&dialog, tr("Reserved Name"), tr("That name is used by a built-in group."));
            return;
        }
        if (!existingGroupName(newName, oldName).isEmpty()) {
            QMessageBox::information(&dialog, tr("Group Exists"), tr("A group with that name already exists."));
            return;
        }
        const int index = m_customContactGroups.indexOf(oldName);
        if (index >= 0) {
            m_customContactGroups[index] = newName;
        }
        for (auto it = m_peerGroups.begin(); it != m_peerGroups.end(); ++it) {
            if (it.value() == oldName) {
                it.value() = newName;
            }
        }
        const QString oldKey = QStringLiteral("custom:%1").arg(oldName);
        const bool wasCollapsed = m_groupCollapsed.take(oldKey);
        if (wasCollapsed) {
            m_groupCollapsed.insert(QStringLiteral("custom:%1").arg(newName), true);
        }
        refreshList();
        rebuildContactList();
        saveSettings();
    });

    connect(deleteButton, &QPushButton::clicked, &dialog, [&, this] {
        const QString groupName = selectedGroupName();
        if (groupName.isEmpty()) {
            return;
        }
        if (QMessageBox::question(&dialog,
                                  tr("Delete Group"),
                                  tr("Delete \"%1\" and move its contacts back to status groups?").arg(groupName),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        m_customContactGroups.removeAll(groupName);
        for (auto it = m_peerGroups.begin(); it != m_peerGroups.end(); ) {
            if (it.value() == groupName) {
                it = m_peerGroups.erase(it);
            } else {
                ++it;
            }
        }
        m_groupCollapsed.remove(QStringLiteral("custom:%1").arg(groupName));
        refreshList();
        rebuildContactList();
        saveSettings();
    });

    connect(list, &QListWidget::currentItemChanged, &dialog, [updateButtons](QListWidgetItem *, QListWidgetItem *) {
        updateButtons();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    refreshList();
    updateButtons();
    dialog.exec();
}

void MainWindow::resetAppSettings()
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    settings.clear();
    settings.sync();

    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::homePath() + QStringLiteral("/BlinqMessenger");
    }
    QDir historyDir(QDir(basePath).filePath(QStringLiteral("history")));
    if (historyDir.exists()) {
        historyDir.removeRecursively();
    }

    m_settings = AppSettings();
    m_blockedPeers.clear();
    m_blockedPeerNames.clear();
    m_offlineMessages.clear();
    m_groupCollapsed.clear();
    m_favoritePeers.clear();
    m_knownPeers.clear();
    m_customContactGroups.clear();
    m_peerGroups.clear();
    m_protocolWarningsShown.clear();
    m_lastRateLimitedActions.clear();
    applyLaunchWithWindowsSetting();

    if (m_muteSoundsAction) {
        m_muteSoundsAction->setText(tr("Mute Sounds"));
        m_muteSoundsAction->setIcon(QIcon(QStringLiteral(":/icons/assets/mute.png")));
    }
    for (ChatWindow *window : std::as_const(m_chatWindows)) {
        window->setHistoryEnabled(m_settings.saveHistory);
    }
    applyTheme();
    updateTrayTooltip();
}

void MainWindow::backupAppData()
{
    saveSettings();

    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath();
    }

    const QString defaultName = QStringLiteral("BlinqMessengerBackup-%1.blinqbackup")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmm")));
    const QString filePath = QFileDialog::getSaveFileName(dialogParent(),
                                                          tr("Backup Blinq Data"),
                                                          QDir(documents).filePath(defaultName),
                                                          tr("Blinq Backup (*.blinqbackup)"));
    if (filePath.isEmpty()) {
        return;
    }

    QJsonObject backup;
    backup.insert(QStringLiteral("type"), QStringLiteral("BlinqMessengerBackup"));
    backup.insert(QStringLiteral("version"), 1);
    backup.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    backup.insert(QStringLiteral("appVersion"), QCoreApplication::applicationVersion());
    backup.insert(QStringLiteral("settings"), settingsToJson());
    backup.insert(QStringLiteral("history"), historyToJson());

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(dialogParent(), tr("Backup Failed"), tr("Could not create the backup file."));
        return;
    }
    file.write(QJsonDocument(backup).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(dialogParent(), tr("Backup Failed"), tr("Could not finish saving the backup file."));
        return;
    }

    statusBar()->showMessage(tr("Backup saved."), 4000);
    QMessageBox::information(dialogParent(),
                             tr("Backup Saved"),
                             tr("Your settings and chat history were saved to the backup file."));
}

void MainWindow::restoreAppData()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath();
    }

    const QString filePath = QFileDialog::getOpenFileName(dialogParent(),
                                                          tr("Restore Blinq Data"),
                                                          documents,
                                                          tr("Blinq Backup (*.blinqbackup)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(dialogParent(), tr("Restore Failed"), tr("Could not open the backup file."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(dialogParent(), tr("Restore Failed"), tr("This backup file is not valid."));
        return;
    }

    const QJsonObject backup = document.object();
    if (backup.value(QStringLiteral("type")).toString() != QStringLiteral("BlinqMessengerBackup")) {
        QMessageBox::warning(dialogParent(), tr("Restore Failed"), tr("This is not a Blinq Messenger backup file."));
        return;
    }

    if (QMessageBox::question(dialogParent(),
                              tr("Restore Backup"),
                              tr("Restoring this backup will replace your current settings and chat history. Blinq Messenger will restart automatically after the restore."),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    restoreSettingsFromJson(backup.value(QStringLiteral("settings")).toObject());
    if (!restoreHistoryFromJson(backup.value(QStringLiteral("history")).toArray())) {
        QMessageBox::warning(dialogParent(), tr("Restore Failed"), tr("Could not restore the chat history files."));
        return;
    }

    loadSettings();
    applyLaunchWithWindowsSetting();
    restartApplication();
}

void MainWindow::showWelcomeDialog()
{
    QSettings settings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    if (settings.value(QStringLiteral("app/welcomeShown"), false).toBool() || !isVisible()) {
        return;
    }

    struct WelcomePage {
        QString title;
        QString body;
    };
    const QList<WelcomePage> pages = {
        {tr("Welcome to Blinq Messenger"),
         tr("Blinq Messenger helps you chat quickly with people nearby on your LAN, or with approved contacts through Internet Mode.")},
        {tr("Chat on Your Network"),
         tr("LAN Mode can discover nearby users automatically. You can open private chats, join Public Chat, send files, share drawings, whistle, and see presence updates.")},
        {tr("Reach Contacts Online"),
         tr("Internet Mode lets you sign in with a Blinq ID, add approved contacts, and keep private conversations going outside the local network.")},
        {tr("Make It Yours"),
         tr("Set your name, avatar, status, personal message, theme, notifications, privacy options, and history preferences from the menus and Settings.")}
    };

    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Welcome"));
    dialog.setModal(true);
    dialog.setFixedSize(520, 360);
    dialog.setStyleSheet(appStyleSheet()
                         + QStringLiteral(
                               "QLabel#WelcomeTitle { font-size:24px; font-weight:850; color:#0f172a; background:transparent; }"
                               "QLabel#WelcomeSubtitle { color:#475569; font-size:11px; background:transparent; }"
                               "QLabel#WelcomePageTitle { font-size:18px; font-weight:800; color:#0f2748; background:transparent; }"
                               "QLabel#WelcomeBody { color:#334155; font-size:11.5pt; line-height:140%; background:transparent; }"
                               "QLabel#WelcomeStep { color:#64748b; font-weight:650; background:transparent; }"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(16);

    auto *header = new QWidget(&dialog);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(14);

    auto *icon = new QLabel(header);
    icon->setFixedSize(72, 72);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/assets/appicon.ico")).pixmap(64, 64));
    headerLayout->addWidget(icon);

    auto *titleBlock = new QWidget(header);
    auto *titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);
    auto *appName = new QLabel(tr("Blinq Messenger"), titleBlock);
    appName->setObjectName(QStringLiteral("WelcomeTitle"));
    auto *subtitle = new QLabel(tr("LAN and Internet chat, without the ceremony"), titleBlock);
    subtitle->setObjectName(QStringLiteral("WelcomeSubtitle"));
    titleLayout->addWidget(appName);
    titleLayout->addWidget(subtitle);
    titleLayout->addStretch();
    headerLayout->addWidget(titleBlock, 1);
    layout->addWidget(header);

    auto *pageTitle = new QLabel(&dialog);
    pageTitle->setObjectName(QStringLiteral("WelcomePageTitle"));
    pageTitle->setWordWrap(true);
    layout->addWidget(pageTitle);

    auto *body = new QLabel(&dialog);
    body->setObjectName(QStringLiteral("WelcomeBody"));
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body->setMinimumHeight(105);
    layout->addWidget(body, 1);

    auto *footer = new QWidget(&dialog);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    auto *stepLabel = new QLabel(footer);
    stepLabel->setObjectName(QStringLiteral("WelcomeStep"));
    auto *nextButton = new QPushButton(tr("Next"), footer);
    nextButton->setMinimumWidth(110);
    footerLayout->addWidget(stepLabel);
    footerLayout->addStretch(1);
    footerLayout->addWidget(nextButton);
    layout->addWidget(footer);

    int pageIndex = 0;
    const auto updatePage = [&] {
        pageTitle->setText(pages.at(pageIndex).title);
        body->setText(pages.at(pageIndex).body);
        stepLabel->setText(tr("%1 of %2").arg(pageIndex + 1).arg(pages.size()));
        nextButton->setText(pageIndex == pages.size() - 1 ? tr("Finish") : tr("Next"));
    };
    connect(nextButton, &QPushButton::clicked, &dialog, [&] {
        if (pageIndex < pages.size() - 1) {
            ++pageIndex;
            updatePage();
            return;
        }

        settings.setValue(QStringLiteral("app/welcomeShown"), true);
        settings.sync();
        dialog.accept();
    });

    updatePage();
    dialog.exec();
}

void MainWindow::showAbout()
{
    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("About Blinq Messenger"));
    dialog.setMinimumSize(480, 390);
    dialog.resize(520, 430);
    dialog.setStyleSheet(appStyleSheet()
                         + QStringLiteral(
                               "QLabel#AboutTitle { font-size:24px; font-weight:800; color:#0f172a; background:transparent; }"
                               "QLabel#AboutSubtitle { color:#475569; background:transparent; }"
                               "QWidget#AboutHeader { background:transparent; border:none; }"
                               "QTextEdit#AboutText { background:#ffffff; border:1px solid #cbd5e1; border-radius:8px; padding:10px; }"));

    auto *layout = new QVBoxLayout(&dialog);

    auto *header = new QWidget(&dialog);
    header->setObjectName(QStringLiteral("AboutHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 12, 14, 12);
    headerLayout->setSpacing(12);

    auto *icon = new QLabel(header);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/assets/appicon.ico")).pixmap(56, 56));
    icon->setFixedSize(64, 64);
    icon->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(icon);

    auto *titleBlock = new QWidget(header);
    auto *titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("Blinq Messenger"), titleBlock);
    title->setObjectName(QStringLiteral("AboutTitle"));
    auto *subtitle = new QLabel(tr("Freeware LAN and Internet messenger"), titleBlock);
    subtitle->setObjectName(QStringLiteral("AboutSubtitle"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    headerLayout->addWidget(titleBlock, 1);
    layout->addWidget(header);

    auto *tabs = new QTabWidget(&dialog);
    auto *aboutText = new QTextEdit(tabs);
    aboutText->setObjectName(QStringLiteral("AboutText"));
    aboutText->setReadOnly(true);
    aboutText->setHtml(tr(
        "<h3>About Blinq Messenger</h3>"
        "<p>Blinq Messenger is a freeware messenger for quick private chats, public LAN chat, file sharing, drawings, profile pictures, themes, and presence/status updates.</p>"
        "<p>In LAN Mode, the app discovers nearby users on the same local network and communicates directly between devices whenever possible. In Internet Mode, it can connect through the Blinq Internet service so approved contacts can chat outside the local network.</p>"
        "<p>LAN Mode is best for trusted home, school, office, lab, or small-team networks. Internet Mode adds account-based contacts and remote private chats while keeping the same familiar chat window experience.</p>"
        "<p>Developer: <b>Exe Innovate</b></p>"));
    tabs->addTab(aboutText, tr("About"));

    auto *debugText = new QTextEdit(tabs);
    debugText->setObjectName(QStringLiteral("AboutText"));
    debugText->setReadOnly(true);
    QStringList debug;
    debug << tr("Application: %1").arg(QCoreApplication::applicationName());
    debug << tr("Version: %1").arg(QCoreApplication::applicationVersion());
    debug << tr("Developer: Exe Innovate");
    debug << tr("Qt version: %1").arg(QString::fromLatin1(qVersion()));
    debug << tr("Build date: %1 %2").arg(QStringLiteral(__DATE__), QStringLiteral(__TIME__));
    debug << QString();
    debug << tr("Mode:");
    debug << tr("- Current mode: %1").arg(isInternetMode() ? tr("Internet Mode") : tr("LAN Mode"));
    if (isInternetMode()) {
        debug << tr("- Blinq ID: %1").arg(m_settings.internetBlinqId.isEmpty() ? tr("not signed in") : m_settings.internetBlinqId);
        debug << tr("- Internet server: %1:%2")
                     .arg(m_settings.internetServerHost,
                          QString::number(m_settings.internetServerPort));
        debug << tr("- Server state: %1").arg(m_internetRelay->isAuthenticated() ? tr("authenticated") : tr("not authenticated"));
    }
    debug << QString();
    debug << tr("Network:");
    debug << tr("- Discovery: UDP port %1 (%2)")
                 .arg(QString::number(m_chatService->discoveryPort()),
                      m_chatService->discoveryAvailable() ? tr("available") : tr("not bound"));
    if (!m_chatService->discoveryAvailable() && !m_chatService->discoveryError().isEmpty()) {
        debug << tr("  Discovery error: %1").arg(m_chatService->discoveryError());
    }
    debug << tr("- Direct messages/files: TCP port %1").arg(QString::number(m_chatService->directTcpPort()));
    debug << tr("- Preferred direct TCP range: %1-%2")
                 .arg(QString::number(m_chatService->preferredDirectTcpPortStart()),
                      QString::number(m_chatService->preferredDirectTcpPortEnd()));
    if (m_chatService->usingFallbackDirectTcpPort()) {
        debug << tr("- Using an OS-assigned direct TCP fallback port");
    }
    debug << tr("- LAN public chat and LAN presence use local-network peer discovery");
    debug << tr("- Internet private chats use the configured Blinq Internet server when Internet Mode is active");
    debug << QString();
    debug << tr("Troubleshooting:");
    debug << tr("- Allow Blinq Messenger through Windows Firewall on Private networks");
    debug << tr("- Make sure both computers are on the same LAN/VLAN");
    debug << tr("- VPN or Guest Wi-Fi networks may block local discovery");
    debug << tr("- For Internet Mode, make sure the server is reachable and your Blinq account is signed in");
    debugText->setPlainText(debug.join(QLatin1Char('\n')));
    tabs->addTab(debugText, tr("Debug Info"));

    auto *licenseText = new QTextEdit(tabs);
    licenseText->setObjectName(QStringLiteral("AboutText"));
    licenseText->setReadOnly(true);
    licenseText->setHtml(tr(
        "<h3>Blinq Messenger Freeware License</h3>"
        "<p>Blinq Messenger is provided as freeware by Exe Innovate. You may use the application to communicate, share messages, send files, and use LAN or Internet chat features for personal or business work.</p>"
        "<h3>Responsible use</h3>"
        "<p>You are responsible for the messages, files, images, drawings, profile information, and other content you send or receive with the application. Do not use Blinq Messenger to harass, impersonate, distribute illegal content, invade privacy, violate workplace or school rules, or break any applicable law.</p>"
        "<p>Exe Innovate is not responsible for how users choose to use the application, for content exchanged between users, or for consequences caused by misuse, unauthorized sharing, offensive messages, unsafe files, or communication with unintended recipients.</p>"
        "<h3>Network, accounts, and privacy</h3>"
        "<p>LAN Mode is designed for local-network communication and peer discovery. Users are responsible for choosing trusted networks, configuring firewall rules, and verifying who they communicate with.</p>"
        "<p>Internet Mode uses a Blinq account and server connection to reach approved contacts outside the LAN. Users are responsible for protecting their account credentials, choosing contacts carefully, and following applicable rules for remote communication.</p>"
        "<h3>Files and security</h3>"
        "<p>Always be careful with files received from other users. Exe Innovate is not responsible for data loss, malware, unwanted files, disclosure of private information, or damage caused by files, images, links, or content exchanged through the app.</p>"
        "<h3>Included components</h3>"
        "<p>The application includes its own code and third-party components, including Qt libraries and interface resources. Those components remain subject to their respective licenses.</p>"
        "<h3>No warranty</h3>"
        "<p>The software is provided as-is, without warranty of any kind. Exe Innovate does not guarantee uninterrupted communication, message delivery, compatibility, security, or fitness for a particular purpose.</p>"
        "<h3>Redistribution</h3>"
        "<p>Redistribution is allowed only when the application remains unmodified and credited to Exe Innovate. Commercial resale, reverse branding, or claiming authorship is not permitted.</p>"));
    tabs->addTab(licenseText, tr("License"));
    layout->addWidget(tabs, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *closeButton = new QPushButton(tr("Close"), &dialog);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    dialog.exec();
}

void MainWindow::showHelp()
{
    const QString helpPath = QCoreApplication::applicationDirPath() + QStringLiteral("/help.html");
    QDesktopServices::openUrl(QUrl::fromLocalFile(helpPath));
}

void MainWindow::showUpdateDialog()
{
    QDialog dialog(dialogParent());
    dialog.setWindowTitle(tr("Check for Updates"));
    dialog.resize(560, 460);
    dialog.setStyleSheet(appStyleSheet());

    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(tr("Blinq Messenger Updates"), &dialog);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #0f172a;"));
    layout->addWidget(title);

    auto *versionLabel = new QLabel(tr("Current version: %1").arg(QCoreApplication::applicationVersion()), &dialog);
    layout->addWidget(versionLabel);

    auto *statusLabel = new QLabel(tr("Checking for updates..."), &dialog);
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto *changelog = new QTextEdit(&dialog);
    changelog->setReadOnly(true);
    changelog->setPlaceholderText(tr("Release notes will appear here."));
    layout->addWidget(changelog, 1);

    auto *progress = new QProgressBar(&dialog);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setVisible(false);
    layout->addWidget(progress);

    auto *buttonRow = new QHBoxLayout();
    auto *downloadButton = new QPushButton(tr("Download Update"), &dialog);
    downloadButton->setEnabled(false);
    auto *runInstallerButton = new QPushButton(tr("Run Installer"), &dialog);
    runInstallerButton->setVisible(false);
    auto *closeButton = new QPushButton(tr("Close"), &dialog);
    buttonRow->addStretch(1);
    buttonRow->addWidget(downloadButton);
    buttonRow->addWidget(runInstallerButton);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    auto *manager = new QNetworkAccessManager(&dialog);
    QString installerUrl;
    QString installerPath;
    QByteArray downloadBytes;

    auto requestFor = [](const QUrl &url) {
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        return request;
    };

    QNetworkReply *metadataReply = manager->get(requestFor(QUrl(UpdateMetadataUrl)));
    connect(metadataReply, &QNetworkReply::finished, &dialog, [&, metadataReply] {
        metadataReply->deleteLater();
        if (metadataReply->error() != QNetworkReply::NoError) {
            statusLabel->setText(tr("Could not check for updates: %1").arg(metadataReply->errorString()));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(metadataReply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            statusLabel->setText(tr("The update information file is not valid."));
            return;
        }

        const QJsonObject object = document.object();
        const QString latestVersion = object.value(QStringLiteral("version")).toString();
        installerUrl = object.value(QStringLiteral("installerUrl")).toString();
        const QString releaseDate = object.value(QStringLiteral("releaseDate")).toString();

        QStringList notes;
        const QJsonValue changelogValue = object.value(QStringLiteral("changelog"));
        if (changelogValue.isArray()) {
            const QJsonArray array = changelogValue.toArray();
            for (const QJsonValue &value : array) {
                notes.append(QStringLiteral("- %1").arg(value.toString()));
            }
        } else {
            notes = changelogValue.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }

        changelog->setPlainText(notes.join(QLatin1Char('\n')));
        if (latestVersion.isEmpty() || installerUrl.isEmpty()) {
            statusLabel->setText(tr("The update information is missing a version or installer link."));
            return;
        }

        const QString releaseText = releaseDate.isEmpty() ? QString() : tr(" Released %1.").arg(releaseDate);
        if (isNewerVersion(latestVersion, QCoreApplication::applicationVersion())) {
            statusLabel->setText(tr("Version %1 is available.%2 You can install it over your current copy. Settings and chat history should be kept.")
                                     .arg(latestVersion, releaseText));
            downloadButton->setEnabled(true);
        } else {
            statusLabel->setText(tr("You are up to date. Latest version: %1.%2").arg(latestVersion, releaseText));
        }
    });

    connect(downloadButton, &QPushButton::clicked, &dialog, [&] {
        const QUrl url(installerUrl);
        if (!url.isValid()) {
            statusLabel->setText(tr("The installer download link is not valid."));
            return;
        }

        QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (downloads.isEmpty()) {
            downloads = QDir::homePath();
        }
        QDir dir(downloads);
        dir.mkpath(QStringLiteral("Blinq Messenger Updates"));
        dir.cd(QStringLiteral("Blinq Messenger Updates"));
        QString fileName = QFileInfo(url.path()).fileName();
        if (fileName.isEmpty()) {
            fileName = QStringLiteral("BlinqMessengerSetup-%1.exe").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")));
        }
        installerPath = dir.filePath(fileName);
        downloadBytes.clear();
        progress->setVisible(true);
        progress->setValue(0);
        downloadButton->setEnabled(false);
        statusLabel->setText(tr("Downloading update..."));

        QNetworkReply *downloadReply = manager->get(requestFor(url));
        connect(downloadReply, &QNetworkReply::downloadProgress, &dialog, [progress](qint64 received, qint64 total) {
            if (total > 0) {
                progress->setRange(0, 100);
                progress->setValue(static_cast<int>((received * 100) / total));
            } else {
                progress->setRange(0, 0);
            }
        });
        connect(downloadReply, &QNetworkReply::readyRead, &dialog, [&, downloadReply] {
            downloadBytes += downloadReply->readAll();
        });
        connect(downloadReply, &QNetworkReply::finished, &dialog, [&, downloadReply] {
            downloadBytes += downloadReply->readAll();
            downloadReply->deleteLater();
            progress->setRange(0, 100);
            if (downloadReply->error() != QNetworkReply::NoError) {
                statusLabel->setText(tr("Download failed: %1").arg(downloadReply->errorString()));
                downloadButton->setEnabled(true);
                return;
            }

            QSaveFile file(installerPath);
            if (!file.open(QIODevice::WriteOnly)) {
                statusLabel->setText(tr("Could not save the installer."));
                downloadButton->setEnabled(true);
                return;
            }
            file.write(downloadBytes);
            if (!file.commit()) {
                statusLabel->setText(tr("Could not finish saving the installer."));
                downloadButton->setEnabled(true);
                return;
            }

            progress->setValue(100);
            statusLabel->setText(tr("Update downloaded. Close Blinq Messenger before installing, or use Run Installer and the app will close automatically."));
            runInstallerButton->setVisible(true);
        });
    });

    connect(runInstallerButton, &QPushButton::clicked, &dialog, [&] {
        if (installerPath.isEmpty() || !QFileInfo::exists(installerPath)) {
            statusLabel->setText(tr("The downloaded installer could not be found."));
            return;
        }
        QProcess::startDetached(installerPath);
        m_reallyQuit = true;
        qApp->quit();
    });

    dialog.exec();
}

void MainWindow::showConnectionInfo()
{
    auto *dialog = new QDialog(dialogParent());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Network Diagnostics"));
    dialog->resize(520, 420);

    auto *layout = new QVBoxLayout(dialog);
    auto *title = new QLabel(tr("Connection Info"), dialog);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600;"));
    layout->addWidget(title);

    QStringList lines;
    lines << tr("Discovery: UDP port %1 (%2)")
                 .arg(QString::number(m_chatService->discoveryPort()),
                      m_chatService->discoveryAvailable() ? tr("available") : tr("not bound"));
    if (!m_chatService->discoveryAvailable() && !m_chatService->discoveryError().isEmpty()) {
        lines << tr("Discovery error: %1").arg(m_chatService->discoveryError());
    }
    lines << tr("Direct messages/files: TCP port %1").arg(QString::number(m_chatService->directTcpPort()));
    lines << tr("Preferred direct TCP range: %1-%2")
                 .arg(QString::number(m_chatService->preferredDirectTcpPortStart()),
                      QString::number(m_chatService->preferredDirectTcpPortEnd()));
    if (m_chatService->usingFallbackDirectTcpPort()) {
        lines << tr("Using an OS-assigned fallback TCP port because the preferred range was unavailable.");
        lines << tr("Manual Direct Connect may need the exact IP:port shown on this PC.");
    }
    lines << QString();
    lines << m_chatService->connectionInfo();
    lines << QString();
    lines << tr("Local IPv4 addresses:");
    const auto addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            lines << tr("- %1").arg(address.toString());
        }
    }
    lines << QString();
    lines << tr("If peers do not appear, check:");
    lines << tr("- Windows Firewall allows Blinq Messenger on Private networks.");
    lines << tr("- Both PCs are on the same LAN/VLAN and not Guest Wi-Fi.");
    lines << tr("- VPN software is not blocking local broadcast traffic.");
    lines << tr("- If discovery is not bound on this PC, you may not see peers automatically.");
    lines << tr("- Manual Connect by IP works when broadcast discovery is blocked.");

    auto *info = new QTextEdit(dialog);
    info->setReadOnly(true);
    info->setText(lines.join(QLatin1Char('\n')));
    layout->addWidget(info, 1);

    auto *buttonRow = new QHBoxLayout();
    auto *inviteButton = new QPushButton(tr("Copy Invite"), dialog);
    auto *reconnectButton = new QPushButton(tr("Reconnect Saved"), dialog);
    auto *scanButton = new QPushButton(tr("Scan LAN"), dialog);
    auto *firewallButton = new QPushButton(tr("Firewall Help"), dialog);
    auto *closeButton = new QPushButton(tr("Close"), dialog);
    buttonRow->addWidget(inviteButton);
    buttonRow->addWidget(reconnectButton);
    buttonRow->addWidget(scanButton);
    buttonRow->addWidget(firewallButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    connect(inviteButton, &QPushButton::clicked, this, &MainWindow::showConnectionInviteDialog);
    connect(reconnectButton, &QPushButton::clicked, this, &MainWindow::reconnectSavedContacts);
    connect(scanButton, &QPushButton::clicked, this, &MainWindow::scanLocalSubnet);
    connect(firewallButton, &QPushButton::clicked, this, &MainWindow::showFirewallHelperDialog);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addLayout(buttonRow);
    dialog->show();
}

void MainWindow::updateNetworkStatus()
{
    if (!m_networkStatus || !m_chatService) {
        return;
    }

    if (isInternetMode()) {
        const QString state = m_internetRelay->isAuthenticated() ? tr("Server connected") : tr("Server unavailable");
        m_networkStatus->setText(tr("Internet Mode  |  %1").arg(state));
        m_networkStatus->setToolTip(m_internetRelay->isAuthenticated()
                                        ? tr("Connected to the Blinq Internet server.")
                                        : tr("The Blinq Internet server is not connected. If it is unavailable, Blinq Messenger will switch to LAN Mode."));
        return;
    }

    const QString discovery = m_chatService->discoveryAvailable() ? tr("Discovery OK") : tr("Discovery unavailable");
    const QString tcp = m_chatService->directTcpPort() > 0
                            ? tr("TCP %1").arg(m_chatService->directTcpPort())
                            : tr("TCP starting");
    const QString fallback = m_chatService->usingFallbackDirectTcpPort() ? tr("fallback") : tr("preferred");
    m_networkStatus->setText(tr("%1  |  %2 (%3)").arg(discovery, tcp, fallback));
    m_networkStatus->setToolTip(m_chatService->discoveryAvailable()
                                    ? tr("Local discovery is bound. Blinq can receive LAN presence broadcasts.")
                                    : tr("Local discovery is not bound."));
}

bool MainWindow::showIncomingNotification(const ChatPeer &peer, const QString &message)
{
    if (!m_settings.showNotifications || !m_settings.directMessageNotifications) {
        return false;
    }

    m_pendingNotificationPeerId = peer.id;
    m_pendingNotificationAction = QStringLiteral("openChat");
    m_pendingNotificationIsPublic = false;
    showNativeNotification(peer.name.isEmpty() ? tr("Blinq Messenger message") : peer.name,
                           message,
                           avatarIcon(peer.avatarData, peer.name));
    return true;
}

bool MainWindow::showPublicIncomingNotification(const QString &sender, const QString &message)
{
    if (!m_settings.showNotifications || !m_settings.publicChatNotifications) {
        return false;
    }

    m_pendingNotificationPeerId.clear();
    m_pendingNotificationAction = QStringLiteral("openPublic");
    m_pendingNotificationIsPublic = true;
    showNativeNotification(tr("Public chat - %1").arg(sender),
                           message,
                           windowIcon());
    return true;
}

void MainWindow::showNativeNotification(const QString &title, const QString &message, const QIcon &icon)
{
    if (!m_trayIcon || !m_trayIcon->isVisible()) {
        return;
    }

    m_trayIcon->showMessage(title, message, icon, 7000);
}

void MainWindow::openPendingNotification()
{
    if (m_pendingNotificationAction == QStringLiteral("openPublic") || m_pendingNotificationIsPublic) {
        openPublicChat();
        return;
    }

    if (!m_pendingNotificationPeerId.isEmpty()) {
        openChat(m_pendingNotificationPeerId);
    }
}

void MainWindow::markChatActive()
{
    m_chatService->setLocalIdle(false);
    if (m_chatIdleTimer) {
        m_chatIdleTimer->start();
    }
}

void MainWindow::markChatIdle()
{
    m_chatService->setLocalIdle(true);
}

void MainWindow::updateTrayTooltip()
{
    if (m_trayIcon) {
        m_trayIcon->setToolTip(isInternetMode()
                                   ? tr("Blinq Messenger - %1").arg(m_settings.internetBlinqId.isEmpty() ? tr("Internet Mode") : m_settings.internetBlinqId)
                                   : tr("Blinq Messenger - %1").arg(m_chatService->localStatus()));
    }
}

void MainWindow::updateEmptyContactsLabel()
{
    bool hasContacts = false;
    for (const QString &peerId : m_knownPeers.keys()) {
        if (!m_blockedPeers.contains(peerId)) {
            hasContacts = true;
            break;
        }
    }
    if (m_emptyContactsLabel) {
        m_emptyContactsLabel->setText(m_isLoading ? tr("Loading Blinq Messenger...") : tr("No Online Contacts"));
        m_emptyContactsLabel->setVisible(!hasContacts);
    }
    if (m_peerList) {
        m_peerList->setVisible(hasContacts);
    }
}

void MainWindow::updateContactsLabel()
{
    if (m_contactsLabel) {
        m_contactsLabel->setText(tr("Contacts"));
    }
    if (m_contactsTotalLabel) {
        int total = 0;
        for (const QString &peerId : m_knownPeers.keys()) {
            if (!m_blockedPeers.contains(peerId)) {
                ++total;
            }
        }
        m_contactsTotalLabel->setText(tr("%1 total").arg(compactCount(total)));
    }
}

QString MainWindow::contactGroupKey(const ChatPeer &peer) const
{
    const QString customGroup = m_peerGroups.value(peer.id);
    if (!customGroup.isEmpty() && m_customContactGroups.contains(customGroup)) {
        return QStringLiteral("custom:%1").arg(customGroup);
    }
    if (peer.status == tr("Offline")) {
        return QStringLiteral("offline");
    }
    if (peer.status == tr("Busy") || peer.status == tr("Do Not Disturb")) {
        return QStringLiteral("busy");
    }
    if (peer.status == tr("Away") || peer.status == tr("Idle")) {
        return QStringLiteral("away");
    }
    return QStringLiteral("online");
}

QString MainWindow::contactGroupTitle(const QString &groupKey, int count) const
{
    if (groupKey == QStringLiteral("favorites")) {
        return tr("Favorites (%1)").arg(compactCount(count));
    }
    if (groupKey.startsWith(QStringLiteral("custom:"))) {
        return tr("%1 (%2)").arg(contactCustomGroupName(groupKey), compactCount(count));
    }
    if (groupKey == QStringLiteral("online")) {
        return tr("Online (%1)").arg(compactCount(count));
    }
    if (groupKey == QStringLiteral("away")) {
        return tr("Away / Idle (%1)").arg(compactCount(count));
    }
    if (groupKey == QStringLiteral("busy")) {
        return tr("Busy (%1)").arg(compactCount(count));
    }
    return tr("Offline (%1)").arg(compactCount(count));
}

QString MainWindow::contactCustomGroupName(const QString &groupKey) const
{
    return groupKey.startsWith(QStringLiteral("custom:")) ? groupKey.mid(7) : QString();
}

void MainWindow::showSignInNotification(const ChatPeer &peer)
{
    if (peer.id.isEmpty()) {
        return;
    }
    if (m_isLoading || m_suppressSignInNotifications) {
        m_seenOnlineNotifications.insert(peer.id);
        return;
    }
    if (m_seenOnlineNotifications.contains(peer.id)) {
        return;
    }
    if (!m_settings.showNotifications || !m_settings.directMessageNotifications) {
        return;
    }

    m_seenOnlineNotifications.insert(peer.id);
    const QString name = peer.name.isEmpty() ? tr("A contact") : peer.name;
    QString detail = peer.personalMessage.trimmed();
    if (detail.isEmpty()) {
        detail = peer.status.isEmpty() ? tr("Available") : peer.status;
    }
    m_pendingNotificationPeerId = peer.id;
    m_pendingNotificationAction = QStringLiteral("openChat");
    m_pendingNotificationIsPublic = false;
    showNativeNotification(tr("%1 just came online").arg(name), detail, avatarIcon(peer.avatarData, name));
    playNotificationSound();
}

void MainWindow::rebuildContactList()
{
    if (!m_peerList) {
        return;
    }

    const QString selectedId = selectedPeerId();
    m_peerList->clear();
    m_peerItems.clear();

    QHash<QString, QList<ChatPeer>> groupedPeers;
    for (const ChatPeer &peer : std::as_const(m_knownPeers)) {
        if (peer.id.isEmpty() || m_blockedPeers.contains(peer.id)) {
            continue;
        }
        groupedPeers[contactGroupKey(peer)].append(peer);
        if (m_favoritePeers.contains(peer.id)) {
            groupedPeers[QStringLiteral("favorites")].append(peer);
        }
    }

    QStringList groupOrder = {
        QStringLiteral("favorites")
    };
    for (const QString &groupName : m_customContactGroups) {
        if (!groupName.trimmed().isEmpty()) {
            groupOrder.append(QStringLiteral("custom:%1").arg(groupName));
        }
    }
    groupOrder << QStringLiteral("online")
               << QStringLiteral("away")
               << QStringLiteral("busy")
               << QStringLiteral("offline");

    for (const QString &groupKey : groupOrder) {
        QList<ChatPeer> peers = groupedPeers.value(groupKey);
        std::sort(peers.begin(), peers.end(), [](const ChatPeer &left, const ChatPeer &right) {
            return QString::localeAwareCompare(left.name.toLower(), right.name.toLower()) < 0;
        });

        if (peers.isEmpty() && !groupKey.startsWith(QStringLiteral("custom:"))) {
            continue;
        }

        auto *headerItem = new QListWidgetItem(m_peerList);
        headerItem->setData(ContactItemTypeRole, QStringLiteral("group"));
        headerItem->setData(ContactItemIdRole, groupKey);
        headerItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
        headerItem->setSizeHint(QSize(340, 30));
        m_peerList->setItemWidget(headerItem, createGroupHeader(groupKey, peers.size()));

        if (m_groupCollapsed.value(groupKey, false)) {
            continue;
        }

        for (const ChatPeer &peer : std::as_const(peers)) {
            auto *item = new QListWidgetItem(m_peerList);
            item->setData(ContactItemTypeRole, QStringLiteral("peer"));
            item->setData(ContactItemIdRole, peer.id);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            item->setSizeHint(QSize(340, 72));
            item->setText(QString());
            item->setIcon(QIcon());
            m_peerList->setItemWidget(item, createPeerRow(peer));
            m_peerItems.insert(peer.id, item);
            if (peer.id == selectedId) {
                m_peerList->setCurrentItem(item);
                item->setSelected(true);
            }
        }
    }

    updateEmptyContactsLabel();
    updateContactsLabel();
}

void MainWindow::setupSounds()
{
    m_sentSound = new QSoundEffect(this);
    m_receivedSound = new QSoundEffect(this);
    m_notificationSound = new QSoundEffect(this);
    m_whistleSound = new QSoundEffect(this);

    m_sentSound->setVolume(0.65f);
    m_receivedSound->setVolume(0.65f);
    m_notificationSound->setVolume(0.75f);
    m_whistleSound->setVolume(0.8f);

    m_sentSound->setSource(QUrl::fromLocalFile(prepareSoundFile(QStringLiteral(":/sounds/assets/chat_sent.wav"), QStringLiteral("chat_sent.wav"))));
    m_receivedSound->setSource(QUrl::fromLocalFile(prepareSoundFile(QStringLiteral(":/sounds/assets/chat_received.wav"), QStringLiteral("chat_received.wav"))));
    m_notificationSound->setSource(QUrl::fromLocalFile(prepareSoundFile(QStringLiteral(":/sounds/assets/chat_notification.wav"), QStringLiteral("chat_notification.wav"))));
    m_whistleSound->setSource(QUrl::fromLocalFile(prepareSoundFile(QStringLiteral(":/sounds/assets/whistle.wav"), QStringLiteral("whistle.wav"))));

    const auto reportSoundError = [this](QSoundEffect *sound) {
        if (sound->status() == QSoundEffect::Error) {
            statusBar()->showMessage(tr("Sound error: Could not load %1").arg(sound->source().toLocalFile()), 7000);
        }
    };
    connect(m_sentSound, &QSoundEffect::statusChanged, this, [this, reportSoundError] {
        reportSoundError(m_sentSound);
    });
    connect(m_receivedSound, &QSoundEffect::statusChanged, this, [this, reportSoundError] {
        reportSoundError(m_receivedSound);
    });
    connect(m_notificationSound, &QSoundEffect::statusChanged, this, [this, reportSoundError] {
        reportSoundError(m_notificationSound);
    });
    connect(m_whistleSound, &QSoundEffect::statusChanged, this, [this, reportSoundError] {
        reportSoundError(m_whistleSound);
    });
}

QString MainWindow::prepareSoundFile(const QString &resourcePath, const QString &fileName) const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::tempPath();
    }

    QDir dir(basePath);
    dir.mkpath(QStringLiteral("BlinqMessenger"));
    dir.cd(QStringLiteral("BlinqMessenger"));

    const QString targetPath = dir.filePath(fileName);
    QFile source(resourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return QString();
    }

    const QByteArray data = source.readAll();
    QFile target(targetPath);
    const bool needsCopy = !target.exists() || target.size() != data.size();
    if (needsCopy && target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        target.write(data);
    }

    return targetPath;
}

void MainWindow::playSentSound()
{
    playSound(m_sentSound);
}

void MainWindow::playReceivedSound()
{
    playSound(m_receivedSound);
}

void MainWindow::playNotificationSound()
{
    playSound(m_notificationSound);
}

void MainWindow::playWhistleSound()
{
    playSound(m_whistleSound);
}

void MainWindow::playSound(QSoundEffect *sound)
{
    if (!sound) {
        return;
    }
    if (m_settings.muteSounds) {
        return;
    }

#ifdef Q_OS_WIN
    playWaveFile(sound->source().toLocalFile());
    return;
#endif

    if (sound->status() == QSoundEffect::Loading) {
        if (m_pendingSoundPlays.contains(sound)) {
            return;
        }

        m_pendingSoundPlays.insert(sound);
        auto *connection = new QMetaObject::Connection;
        *connection = connect(sound, &QSoundEffect::statusChanged, this, [this, sound, connection] {
            if (sound->status() != QSoundEffect::Ready && sound->status() != QSoundEffect::Error) {
                return;
            }

            disconnect(*connection);
            delete connection;
            const bool shouldPlay = m_pendingSoundPlays.remove(sound) > 0 && sound->status() == QSoundEffect::Ready;
            if (shouldPlay) {
                playSound(sound);
            }
        });
        return;
    }

    if (sound->status() != QSoundEffect::Ready) {
        return;
    }

    if (sound->isPlaying()) {
        sound->stop();
    }
    QTimer::singleShot(0, sound, [sound] {
        sound->play();
    });
}

void MainWindow::setSoundsMuted(bool muted)
{
    m_settings.muteSounds = muted;
    if (m_muteSoundsAction) {
        m_muteSoundsAction->setText(muted ? tr("Unmute Sounds") : tr("Mute Sounds"));
        m_muteSoundsAction->setIcon(QIcon(muted ? QStringLiteral(":/icons/assets/unmute.png") : QStringLiteral(":/icons/assets/mute.png")));
    }
    saveSettings();
}

void MainWindow::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
    setFocus();
    QTimer::singleShot(250, this, &MainWindow::showWelcomeDialog);
}

void MainWindow::clearSelectedHistory()
{
    const QString peerId = selectedPeerId();
    if (peerId.isEmpty()) {
        return;
    }

    if (QMessageBox::question(dialogParent(), tr("Clear Chat"), 
        tr("Are you sure you want to permanently clear the chat history with this contact?")) != QMessageBox::Yes) {
        return;
    }

    auto *window = chatWindowFor(peerId);
    window->clearHistory();
    statusBar()->showMessage(tr("Chat history cleared."), 5000);
}

void MainWindow::blockSelectedPeer()
{
    const QString peerId = selectedPeerId();
    if (peerId.isEmpty()) {
        return;
    }

    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    const QString peerName = peer.name.isEmpty() ? tr("this user") : peer.name;
    if (QMessageBox::question(dialogParent(),
                              tr("Block User"),
                              tr("Block %1?\n\nYou will stop seeing their messages, files, public chat posts, and chat requests.").arg(peerName),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    m_blockedPeers.insert(peerId);
    m_blockedPeerNames.insert(peerId, peer.name.isEmpty() ? peerId : peer.name);
    m_settings.blockedPeers.clear();
    for (const QString &blockedPeerId : m_blockedPeers) {
        m_settings.blockedPeers.insert(blockedPeerId, m_blockedPeerNames.value(blockedPeerId, blockedPeerId));
    }
    m_favoritePeers.remove(peerId);
    m_peerGroups.remove(peerId);
    m_peers.remove(peerId);
    m_knownPeers.remove(peerId);
    m_internetPeers.remove(peerId);
    if (auto *window = m_chatWindows.take(peerId)) {
        window->close();
        window->deleteLater();
    }
    saveSettings();
    rebuildContactList();
    statusBar()->showMessage(tr("User blocked."), 5000);
}

void MainWindow::unblockSelectedPeer()
{
    const QString peerId = selectedPeerId();
    if (peerId.isEmpty()) {
        return;
    }

    m_blockedPeers.remove(peerId);
    m_blockedPeerNames.remove(peerId);
    m_settings.blockedPeers.clear();
    for (const QString &blockedPeerId : m_blockedPeers) {
        m_settings.blockedPeers.insert(blockedPeerId, m_blockedPeerNames.value(blockedPeerId, blockedPeerId));
    }
    saveSettings();
    statusBar()->showMessage(tr("User unblocked. They will reappear when their next presence update arrives."), 6000);
}

void MainWindow::buzzSelectedPeer()
{
    const QString peerId = selectedPeerId();
    if (peerId.isEmpty()) {
        return;
    }
    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    if (isInternetPeer(peerId)) {
        if (!m_peers.contains(peerId)) {
            statusBar()->showMessage(tr("That contact is offline. Whistle was not sent."), 5000);
            return;
        }
        m_internetRelay->sendBuzz(peerId);
        return;
    }
    if (peer.status == tr("Do Not Disturb")) {
        statusBar()->showMessage(tr("%1 is in Do Not Disturb. Whistle was not sent.").arg(peer.name.isEmpty() ? tr("Contact") : peer.name), 5000);
        return;
    }
    makeAvailableForOutgoingPrivateAction();
    m_chatService->sendBuzz(peerId);
}

bool MainWindow::makeAvailableForOutgoingPrivateAction(ChatWindow *window)
{
    if (m_chatService->localStatus() != tr("Do Not Disturb")) {
        return false;
    }

    if (m_statusCombo->findText(tr("Available")) < 0) {
        m_statusCombo->addItem(tr("Available"));
    }
    m_statusCombo->setCurrentText(tr("Available"));

    const QString message = tr("Your status changed to Available because you started a private conversation.");
    if (window) {
        window->appendSystemMessage(message);
    } else {
        statusBar()->showMessage(message, 5000);
    }
    return true;
}

void MainWindow::startActivity(const QString &peerId, const QString &activity)
{
    if (peerId.isEmpty()) {
        return;
    }
    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    if (peer.status == tr("Do Not Disturb")) {
        const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
        chatWindowFor(peerId)->appendSystemMessage(tr("%1 is in Do Not Disturb. Activity was not started.").arg(name));
        return;
    }
    if (!m_peers.contains(peerId)) {
        const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
        chatWindowFor(peerId)->appendSystemMessage(tr("%1 is not online. Activity was not started.").arg(name));
        return;
    }

    makeAvailableForOutgoingPrivateAction(chatWindowFor(peerId));
    if (activity == QStringLiteral("drawing")) {
        openDrawingPad(peerId);
    }
}

QString MainWindow::promptRpsChoice(const QString &title) const
{
    QDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(title);
    dialog.setStyleSheet(appStyleSheet());
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Choose your move"), &dialog);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-size:16px; font-weight:750; color:#0f172a;"));
    layout->addWidget(label);

    auto *buttonRow = new QHBoxLayout();
    QString choice;
    const QStringList choices = {tr("Rock"), tr("Paper"), tr("Scissors")};
    for (const QString &option : choices) {
        auto *button = new QPushButton(option, &dialog);
        button->setMinimumSize(100, 72);
        button->setStyleSheet(QStringLiteral("QPushButton { font-size:15px; font-weight:800; }"));
        connect(button, &QPushButton::clicked, &dialog, [&dialog, &choice, option] {
            choice = option;
            dialog.accept();
        });
        buttonRow->addWidget(button);
    }
    layout->addLayout(buttonRow);
    dialog.resize(380, 150);
    return dialog.exec() == QDialog::Accepted ? choice : QString();
}

void MainWindow::startRockPaperScissors(const QString &peerId)
{
    const QString choice = promptRpsChoice(tr("Rock-Paper-Scissors"));
    if (choice.isEmpty()) {
        return;
    }
    const QString gameId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_rpsChoices.insert(gameId, choice);
    chatWindowFor(peerId)->appendSystemMessage(tr("Rock-Paper-Scissors challenge sent. You chose %1.").arg(choice));
    m_chatService->sendGameAction(peerId, QStringLiteral("rps"), QStringLiteral("challenge"), gameId, choice);
}

QString MainWindow::tttWinner(const QString &board) const
{
    const int wins[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6}
    };
    for (const auto &line : wins) {
        const QChar mark = board.at(line[0]);
        if (mark != QLatin1Char('.') && mark == board.at(line[1]) && mark == board.at(line[2])) {
            return QString(mark);
        }
    }
    return board.contains(QLatin1Char('.')) ? QString() : QStringLiteral("draw");
}

QString MainWindow::tttBoardHtml(const QString &board) const
{
    QStringList cells;
    for (int i = 0; i < 9; ++i) {
        const QChar mark = board.at(i);
        cells << (mark == QLatin1Char('.') ? QString::number(i + 1) : QString(mark));
    }
    return tr("%1 | %2 | %3    %4 | %5 | %6    %7 | %8 | %9")
        .arg(cells.value(0), cells.value(1), cells.value(2),
             cells.value(3), cells.value(4), cells.value(5),
             cells.value(6), cells.value(7), cells.value(8));
}

bool MainWindow::promptTicTacToeMove(QString *board, const QString &mark, const QString &title) const
{
    if (!board || board->size() != 9) {
        return false;
    }

    if (!board->contains(QLatin1Char('.'))) {
        return false;
    }

    QDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(title);
    dialog.setStyleSheet(appStyleSheet());
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("You are %1. Choose a square.").arg(mark), &dialog);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-size:14px; font-weight:750; color:#0f172a;"));
    layout->addWidget(label);

    auto *grid = new QGridLayout();
    grid->setSpacing(6);
    int selectedIndex = -1;
    for (int i = 0; i < 9; ++i) {
        const QChar cell = board->at(i);
        auto *button = new QPushButton(cell == QLatin1Char('.') ? QString() : QString(cell), &dialog);
        button->setFixedSize(72, 72);
        button->setEnabled(cell == QLatin1Char('.'));
        button->setStyleSheet(QStringLiteral("QPushButton { font-size:28px; font-weight:900; }"));
        connect(button, &QPushButton::clicked, &dialog, [&dialog, &selectedIndex, i] {
            selectedIndex = i;
            dialog.accept();
        });
        grid->addWidget(button, i / 3, i % 3);
    }
    layout->addLayout(grid);
    dialog.resize(260, 320);
    if (dialog.exec() != QDialog::Accepted || selectedIndex < 0) {
        return false;
    }

    (*board)[selectedIndex] = mark.at(0);
    return true;
}

void MainWindow::startTicTacToe(const QString &peerId)
{
    const QString gameId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString board = QStringLiteral(".........");
    m_tttMarks.insert(gameId, QStringLiteral("X"));
    chatWindowFor(peerId)->appendSystemMessage(tr("Tic-Tac-Toe invite sent. You are X."));
    m_chatService->sendGameAction(peerId, QStringLiteral("tictactoe"), QStringLiteral("invite"), gameId);
    if (promptTicTacToeMove(&board, QStringLiteral("X"), tr("Tic-Tac-Toe"))) {
        chatWindowFor(peerId)->appendSystemMessage(tr("Tic-Tac-Toe: %1").arg(tttBoardHtml(board)));
        m_chatService->sendGameAction(peerId, QStringLiteral("tictactoe"), QStringLiteral("move"), gameId, QStringLiteral("X"), board);
    }
}

void MainWindow::openDrawingPad(const QString &peerId)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Drawing Pad"));
    dialog.setStyleSheet(appStyleSheet());
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Draw something and send it as an image."), &dialog);
    label->setStyleSheet(QStringLiteral("font-weight:700; color:#334155;"));
    layout->addWidget(label);

    auto *toolbar = new QWidget(&dialog);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto *canvas = new DrawingCanvas(&dialog);
    auto *undoButton = new QPushButton(tr("Undo"), toolbar);
    auto *redoButton = new QPushButton(tr("Redo"), toolbar);
    auto *penButton = new QPushButton(tr("Pen"), toolbar);
    auto *eraserButton = new QPushButton(tr("Eraser"), toolbar);
    auto *colorButton = new QPushButton(tr("Color"), toolbar);
    auto *sizeLabel = new QLabel(tr("Size"), toolbar);
    auto *sizeSlider = new QSlider(Qt::Horizontal, toolbar);
    sizeSlider->setRange(2, 28);
    sizeSlider->setValue(4);
    sizeSlider->setFixedWidth(120);

    const QList<QColor> swatches = {
        QColor(QStringLiteral("#111827")),
        QColor(QStringLiteral("#ef4444")),
        QColor(QStringLiteral("#f97316")),
        QColor(QStringLiteral("#22c55e")),
        QColor(QStringLiteral("#1d9bf0")),
        QColor(QStringLiteral("#7c3aed"))
    };
    for (const QColor &swatch : swatches) {
        auto *button = new QToolButton(toolbar);
        button->setFixedSize(24, 24);
        button->setToolTip(swatch.name());
        button->setStyleSheet(QStringLiteral("QToolButton { background:%1; border:1px solid #cbd5e1; border-radius:12px; }"
                                             "QToolButton:hover { border:2px solid #111827; }").arg(swatch.name()));
        connect(button, &QToolButton::clicked, canvas, [canvas, swatch] {
            canvas->setPenColor(swatch);
        });
        toolbarLayout->addWidget(button);
    }

    toolbarLayout->addSpacing(8);
    toolbarLayout->addWidget(penButton);
    toolbarLayout->addWidget(eraserButton);
    toolbarLayout->addWidget(colorButton);
    toolbarLayout->addSpacing(8);
    toolbarLayout->addWidget(sizeLabel);
    toolbarLayout->addWidget(sizeSlider);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(undoButton);
    toolbarLayout->addWidget(redoButton);
    layout->addWidget(toolbar);

    layout->addWidget(canvas, 0, Qt::AlignCenter);

    auto *buttons = new QHBoxLayout();
    auto *clearButton = new QPushButton(tr("Clear"), &dialog);
    auto *sendButton = new QPushButton(tr("Send Drawing"), &dialog);
    auto *cancelButton = new QPushButton(tr("Cancel"), &dialog);
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    buttons->addWidget(sendButton);
    buttons->addWidget(cancelButton);
    layout->addLayout(buttons);

    connect(undoButton, &QPushButton::clicked, canvas, &DrawingCanvas::undo);
    connect(redoButton, &QPushButton::clicked, canvas, &DrawingCanvas::redo);
    connect(penButton, &QPushButton::clicked, canvas, [canvas] {
        canvas->setEraser(false);
    });
    connect(eraserButton, &QPushButton::clicked, canvas, [canvas] {
        canvas->setEraser(true);
    });
    connect(colorButton, &QPushButton::clicked, &dialog, [canvas, &dialog] {
        const QColor color = QColorDialog::getColor(canvas->penColor(), &dialog, tr("Drawing Color"));
        if (color.isValid()) {
            canvas->setPenColor(color);
        }
    });
    connect(sizeSlider, &QSlider::valueChanged, canvas, &DrawingCanvas::setBrushSize);
    connect(clearButton, &QPushButton::clicked, canvas, &DrawingCanvas::clear);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(sendButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.resize(610, 480);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempPath.isEmpty()) {
        tempPath = QDir::tempPath();
    }
    QDir dir(tempPath);
    dir.mkpath(QStringLiteral("BlinqMessenger_Doodles"));
    dir.cd(QStringLiteral("BlinqMessenger_Doodles"));
    const QString filePath = dir.filePath(QStringLiteral("drawing_%1.png").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!canvas->image().save(filePath, "PNG")) {
        chatWindowFor(peerId)->appendSystemMessage(tr("Could not save the drawing."));
        return;
    }

    chatWindowFor(peerId)->appendSystemMessage(tr("Drawing sent."));
    if (isInternetPeer(peerId)) {
        m_internetRelay->sendImage(peerId, filePath);
    } else {
        m_chatService->sendFile(peerId, filePath);
    }
}

void MainWindow::sendWink(const QString &peerId)
{
    const QString gameId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    chatWindowFor(peerId)->appendSystemMessage(tr("Wink sent."));
    m_chatService->sendGameAction(peerId, QStringLiteral("wink"), QStringLiteral("send"), gameId, QStringLiteral("spark"));
}

void MainWindow::showWink(const QString &peerName, const QString &kind)
{
    Q_UNUSED(kind);
    auto *dialog = new QDialog(dialogParent());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    dialog->setStyleSheet(QStringLiteral(
        "QDialog { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #ffffff, stop:0.55 #dff2ff, stop:1 #bde3ff);"
        " border: 2px solid #1d9bf0; border-radius: 16px; }"
        "QLabel { background: transparent; color: #0f172a; }"));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 18, 24, 18);
    auto *title = new QLabel(tr("Wink!"), dialog);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 900;"));
    auto *subtitle = new QLabel(tr("%1 sent you a wink").arg(peerName.isEmpty() ? tr("Someone") : peerName), dialog);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color:#334155;"));
    layout->addWidget(title);
    layout->addWidget(subtitle);
    dialog->resize(260, 120);
    dialog->show();
    dialog->raise();
    QTimer::singleShot(1800, dialog, &QDialog::accept);
}

void MainWindow::handleGameAction(const QString &peerId, const QString &peerName, const QString &game, const QString &action, const QString &gameId, const QString &value, const QString &board)
{
    if (m_blockedPeers.contains(peerId) || peerId.isEmpty()) {
        return;
    }

    auto *window = chatWindowFor(peerId);
    if (game == QStringLiteral("wink") && action == QStringLiteral("send")) {
        showWink(peerName, value);
        window->appendSystemMessage(tr("%1 sent a wink.").arg(peerName));
        return;
    }

    if (game == QStringLiteral("rps")) {
        if (action == QStringLiteral("challenge")) {
            const QString reply = promptRpsChoice(tr("%1 challenged you").arg(peerName));
            if (reply.isEmpty()) {
                window->appendSystemMessage(tr("Declined Rock-Paper-Scissors from %1.").arg(peerName));
                return;
            }
            const bool tie = reply == value;
            const bool win = (reply == tr("Rock") && value == tr("Scissors"))
                             || (reply == tr("Paper") && value == tr("Rock"))
                             || (reply == tr("Scissors") && value == tr("Paper"));
            window->appendSystemMessage(tie ? tr("Rock-Paper-Scissors: tie. You both chose %1.").arg(reply)
                                            : tr("Rock-Paper-Scissors: you %1. You chose %2, %3 chose %4.")
                                                  .arg(win ? tr("won") : tr("lost"), reply, peerName, value));
            m_chatService->sendGameAction(peerId, QStringLiteral("rps"), QStringLiteral("response"), gameId, reply);
            return;
        }
        if (action == QStringLiteral("response")) {
            const QString mine = m_rpsChoices.take(gameId);
            const bool tie = mine == value;
            const bool win = (mine == tr("Rock") && value == tr("Scissors"))
                             || (mine == tr("Paper") && value == tr("Rock"))
                             || (mine == tr("Scissors") && value == tr("Paper"));
            window->appendSystemMessage(tie ? tr("Rock-Paper-Scissors: tie. You both chose %1.").arg(mine)
                                            : tr("Rock-Paper-Scissors: you %1. You chose %2, %3 chose %4.")
                                                  .arg(win ? tr("won") : tr("lost"), mine, peerName, value));
            return;
        }
    }

    if (game == QStringLiteral("tictactoe")) {
        if (action == QStringLiteral("invite")) {
            if (QMessageBox::question(dialogParent(),
                                      tr("Tic-Tac-Toe"),
                                      tr("%1 invited you to play Tic-Tac-Toe. Accept?").arg(peerName),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::Yes)
                != QMessageBox::Yes) {
                m_chatService->sendGameAction(peerId, QStringLiteral("tictactoe"), QStringLiteral("decline"), gameId);
                return;
            }
            m_tttMarks.insert(gameId, QStringLiteral("O"));
            window->appendSystemMessage(tr("Tic-Tac-Toe accepted. You are O."));
            return;
        }
        if (action == QStringLiteral("decline")) {
            window->appendSystemMessage(tr("%1 declined Tic-Tac-Toe.").arg(peerName));
            return;
        }
        if (action == QStringLiteral("move") && board.size() == 9) {
            window->appendSystemMessage(tr("Tic-Tac-Toe: %1").arg(tttBoardHtml(board)));
            const QString winner = tttWinner(board);
            if (!winner.isEmpty()) {
                window->appendSystemMessage(winner == QStringLiteral("draw")
                                                ? tr("Tic-Tac-Toe ended in a draw.")
                                                : tr("Tic-Tac-Toe winner: %1.").arg(winner));
                m_tttMarks.remove(gameId);
                return;
            }

            const QString myMark = m_tttMarks.value(gameId, value == QStringLiteral("X") ? QStringLiteral("O") : QStringLiteral("X"));
            QString nextBoard = board;
            if (promptTicTacToeMove(&nextBoard, myMark, tr("Tic-Tac-Toe"))) {
                window->appendSystemMessage(tr("Tic-Tac-Toe: %1").arg(tttBoardHtml(nextBoard)));
                m_chatService->sendGameAction(peerId, QStringLiteral("tictactoe"), QStringLiteral("move"), gameId, myMark, nextBoard);
            }
        }
    }
}

void MainWindow::showPeerContextMenu(const QPoint &position)
{
    QListWidgetItem *item = m_peerList->itemAt(position);
    if (!item) {
        return;
    }
    if (item->data(ContactItemTypeRole).toString() == QStringLiteral("group")) {
        const QString groupKey = item->data(ContactItemIdRole).toString();
        QMenu menu(this);
        const bool collapsed = m_groupCollapsed.value(groupKey, false);
        menu.addAction(collapsed ? tr("Expand Group") : tr("Collapse Group"), this, [this, groupKey] {
            m_groupCollapsed.insert(groupKey, !m_groupCollapsed.value(groupKey, false));
            rebuildContactList();
            saveSettings();
        });
        if (groupKey.startsWith(QStringLiteral("custom:"))) {
            const QString oldName = contactCustomGroupName(groupKey);
            menu.addSeparator();
            auto *renameAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/display_name.png")), tr("Rename Group"), this, [this, oldName, groupKey] {
                bool ok = false;
                const QString newName = sanitizedGroupName(QInputDialog::getText(this,
                                                                                  tr("Rename Group"),
                                                                                  tr("Group name:"),
                                                                                  QLineEdit::Normal,
                                                                                  oldName,
                                                                                  &ok));
                if (!ok || newName.isEmpty() || newName == oldName) {
                    return;
                }
                for (const QString &existing : std::as_const(m_customContactGroups)) {
                    if (existing.compare(newName, Qt::CaseInsensitive) == 0) {
                        QMessageBox::information(dialogParent(), tr("Group Exists"), tr("A group with that name already exists."));
                        return;
                    }
                }
                const int index = m_customContactGroups.indexOf(oldName);
                if (index >= 0) {
                    m_customContactGroups[index] = newName;
                }
                for (auto it = m_peerGroups.begin(); it != m_peerGroups.end(); ++it) {
                    if (it.value() == oldName) {
                        it.value() = newName;
                    }
                }
                const bool wasCollapsed = m_groupCollapsed.take(groupKey);
                if (wasCollapsed) {
                    m_groupCollapsed.insert(QStringLiteral("custom:%1").arg(newName), true);
                }
                rebuildContactList();
                saveSettings();
            });
            renameAction->setIconVisibleInMenu(true);
            auto *deleteAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/clear_history.png")), tr("Delete Group"), this, [this, oldName, groupKey] {
                if (QMessageBox::question(dialogParent(),
                                          tr("Delete Group"),
                                          tr("Delete \"%1\" and move its contacts back to status groups?").arg(oldName))
                    != QMessageBox::Yes) {
                    return;
                }
                m_customContactGroups.removeAll(oldName);
                for (auto it = m_peerGroups.begin(); it != m_peerGroups.end(); ) {
                    if (it.value() == oldName) {
                        it = m_peerGroups.erase(it);
                    } else {
                        ++it;
                    }
                }
                m_groupCollapsed.remove(groupKey);
                rebuildContactList();
                saveSettings();
            });
            deleteAction->setIconVisibleInMenu(true);
        }
        menu.exec(m_peerList->viewport()->mapToGlobal(position));
        return;
    }
    m_peerList->setCurrentItem(item);
    const QString peerId = selectedPeerId();

    QMenu menu(this);
    menu.addAction(QIcon(QStringLiteral(":/icons/assets/open_chat.png")), tr("Open Chat"), this, [this] {
        openChat(selectedPeerId());
    });
    auto *contactInfoAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/contact_info.png")), tr("View Contact Info"), this, [this, peerId] {
        showContactInfo(peerId);
    });
    contactInfoAction->setIconVisibleInMenu(true);
    if (isInternetPeer(peerId)) {
        menu.addAction(QIcon(QStringLiteral(":/icons/assets/send_file.png")), tr("Send Image"), this, [this, peerId] {
            const QString filePath = QFileDialog::getOpenFileName(this,
                                                                  tr("Send Image"),
                                                                  QString(),
                                                                  tr("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
            if (!filePath.isEmpty()) {
                m_internetRelay->sendImage(peerId, filePath);
            }
        });
        menu.addAction(QIcon(QStringLiteral(":/icons/assets/whistle.png")), tr("Whistle"), this, &MainWindow::buzzSelectedPeer);
        menu.addSeparator();
        menu.addAction(QIcon(QStringLiteral(":/icons/assets/clear_history.png")), tr("Clear Chat History"), this, &MainWindow::clearSelectedHistory);
        menu.addSeparator();
        if (m_blockedPeers.contains(peerId)) {
            menu.addAction(QIcon(QStringLiteral(":/icons/assets/block_user.png")), tr("Unblock User"), this, &MainWindow::unblockSelectedPeer);
        } else {
            menu.addAction(QIcon(QStringLiteral(":/icons/assets/block_user.png")), tr("Block User"), this, &MainWindow::blockSelectedPeer);
        }
        menu.exec(m_peerList->viewport()->mapToGlobal(position));
        return;
    }
    auto *saveContactAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/add.png")), tr("Save Contact"), this, [this, peerId] {
        const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
        if (peer.address.isNull() || peer.port == 0) {
            statusBar()->showMessage(tr("Cannot save contact: no IP address is available yet."), 5000);
            return;
        }
        const QString endpoint = QStringLiteral("%1:%2").arg(peer.address.toString()).arg(peer.port);
        if (m_settings.manualPeerAddresses.contains(endpoint)) {
            statusBar()->showMessage(tr("%1 is already saved in Manual IPs.").arg(peer.name.isEmpty() ? tr("Contact") : peer.name), 5000);
            return;
        }
        m_settings.manualPeerAddresses.append(endpoint);
        m_settings.manualPeerAddresses.removeDuplicates();
        saveSettings();
        statusBar()->showMessage(tr("Saved %1 to Manual IPs: %2").arg(peer.name.isEmpty() ? tr("Contact") : peer.name, endpoint), 5000);
    });
    saveContactAction->setIconVisibleInMenu(true);
    menu.addAction(QIcon(QStringLiteral(":/icons/assets/send_file.png")), tr("Send File"), this, [this] {
        const QString peerId = selectedPeerId();
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Send File"));
        if (!peerId.isEmpty() && !filePath.isEmpty()) {
            m_chatService->sendFile(peerId, filePath);
        }
    });
    menu.addAction(QIcon(QStringLiteral(":/icons/assets/whistle.png")), tr("Whistle"), this, &MainWindow::buzzSelectedPeer);
    menu.addSeparator();
    if (m_favoritePeers.contains(peerId)) {
        auto *favoriteAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/unfavorite.png")), tr("Remove from Favorites"), this, [this, peerId] {
            m_favoritePeers.remove(peerId);
            rebuildContactList();
            saveSettings();
        });
        favoriteAction->setIconVisibleInMenu(true);
    } else {
        auto *favoriteAction = menu.addAction(QIcon(QStringLiteral(":/icons/assets/favorite.png")), tr("Add to Favorites"), this, [this, peerId] {
            if (!peerId.isEmpty()) {
                m_favoritePeers.insert(peerId);
                rebuildContactList();
                saveSettings();
            }
        });
        favoriteAction->setIconVisibleInMenu(true);
    }

    auto *moveMenu = menu.addMenu(tr("Move to Group"));
    moveMenu->menuAction()->setIconVisibleInMenu(true);
    for (const QString &groupName : std::as_const(m_customContactGroups)) {
        auto *groupAction = moveMenu->addAction(groupName, this, [this, peerId, groupName] {
            if (!peerId.isEmpty()) {
                m_peerGroups.insert(peerId, groupName);
                rebuildContactList();
                saveSettings();
            }
        });
        groupAction->setCheckable(true);
        groupAction->setChecked(m_peerGroups.value(peerId) == groupName);
    }
    if (!m_customContactGroups.isEmpty()) {
        moveMenu->addSeparator();
    }
    auto *newGroupAction = moveMenu->addAction(QIcon(QStringLiteral(":/icons/assets/add.png")), tr("New Group..."), this, [this, peerId] {
        bool ok = false;
        const QString groupName = sanitizedGroupName(QInputDialog::getText(this,
                                                                           tr("New Contact Group"),
                                                                           tr("Group name:"),
                                                                           QLineEdit::Normal,
                                                                           QString(),
                                                                           &ok));
        if (!ok || groupName.isEmpty() || peerId.isEmpty()) {
            return;
        }
        const QStringList reservedNames = {tr("Favorites"), tr("Online"), tr("Away / Idle"), tr("Busy"), tr("Offline")};
        for (const QString &reserved : reservedNames) {
            if (reserved.compare(groupName, Qt::CaseInsensitive) == 0) {
                QMessageBox::information(dialogParent(), tr("Reserved Name"), tr("That name is used by a built-in group."));
                return;
            }
        }
        for (const QString &existing : std::as_const(m_customContactGroups)) {
            if (existing.compare(groupName, Qt::CaseInsensitive) == 0) {
                m_peerGroups.insert(peerId, existing);
                rebuildContactList();
                saveSettings();
                return;
            }
        }
        m_customContactGroups.append(groupName);
        m_peerGroups.insert(peerId, groupName);
        rebuildContactList();
        saveSettings();
    });
    newGroupAction->setIconVisibleInMenu(true);
    if (m_peerGroups.contains(peerId)) {
        auto *removeGroupAction = moveMenu->addAction(QIcon(QStringLiteral(":/icons/assets/remove_from_group.png")), tr("Remove from Group"), this, [this, peerId] {
            m_peerGroups.remove(peerId);
            rebuildContactList();
            saveSettings();
        });
        removeGroupAction->setIconVisibleInMenu(true);
    }

    menu.addSeparator();
    menu.addAction(QIcon(QStringLiteral(":/icons/assets/clear_history.png")), tr("Clear Chat History"), this, &MainWindow::clearSelectedHistory);
    menu.addSeparator();
    if (m_blockedPeers.contains(peerId)) {
        menu.addAction(QIcon(QStringLiteral(":/icons/assets/block_user.png")), tr("Unblock User"), this, &MainWindow::unblockSelectedPeer);
    } else {
        menu.addAction(QIcon(QStringLiteral(":/icons/assets/block_user.png")), tr("Block User"), this, &MainWindow::blockSelectedPeer);
    }
    menu.exec(m_peerList->viewport()->mapToGlobal(position));
}

void MainWindow::showContactInfo(const QString &peerId)
{
    if (peerId.isEmpty()) {
        return;
    }

    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    if (peer.id.isEmpty()) {
        return;
    }

    const QString name = peer.name.isEmpty() ? tr("Unknown contact") : peer.name;
    QStringList rows;
    rows << tr("<b>Status:</b> %1").arg((peer.status.isEmpty() ? tr("Available") : peer.status).toHtmlEscaped());
    if (!peer.personalMessage.trimmed().isEmpty()) {
        rows << tr("<b>Personal message:</b> %1").arg(peer.personalMessage.toHtmlEscaped());
    }
    if (m_favoritePeers.contains(peerId)) {
        rows << tr("<b>Favorite:</b> Yes");
    }
    const QString group = m_peerGroups.value(peerId);
    if (!group.isEmpty()) {
        rows << tr("<b>Group:</b> %1").arg(group.toHtmlEscaped());
    }
    if (!peer.address.isNull()) {
        rows << tr("<b>Address:</b> %1:%2").arg(peer.address.toString().toHtmlEscaped()).arg(peer.port);
    }
    if (peer.lastSeen.isValid()) {
        rows << tr("<b>Last seen:</b> %1").arg(peer.lastSeen.toLocalTime().toString(QStringLiteral("MMM d, h:mm AP")).toHtmlEscaped());
    }
    rows << tr("<b>Protocol:</b> v%1").arg(peer.protocolVersion);

    QMessageBox box(dialogParent());
    box.setWindowTitle(tr("Contact Info"));
    QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    const QColor localFrameColor(appThemeForKey(themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString()).accent);
    const QColor peerFrameColor(peer.themeColor);
    box.setIconPixmap(avatarPixmapWithStatus(peer.avatarData, name, peer.status, 64, peerFrameColor.isValid() ? peerFrameColor : localFrameColor));
    box.setText(QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped()));
    box.setInformativeText(rows.join(QStringLiteral("<br>")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::updateLocalProfile()
{
    if (isInternetMode()) {
        const InternetRelayPeer self = m_internetRelay->self();
        const QString displayName = !m_settings.internetDisplayName.isEmpty()
                                        ? m_settings.internetDisplayName
                                        : (self.displayName.isEmpty() ? m_settings.internetBlinqId : self.displayName);
        const QByteArray avatarData = QByteArray::fromBase64(self.avatar.toLatin1());
        m_localName->setText(displayName.isEmpty() ? tr("Blinq Messenger") : displayName);
        if (auto *pmEdit = findChild<QLineEdit*>(QStringLiteral("LocalPersonalMessageEdit"))) {
            pmEdit->setText(m_manualPersonalMessage);
        }
        const QIcon icon = avatarIcon(avatarData, displayName);
        m_localAvatar->setFixedSize(64, 64);
        m_localAvatar->setPixmap(avatarPixmap(avatarData, displayName, 58));
        if (m_trayIcon) {
            m_trayIcon->setIcon(icon);
            updateTrayTooltip();
        }
        return;
    }

    m_localName->setText(m_chatService->localName());
    const int statusIndex = m_statusCombo->findText(m_chatService->localStatus());
    if (statusIndex >= 0) {
        m_statusCombo->setCurrentIndex(statusIndex);
    } else if (!m_chatService->localStatus().isEmpty()) {
        m_statusCombo->addItem(m_chatService->localStatus());
        m_statusCombo->setCurrentText(m_chatService->localStatus());
    }
    if (auto *pmEdit = findChild<QLineEdit*>(QStringLiteral("LocalPersonalMessageEdit"))) {
        const QString personalMessage = m_chatService->localPersonalMessage();
        if (personalMessage.startsWith(tr("Now playing: "))) {
            if (m_manualPersonalMessage.isEmpty()) {
                m_manualPersonalMessage = tr("Hi, let's chat!");
            }
            pmEdit->setText(m_manualPersonalMessage);
        } else {
            m_manualPersonalMessage = personalMessage.trimmed().isEmpty() ? tr("Hi, let's chat!") : personalMessage;
            pmEdit->setText(m_manualPersonalMessage);
        }
    }
    const QIcon icon = avatarIcon(m_chatService->localAvatarData(), m_chatService->localName());
    m_localAvatar->setFixedSize(64, 64);
    m_localAvatar->setPixmap(avatarPixmap(m_chatService->localAvatarData(), m_chatService->localName(), 58));
    if (m_trayIcon) {
        m_trayIcon->setIcon(icon);
        updateTrayTooltip();
    }
}

QIcon MainWindow::avatarIcon(const QByteArray &avatarData, const QString &fallbackText) const
{
    return QIcon(avatarPixmap(avatarData, fallbackText, 56));
}

QPixmap MainWindow::avatarPixmap(const QByteArray &avatarData, const QString &fallbackText, int size, const QColor &frameColor) const
{
    QPixmap pixmap;
    if (!avatarData.isEmpty()) {
        pixmap.loadFromData(avatarData);
    }
    if (pixmap.isNull()) {
        pixmap = QPixmap(size, size);
        pixmap.fill(QColor(QStringLiteral("#e0f2fe")));
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(QStringLiteral("#075985")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), qMax(12, size / 3), QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, fallbackText.left(1).toUpper());
    }

    if (pixmap.width() != pixmap.height()) {
        const int squareSize = qMin(pixmap.width(), pixmap.height());
        pixmap = pixmap.copy((pixmap.width() - squareSize) / 2, (pixmap.height() - squareSize) / 2, squareSize, squareSize);
    }

    const int frame = qMax(3, size / 14);
    const int innerSize = qMax(8, size - frame * 2);
    pixmap = pixmap.scaled(innerSize, innerSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPixmap framed(size, size);
    framed.fill(Qt::transparent);
    QPainter painter(&framed);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = frameColor.isValid() ? frameColor : QColor(QStringLiteral("#7dd3fc"));
    QLinearGradient frameGradient(0, 0, size, size);
    frameGradient.setColorAt(0.0, QColor(QStringLiteral("#ffffff")));
    frameGradient.setColorAt(0.48, accent.lighter(175));
    frameGradient.setColorAt(1.0, accent);
    painter.setPen(QPen(accent.darker(115), 1));
    painter.setBrush(frameGradient);
    painter.drawRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), 9, 9);

    QPainterPath path;
    path.addRoundedRect(QRectF(frame, frame, innerSize, innerSize), 7, 7);
    painter.setClipPath(path);
    painter.drawPixmap(frame, frame, pixmap);
    return framed;
}

QPixmap MainWindow::avatarPixmapWithStatus(const QByteArray &avatarData, const QString &fallbackText, const QString &status, int size, const QColor &frameColor) const
{
    QPixmap pixmap = avatarPixmap(avatarData, fallbackText, size, frameColor);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const int dotSize = qMax(12, size / 4);
    const QRectF outer(size - dotSize - 1, size - dotSize - 1, dotSize, dotSize);
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(QColor(statusColor(status)));
    painter.drawEllipse(outer);
    painter.setPen(QPen(QColor(QStringLiteral("#64748b")), 1));
    painter.drawEllipse(outer.adjusted(1, 1, -1, -1));
    return pixmap;
}

QWidget *MainWindow::createGroupHeader(const QString &groupKey, int count)
{
    auto *header = new QWidget(m_peerList);
    header->setCursor(Qt::PointingHandCursor);
    header->setStyleSheet(QStringLiteral(
        "QWidget { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #edf6ff); border: 1px solid #c7dff2; border-radius: 6px; }"
        "QLabel { background: transparent; border: none; }"));

    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(7);

    const bool collapsed = m_groupCollapsed.value(groupKey, false);
    auto *arrow = new QLabel(header);
    arrow->setFixedSize(12, 12);
    arrow->setPixmap(disclosurePixmap(collapsed, QColor(QStringLiteral("#2563eb"))));
    layout->addWidget(arrow);

    auto *title = new QLabel(contactGroupTitle(groupKey, count), header);
    title->setStyleSheet(QStringLiteral("font-weight: 750; color: #0f2748; font-size: 12px;"));
    layout->addWidget(title, 1);

    return header;
}

QWidget *MainWindow::createPeerRow(const ChatPeer &peer)
{
    auto *row = new QWidget(m_peerList);
    row->setStyleSheet(QStringLiteral("QWidget { background: transparent; } QLabel { background: transparent; }"));

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(10);

    auto *avatar = new QLabel(row);
    avatar->setFixedSize(48, 48);
    QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    const QColor localFrameColor(appThemeForKey(themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString()).accent);
    const QColor peerFrameColor(peer.themeColor);
    const QColor avatarFrameColor = peerFrameColor.isValid() ? peerFrameColor : localFrameColor;
    avatar->setPixmap(avatarPixmapWithStatus(peer.avatarData, peer.name, peer.status, 48, avatarFrameColor));
    avatar->setStyleSheet(QStringLiteral("background: transparent;"));
    layout->addWidget(avatar);

    auto *text = new QWidget(row);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto *name = new QLabel(peer.name, text);
    name->setTextFormat(Qt::PlainText);
    name->setText(fontMetrics().elidedText(peer.name, Qt::ElideRight, 220));
    name->setToolTip(peer.name);
    name->setStyleSheet(QStringLiteral("QLabel { font-weight: 700; color: #111827; font-size: 14px; background: transparent; }"));
    name->setAttribute(Qt::WA_StyledBackground, true);
    QPalette namePalette = name->palette();
    namePalette.setColor(QPalette::WindowText, QColor(QStringLiteral("#111827")));
    name->setPalette(namePalette);
    const QString statusText = peer.status.isEmpty() ? tr("Available") : peer.status;
    QString messageText;
    QString messageColor = QStringLiteral("#64748b");
    QString messageWeight = QStringLiteral("400");
    const QString nowPlayingPrefix = tr("Now playing: ");
    if (statusText == tr("Offline")) {
        messageText.clear();
    } else if (m_typingPeers.contains(peer.id)) {
        messageText = tr("typing...");
        messageColor = localFrameColor.name();
        messageWeight = QStringLiteral("700");
    } else if (!peer.personalMessage.isEmpty()) {
        if (peer.personalMessage.startsWith(nowPlayingPrefix)) {
            messageText = tr("Playing: %1").arg(peer.personalMessage.mid(nowPlayingPrefix.size()).trimmed());
            messageColor = QStringLiteral("#475569");
            messageWeight = QStringLiteral("650");
        } else {
            messageText = peer.personalMessage;
            messageColor = QStringLiteral("#64748b");
            messageWeight = QStringLiteral("400");
        }
    }
    if (statusText != tr("Offline") && peer.protocolVersion < 3) {
        messageText = messageText.isEmpty() ? tr("(old version)") : tr("%1 (old version)").arg(messageText);
    }

    auto *detailRow = new QWidget(text);
    auto *detailLayout = new QHBoxLayout(detailRow);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(4);

    auto *status = new QLabel(statusText, detailRow);
    status->setTextFormat(Qt::PlainText);
    status->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; font-weight: 750; background: transparent; }").arg(statusColor(peer.status)));
    status->setWordWrap(false);
    detailLayout->addWidget(status, 0);

    QLabel *message = nullptr;
    if (!messageText.isEmpty()) {
        auto *separator = new QLabel(QString::fromUtf8("\xE2\x80\xA2"), detailRow);
        separator->setStyleSheet(QStringLiteral("QLabel { color: #94a3b8; font-size: 11px; background: transparent; }"));
        detailLayout->addWidget(separator, 0);

        message = new QLabel(detailRow);
        message->setTextFormat(Qt::PlainText);
        message->setText(fontMetrics().elidedText(messageText, Qt::ElideRight, 170));
        message->setToolTip(messageText);
        message->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; font-weight: %2; background: transparent; }").arg(messageColor, messageWeight));
        message->setWordWrap(false);
        detailLayout->addWidget(message, 1);
    }
    auto *lastSeen = new QLabel(lastSeenText(peer), text);
    lastSeen->setText(fontMetrics().elidedText(lastSeenText(peer), Qt::ElideRight, 230));
    lastSeen->setToolTip(lastSeenText(peer));
    lastSeen->setStyleSheet(QStringLiteral("QLabel { color: #64748b; font-size: 10px; background: transparent; }"));
    lastSeen->setTextFormat(Qt::PlainText);
    lastSeen->setWordWrap(false);
    
    textLayout->addWidget(name);
    textLayout->addWidget(detailRow);
    textLayout->addWidget(lastSeen);
    layout->addWidget(text, 1);

    auto unreadCounts = property("unreadCounts").toHash();
    const int unread = unreadCounts.value(peer.id).toInt();
    if (unread > 0) {
        const QString unreadText = compactCount(unread);
        auto *badge = new QLabel(unreadText, row);
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedHeight(22);
        badge->setMinimumWidth(qMax(22, badge->fontMetrics().horizontalAdvance(unreadText) + 14));
        badge->setStyleSheet(QStringLiteral(
            "QLabel {"
            " background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ff6b6b, stop:1 #ef4444);"
            " color: #ffffff;"
            " border: 2px solid #ffffff;"
            " border-radius: 11px;"
            " font-weight: 800;"
            " font-size: 11px;"
            " padding: 0px 7px;"
            "}"));
        layout->addWidget(badge);
    }

    return row;
}

QString MainWindow::selectedPeerId() const
{
    const QListWidgetItem *item = m_peerList->currentItem();
    if (!item || item->data(ContactItemTypeRole).toString() != QStringLiteral("peer")) {
        return QString();
    }
    return item->data(ContactItemIdRole).toString();
}

QString MainWindow::peerIdForManualConnection(const QString &address, const QString &peerName) const
{
    QString hostText = address.trimmed();
    quint16 port = 0;
    const int portSeparator = hostText.lastIndexOf(QLatin1Char(':'));
    if (portSeparator > 0 && hostText.indexOf(QLatin1Char(':')) == portSeparator) {
        bool ok = false;
        const int parsedPort = hostText.mid(portSeparator + 1).toInt(&ok);
        if (ok && parsedPort > 0 && parsedPort <= 65535) {
            port = static_cast<quint16>(parsedPort);
            hostText = hostText.left(portSeparator).trimmed();
        }
    }

    const QHostAddress host(hostText);
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const ChatPeer &peer = it.value();
        const bool addressMatches = !host.isNull() && peer.address == host && (port == 0 || peer.port == port);
        const bool nameMatches = !peerName.isEmpty() && peer.name == peerName;
        if (addressMatches || nameMatches) {
            return it.key();
        }
    }
    return QString();
}

ChatWindow *MainWindow::chatWindowFor(const QString &peerId)
{
    auto *window = m_chatWindows.value(peerId, nullptr);
    if (window) {
        return window;
    }

    window = new ChatWindow(peerId);
    window->setHistoryEnabled(m_settings.saveHistory);
    window->setImageAttachmentsOnly(isInternetPeer(peerId));
    window->setQueuedMessageCount(m_offlineMessages.value(peerId).size());
    QSettings themeSettings(QStringLiteral("LANChat"), QStringLiteral("LANChat"));
    window->setLocalAccentColor(QColor(appThemeForKey(themeSettings.value(QStringLiteral("app/themeName"), QStringLiteral("msnBlue")).toString()).accent));
    m_chatWindows.insert(peerId, window);
    connect(window, &QObject::destroyed, this, [this, peerId] {
        m_chatWindows.remove(peerId);
    });
    connect(window, &ChatWindow::chatClosed, this, &MainWindow::markChatIdle);
    connect(window, &ChatWindow::cancelTransferRequested, m_chatService, &LanChatService::cancelTransfer);
    connect(window, &ChatWindow::becameActive, this, [this, peerId] {
        markChatActive();
        sendPendingReadReceipts(peerId);
        
        auto unreadCounts = property("unreadCounts").toHash();
        if (unreadCounts.value(peerId).toInt() > 0) {
            unreadCounts.remove(peerId);
            setProperty("unreadCounts", unreadCounts);
            if (m_peers.contains(peerId)) {
                if (isInternetPeer(peerId)) {
                    rebuildContactList();
                } else {
                    upsertPeer(m_peers.value(peerId));
                }
            }
        }
    });
    connect(window, &ChatWindow::typingStateChanged, this, [this](const QString &requestedPeerId, bool isTyping) {
        if (isTyping) {
            markChatActive();
        }
        if (m_settings.hideTypingIndicator) {
            return;
        }
        if (isInternetPeer(requestedPeerId)) {
            m_internetRelay->sendTypingState(requestedPeerId, isTyping);
        } else if (m_peers.contains(requestedPeerId)) {
            m_chatService->sendTypingState(requestedPeerId, isTyping);
        }
    });
    connect(window, &ChatWindow::sendMessageRequested, this, [this, window](const QString &requestedPeerId, const QString &message, bool isHtml) {
        markChatActive();
        const ChatPeer peer = m_knownPeers.value(requestedPeerId, m_peers.value(requestedPeerId));
        if (isInternetPeer(requestedPeerId)) {
            if (!m_internetRelay->isAuthenticated()) {
                window->appendSystemMessage(tr("Sign in to Internet Mode before sending messages."));
                showInternetSignInDialog();
                return;
            }
            if (!m_peers.contains(requestedPeerId)) {
                const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
                window->appendSystemMessage(tr("%1 is offline. Internet offline message sync is not enabled yet.").arg(name));
                return;
            }
            m_internetRelay->sendMessage(requestedPeerId, message, isHtml);
            return;
        }
        if (peer.status == tr("Do Not Disturb")) {
            const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
            window->appendSystemMessage(tr("%1 is in Do Not Disturb. Message was not sent or queued.").arg(name));
            return;
        }
        makeAvailableForOutgoingPrivateAction(window);
        if (!m_peers.contains(requestedPeerId)) {
            const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
            queueOfflineMessage(requestedPeerId, message, isHtml);
            window->appendSystemMessage(tr("%1 is offline. Message queued and will be sent when they come online.").arg(name));
            return;
        }
        m_chatService->sendMessage(requestedPeerId, message, isHtml);
    });
    connect(window, &ChatWindow::sendFileRequested, this, [this, window](const QString &requestedPeerId, const QString &filePath) {
        markChatActive();
        if (!allowRateLimitedAction(QStringLiteral("outgoing-file:%1").arg(requestedPeerId), 5000)) {
            window->appendSystemMessage(tr("Please wait a moment before sending another file."));
            return;
        }
        const ChatPeer peer = m_knownPeers.value(requestedPeerId, m_peers.value(requestedPeerId));
        if (isInternetPeer(requestedPeerId)) {
            if (!m_peers.contains(requestedPeerId)) {
                const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
                window->appendSystemMessage(tr("%1 is offline. Image was not sent.").arg(name));
                return;
            }
            m_internetRelay->sendImage(requestedPeerId, filePath);
            return;
        }
        if (peer.status == tr("Do Not Disturb")) {
            const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
            window->appendSystemMessage(tr("%1 is in Do Not Disturb. File was not sent.").arg(name));
            return;
        }
        if (!m_peers.contains(requestedPeerId)) {
            const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
            window->appendSystemMessage(tr("%1 is not online. File was not sent.").arg(name));
            return;
        }
        makeAvailableForOutgoingPrivateAction(window);
        m_chatService->sendFile(requestedPeerId, filePath);
    });
    connect(window, &ChatWindow::buzzRequested, this, [this, window](const QString &requestedPeerId) {
        if (isInternetPeer(requestedPeerId)) {
            if (!allowRateLimitedAction(QStringLiteral("outgoing-internet-buzz:%1").arg(requestedPeerId), 2500)) {
                window->appendSystemMessage(tr("Please wait a moment before whistling again."));
                return;
            }
            if (!m_peers.contains(requestedPeerId)) {
                window->appendSystemMessage(tr("That contact is offline. Whistle was not sent."));
                return;
            }
            m_internetRelay->sendBuzz(requestedPeerId);
            return;
        }
        if (!allowRateLimitedAction(QStringLiteral("outgoing-buzz:%1").arg(requestedPeerId), 2500)) {
            window->appendSystemMessage(tr("Please wait a moment before whistling again."));
            return;
        }
        const ChatPeer peer = m_knownPeers.value(requestedPeerId, m_peers.value(requestedPeerId));
        if (peer.status == tr("Do Not Disturb")) {
            const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
            window->appendSystemMessage(tr("%1 is in Do Not Disturb. Whistle was not sent.").arg(name));
            return;
        }
        if (!m_peers.contains(requestedPeerId)) {
            const QString name = peer.name.isEmpty() ? tr("This contact") : peer.name;
            window->appendSystemMessage(tr("%1 is not online. Whistle was not sent.").arg(name));
            return;
        }
        makeAvailableForOutgoingPrivateAction(window);
        m_chatService->sendBuzz(requestedPeerId);
    });
    connect(window, &ChatWindow::activityRequested, this, [this, window](const QString &requestedPeerId, const QString &activity) {
        if (isInternetPeer(requestedPeerId)) {
            if (activity == QStringLiteral("drawing")) {
                openDrawingPad(requestedPeerId);
            } else {
                window->appendSystemMessage(tr("That activity is LAN-only for now."));
            }
            return;
        }
        startActivity(requestedPeerId, activity);
    });
    connect(window, &ChatWindow::retryQueuedMessagesRequested, this, [this, window](const QString &requestedPeerId) {
        if (!m_peers.contains(requestedPeerId)) {
            window->appendSystemMessage(tr("That contact is still offline. Queued messages will retry automatically."));
            return;
        }
        flushPendingOfflineMessages(requestedPeerId);
    });
    connect(window, &ChatWindow::viewQueuedMessagesRequested, this, [this, window](const QString &requestedPeerId) {
        showQueuedMessages(requestedPeerId, window);
    });
    connect(window, &ChatWindow::clearQueuedMessagesRequested, this, [this, window](const QString &requestedPeerId) {
        const int queuedCount = m_offlineMessages.value(requestedPeerId).size();
        if (queuedCount <= 0) {
            window->setQueuedMessageCount(0);
            return;
        }
        const ChatPeer peer = m_knownPeers.value(requestedPeerId, m_peers.value(requestedPeerId));
        const QString name = peer.name.isEmpty() ? tr("this contact") : peer.name;
        if (QMessageBox::question(window,
                                  tr("Cancel Queued Messages"),
                                  tr("Cancel %n queued message(s) for %1?", nullptr, queuedCount).arg(name),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        const int removed = m_offlineMessages.take(requestedPeerId).size();
        window->setQueuedMessageCount(0);
        saveSettings();
        if (removed > 0) {
            window->appendSystemMessage(tr("Canceled %n queued offline message(s).", nullptr, removed));
        }
    });

    const ChatPeer peer = m_knownPeers.value(peerId, m_peers.value(peerId));
    window->setPeerDetails(peer.name.isEmpty() ? tr("Unknown contact") : peer.name,
                           peer.status.isEmpty() ? tr("Idle") : peer.status,
                           peer.avatarData,
                           peer.lastSeen,
                           QColor(peer.themeColor));
    return window;
}
