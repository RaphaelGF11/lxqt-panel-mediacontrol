#include <ilxqtpanelplugin.h>
#include <pluginsettings.h>

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QImage>
#include <QImageReader>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTranslator>
#include <QUrl>
#include <QWheelEvent>

namespace {
const auto playerPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const auto playerPath = QStringLiteral("/org/mpris/MediaPlayer2");
const auto playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const auto propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const auto busName = QStringLiteral("org.freedesktop.DBus");
const auto busPath = QStringLiteral("/org/freedesktop/DBus");

const auto playIconKey = QStringLiteral("playIcon");
const auto pauseIconKey = QStringLiteral("pauseIcon");
const auto themeIconsKey = QStringLiteral("preferThemeIcons");
const auto iconSizeKey = QStringLiteral("customIconSize");
const auto widgetSizeKey = QStringLiteral("customWidgetSize");
const int maxWidgetSize = 256;
const auto clickKey = QStringLiteral("clickAction");
const auto wheelUpKey = QStringLiteral("wheelUpAction");
const auto wheelDownKey = QStringLiteral("wheelDownAction");
const auto toggleAction = QStringLiteral("PlayPause");
const auto nextAction = QStringLiteral("Next");
const auto previousAction = QStringLiteral("Previous");
const auto stopAction = QStringLiteral("Stop");
const auto showCoverKey = QStringLiteral("showCover");
const auto showTitleKey = QStringLiteral("showTitle");
const auto textWidthKey = QStringLiteral("maxTextWidth");
const auto popupCoverKey = QStringLiteral("tooltipCoverSize");
const int defaultPopupCover = 128;
const int maxPopupCover = 512;
const int popupTextWidth = 320;
const int defaultTextWidth = 200;
const int maxTextWidthLimit = 800;
const qint64 maxCoverBytes = 16 * 1024 * 1024;

struct Player
{
    QString service;
    QString status;
    int priority = 3;
    QString title;
    QString artist;
    QString album;
    QString artUrl;
};

// MPRIS metadata arrives as a{sv} wrapped in a QDBusArgument; its xesam:artist is "as".
QVariantMap toMap(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}

QString joinedStrings(const QVariant &value)
{
    QStringList list;
    if (value.canConvert<QDBusArgument>())
        list = qdbus_cast<QStringList>(value.value<QDBusArgument>());
    else if (value.userType() == QMetaType::QStringList)
        list = value.toStringList();
    else
        list = QStringList{value.toString()};
    list.removeAll(QString());
    return list.join(QStringLiteral(", "));
}
}

class MediaControl final : public QToolButton, public ILXQtPanelPlugin
{
    Q_OBJECT

public:
    MediaControl(const ILXQtPanelPluginStartupInfo &info, Qt::LayoutDirection direction)
        : ILXQtPanelPlugin(info), bus(QDBusConnection::sessionBus()), dialogDirection(direction)
    {
        setAutoRaise(true);
        network.setTransferTimeout(10000);
        settingsChanged();
        refreshTimer.setSingleShot(true);
        refreshTimer.setInterval(50);
        connect(&refreshTimer, &QTimer::timeout, this, &MediaControl::refresh);
        connect(this, &QToolButton::clicked, this, [this] {
            send(clickAction);
        });

        bus.connect(busName, busPath, busName, QStringLiteral("NameOwnerChanged"),
                    this, SLOT(ownerChanged(QString,QString,QString)));
        bus.connect(QString(), playerPath, propertiesInterface, QStringLiteral("PropertiesChanged"),
                    this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        refreshTimer.start();
    }

    Flags flags() const override { return PreferRightAlignment | SingleInstance | HaveConfigDialog; }
    QString themeId() const override { return QStringLiteral("MediaControl"); }
    QWidget *widget() override { return this; }

    void realign() override
    {
        const bool horizontal = panel()->isHorizontal();
        const int length = customWidgetSize > 0 ? customWidgetSize : QWIDGETSIZE_MAX;
        const int icon = customIconSize > 0 ? customIconSize : panel()->iconSize();
        const int visibleIcon = qMin(icon, length);

        setMinimumSize(0, 0);
        setMaximumSize(horizontal ? length : QWIDGETSIZE_MAX,
                       horizontal ? QWIDGETSIZE_MAX : length);
        setMinimumSize(horizontal ? customWidgetSize : 0,
                       horizontal ? 0 : customWidgetSize);
        setIconSize(QSize(visibleIcon, visibleIcon));
        updateButton();
    }

    void settingsChanged() override
    {
        playIconPath = settings()->value(playIconKey).toString();
        pauseIconPath = settings()->value(pauseIconKey).toString();
        preferThemeIcons = settings()->value(themeIconsKey, false).toBool();
        customIconSize = qBound(0, settings()->value(iconSizeKey).toInt(), 64);
        customWidgetSize = qBound(0, settings()->value(widgetSizeKey).toInt(), maxWidgetSize);
        clickAction = settings()->value(clickKey, toggleAction).toString();
        wheelUpAction = settings()->value(wheelUpKey, nextAction).toString();
        wheelDownAction = settings()->value(wheelDownKey, previousAction).toString();
        showCover = settings()->value(showCoverKey, true).toBool();
        showTitle = settings()->value(showTitleKey, true).toBool();
        maxTextWidth = qBound(0, settings()->value(textWidthKey, defaultTextWidth).toInt(),
                              maxTextWidthLimit);
        popupCoverSize = qBound(0, settings()->value(popupCoverKey, defaultPopupCover).toInt(),
                                maxPopupCover);
        wheelDelta = 0;
        realign();
        updateButton();
    }

    QDialog *configureDialog() override
    {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setLayoutDirection(dialogDirection);
        dialog->setWindowTitle(tr("Media Controls"));
        auto *form = new QFormLayout(dialog);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        auto *playIcon = addIconField(form, tr("Play icon"), playIconPath);
        auto *pauseIcon = addIconField(form, tr("Pause icon"), pauseIconPath);
        auto *themeIcons = new QCheckBox(tr("Use theme icons by default"), dialog);
        themeIcons->setChecked(preferThemeIcons);
        form->addRow(themeIcons);

        auto *size = new QSpinBox(dialog);
        size->setRange(0, 64);
        size->setSpecialValueText(tr("Panel icon size"));
        size->setSuffix(QStringLiteral(" px"));
        size->setValue(customIconSize);
        form->addRow(tr("Icon size"), size);

        auto *widgetSize = new QSpinBox(dialog);
        widgetSize->setRange(0, maxWidgetSize);
        widgetSize->setSpecialValueText(tr("Automatic"));
        widgetSize->setSuffix(QStringLiteral(" px"));
        widgetSize->setValue(customWidgetSize);
        form->addRow(panel()->isHorizontal() ? tr("Widget width") : tr("Widget height"),
                     widgetSize);

        auto *cover = new QCheckBox(tr("Show album cover"), dialog);
        cover->setChecked(showCover);
        form->addRow(cover);
        auto *title = new QCheckBox(tr("Show artist and title"), dialog);
        title->setChecked(showTitle);
        form->addRow(title);
        auto *textWidth = new QSpinBox(dialog);
        textWidth->setRange(0, maxTextWidthLimit);
        textWidth->setSpecialValueText(tr("Unlimited"));
        textWidth->setSuffix(QStringLiteral(" px"));
        textWidth->setValue(maxTextWidth);
        textWidth->setEnabled(showTitle);
        form->addRow(tr("Maximum text width"), textWidth);
        connect(title, &QCheckBox::toggled, textWidth, &QWidget::setEnabled);
        auto *popupCover = new QSpinBox(dialog);
        popupCover->setRange(0, maxPopupCover);
        popupCover->setSpecialValueText(tr("Hidden"));
        popupCover->setSuffix(QStringLiteral(" px"));
        popupCover->setValue(popupCoverSize);
        form->addRow(tr("Tooltip cover size"), popupCover);

        auto *click = addActionField(form, tr("Left click"), clickAction);
        auto *wheelUp = addActionField(form, tr("Scroll up"), wheelUpAction);
        auto *wheelDown = addActionField(form, tr("Scroll down"), wheelDownAction);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults, dialog);
        buttons->button(QDialogButtonBox::Save)->setText(tr("Save"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
        auto *reset = buttons->button(QDialogButtonBox::RestoreDefaults);
        reset->setText(tr("Restore defaults"));
        form->addRow(buttons);
        connect(reset, &QPushButton::clicked, dialog, [=] {
            playIcon->clear();
            pauseIcon->clear();
            themeIcons->setChecked(false);
            size->setValue(0);
            widgetSize->setValue(0);
            cover->setChecked(true);
            title->setChecked(true);
            textWidth->setValue(defaultTextWidth);
            popupCover->setValue(defaultPopupCover);
            click->setCurrentIndex(click->findData(toggleAction));
            wheelUp->setCurrentIndex(wheelUp->findData(nextAction));
            wheelDown->setCurrentIndex(wheelDown->findData(previousAction));
        });
        connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
            settings()->setValue(playIconKey, playIcon->text().trimmed());
            settings()->setValue(pauseIconKey, pauseIcon->text().trimmed());
            settings()->setValue(themeIconsKey, themeIcons->isChecked());
            settings()->setValue(iconSizeKey, size->value());
            settings()->setValue(widgetSizeKey, widgetSize->value());
            settings()->setValue(showCoverKey, cover->isChecked());
            settings()->setValue(showTitleKey, title->isChecked());
            settings()->setValue(textWidthKey, textWidth->value());
            settings()->setValue(popupCoverKey, popupCover->value());
            settings()->setValue(clickKey, click->currentData());
            settings()->setValue(wheelUpKey, wheelUp->currentData());
            settings()->setValue(wheelDownKey, wheelDown->currentData());
            settings()->sync();
            settingsChanged();
            dialog->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        return dialog;
    }

protected:
    bool event(QEvent *event) override
    {
        // Qt sends ToolTip after the usual hover delay: show the cover popup instead of the plain
        // tooltip while a player is selected.
        if (event->type() == QEvent::ToolTip && !selected.service.isEmpty()) {
            showPopup(static_cast<QHelpEvent *>(event)->globalPos());
            return true;
        }
        return QToolButton::event(event);
    }

    void leaveEvent(QEvent *event) override
    {
        hidePopup();
        QToolButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        hidePopup();
        QToolButton::mousePressEvent(event);
    }

    void hideEvent(QHideEvent *event) override
    {
        hidePopup();
        QToolButton::hideEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->angleDelta().y() == 0 || selected.service.isEmpty()) {
            event->ignore();
            return;
        }
        const auto &action = event->angleDelta().y() > 0 ? wheelUpAction : wheelDownAction;
        if (action.isEmpty()) {
            wheelDelta = 0;
            event->ignore();
            return;
        }
        wheelDelta += event->angleDelta().y();
        while (wheelDelta >= 120) {
            send(wheelUpAction);
            wheelDelta -= 120;
        }
        while (wheelDelta <= -120) {
            send(wheelDownAction);
            wheelDelta += 120;
        }
        event->accept();
    }

private slots:
    void ownerChanged(const QString &name, const QString &, const QString &)
    {
        if (name.startsWith(playerPrefix))
            refreshTimer.start();
    }

    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated)
    {
        if (interface != playerInterface)
            return;
        for (const auto &key : {QStringLiteral("PlaybackStatus"), QStringLiteral("CanControl"),
                                QStringLiteral("Metadata")}) {
            if (changed.contains(key) || invalidated.contains(key)) {
                refreshTimer.start();
                return;
            }
        }
    }

private:
    QLineEdit *addIconField(QFormLayout *form, const QString &label, const QString &path)
    {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *field = new QLineEdit(path, row);
        field->setPlaceholderText(tr("Default icon"));
        auto *browse = new QPushButton(tr("Browse…"), row);
        layout->addWidget(field);
        layout->addWidget(browse);
        form->addRow(label, row);
        connect(browse, &QPushButton::clicked, row, [field, row] {
            const auto path = QFileDialog::getOpenFileName(
                row, tr("Choose an icon"), field->text(),
                QStringLiteral("%1 (*.png *.svg *.xpm *.jpg *.webp);;%2 (*)")
                    .arg(tr("Images"), tr("All files")));
            if (!path.isEmpty())
                field->setText(path);
        });
        return field;
    }

    QComboBox *addActionField(QFormLayout *form, const QString &label, const QString &action)
    {
        auto *field = new QComboBox;
        field->addItem(tr("None"), QString());
        field->addItem(tr("Play / pause"), toggleAction);
        field->addItem(tr("Next track"), nextAction);
        field->addItem(tr("Previous track"), previousAction);
        field->addItem(tr("Stop"), stopAction);
        field->setCurrentIndex(qMax(0, field->findData(action)));
        form->addRow(label, field);
        return field;
    }

    void refresh()
    {
        if (refreshing) {
            refreshPending = true;
            return;
        }
        refreshing = true;
        auto request = QDBusMessage::createMethodCall(busName, busPath, busName,
                                                       QStringLiteral("ListNames"));
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(request, 1000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
            QDBusPendingReply<QStringList> reply = *watcher;
            watcher->deleteLater();
            best = {};
            if (!reply.isError()) {
                for (const auto &name : reply.value()) {
                    if (name.startsWith(playerPrefix))
                        refreshPlayer(name);
                }
            }
            if (remaining == 0)
                finishRefresh();
        });
    }

    void refreshPlayer(const QString &name)
    {
        auto request = QDBusMessage::createMethodCall(name, playerPath, propertiesInterface,
                                                       QStringLiteral("GetAll"));
        request.setAutoStartService(false);
        request << playerInterface;
        ++remaining;
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(request, 750), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, name] {
            QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            if (!reply.isError()) {
                const auto properties = reply.value();
                if (properties.value(QStringLiteral("CanControl")).toBool()) {
                    const auto status = properties.value(QStringLiteral("PlaybackStatus")).toString();
                    const QStringList order{QStringLiteral("Playing"), QStringLiteral("Paused"),
                                            QStringLiteral("Stopped")};
                    const int priority = order.indexOf(status);
                    if (priority >= 0 && (priority < best.priority ||
                        (priority == best.priority && name < best.service))) {
                        const auto metadata = toMap(properties.value(QStringLiteral("Metadata")));
                        best = {name,
                                status,
                                priority,
                                metadata.value(QStringLiteral("xesam:title")).toString(),
                                joinedStrings(metadata.value(QStringLiteral("xesam:artist"))),
                                metadata.value(QStringLiteral("xesam:album")).toString(),
                                metadata.value(QStringLiteral("mpris:artUrl")).toString()};
                    }
                }
            }
            if (--remaining == 0)
                finishRefresh();
        });
    }

    void finishRefresh()
    {
        if (selected.service != best.service)
            wheelDelta = 0;
        selected = best;
        loadCover(selected.artUrl);
        updateButton();
        refreshing = false;
        if (refreshPending) {
            refreshPending = false;
            refreshTimer.start();
        }
    }

    QIcon stateIcon(bool playing) const
    {
        const auto icon = playing ? QStringLiteral("media-playback-pause")
                                  : QStringLiteral("media-playback-start");
        const auto &customPath = playing ? pauseIconPath : playIconPath;
        QIcon chosen;
        if (!customPath.isEmpty())
            chosen = QIcon(customPath);
        if (chosen.pixmap(24, 24).isNull() && !preferThemeIcons)
            chosen = QIcon(QStringLiteral("/usr/share/icons/gnome/24x24/actions/%1.png").arg(icon));
        if (chosen.pixmap(24, 24).isNull()) {
            const auto fallback = playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay;
            chosen = QIcon::fromTheme(icon, style()->standardIcon(fallback));
        }
        return chosen;
    }

    /// The cover, cropped square, with the playback state as a badge in the corner (drawn in
    /// white, so it shows whatever the icon theme); dimmed while not playing.
    QIcon coverIcon(bool playing) const
    {
        const int side = qMax(16, iconSize().width());
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap(QSize(side, side) * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, side, side), side / 8.0, side / 8.0);
        painter.setClipPath(clip);
        const QImage scaled = coverImage.scaled(QSize(side, side) * dpr,
                                                Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation);
        const QRect source((scaled.width() - side * dpr) / 2, (scaled.height() - side * dpr) / 2,
                           side * dpr, side * dpr);
        painter.drawImage(QRectF(0, 0, side, side), scaled, source);
        if (!playing)
            painter.fillRect(QRectF(0, 0, side, side), QColor(0, 0, 0, 90));
        painter.setClipping(false);

        const qreal b = side * 0.5;
        const QRectF badgeRect(side - b, side - b, b, b);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawEllipse(badgeRect);
        painter.setBrush(Qt::white);
        const QPointF c = badgeRect.center();
        const qreal g = b * 0.22;
        if (playing) {
            // pause: two bars
            painter.drawRect(QRectF(c.x() - g, c.y() - g, g * 0.75, 2 * g));
            painter.drawRect(QRectF(c.x() + g * 0.25, c.y() - g, g * 0.75, 2 * g));
        } else {
            // play: triangle
            const QPointF tri[3] = {{c.x() - g * 0.8, c.y() - g}, {c.x() - g * 0.8, c.y() + g},
                                    {c.x() + g * 1.1, c.y()}};
            painter.drawPolygon(tri, 3);
        }
        painter.end();
        return QIcon(pixmap);
    }

    void updateButton()
    {
        const bool playing = selected.status == QStringLiteral("Playing");
        const bool cover = showCover && !selected.service.isEmpty() && !coverImage.isNull();
        setIcon(cover ? coverIcon(playing) : stateIcon(playing));

        QString text;
        if (!selected.service.isEmpty() && !selected.title.isEmpty()) {
            text = selected.artist.isEmpty()
                       ? selected.title
                       : QStringLiteral("%1 – %2").arg(selected.artist, selected.title);
        }
        const bool horizontal = panel() == nullptr || panel()->isHorizontal();
        if (showTitle && horizontal && !text.isEmpty()) {
            int width = maxTextWidth > 0 ? maxTextWidth : QWIDGETSIZE_MAX;
            if (customWidgetSize > 0)
                width = qMin(width, qMax(0, customWidgetSize - iconSize().width() - 12));
            setText(fontMetrics().elidedText(text, Qt::ElideRight, width));
            setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        } else {
            setText(QString());
            setToolButtonStyle(Qt::ToolButtonIconOnly);
        }

        if (selected.service.isEmpty()) {
            setToolTip(tr("No media player available"));
            hidePopup();
            return;
        }
        setToolTip(QString());
        if (popupShown && QToolTip::isVisible() && underMouse())
            showPopup(popupPos);  // content changed while shown
    }

    QString statusText() const
    {
        if (selected.status == QStringLiteral("Playing")) return tr("Playing");
        if (selected.status == QStringLiteral("Paused")) return tr("Paused");
        if (selected.status == QStringLiteral("Stopped")) return tr("Stopped");
        return selected.status;
    }

    /// The mouse-over tooltip: a real QToolTip, so the widget style (e.g. Kvantum) and the
    /// panel theme draw it like every other tooltip. The cover is embedded as a data: URL.
    QString popupHtml() const
    {
        const QFontMetrics metrics = fontMetrics();
        const auto line = [&](const QString &text) {
            return metrics.elidedText(text, Qt::ElideRight, popupTextWidth).toHtmlEscaped();
        };
        QString text;
        if (!selected.title.isEmpty())
            text += QStringLiteral("<b style=\"font-size:large\">%1</b><br>").arg(line(selected.title));
        if (!selected.artist.isEmpty())
            text += line(selected.artist) + QStringLiteral("<br>");
        if (!selected.album.isEmpty())
            text += QStringLiteral("<i>%1</i><br>").arg(line(selected.album));
        const auto player = selected.service.mid(playerPrefix.size());
        text += QStringLiteral("<small>%1</small>")
                    .arg(QStringLiteral("%1 · %2").arg(player, statusText()).toHtmlEscaped());

        if (!showCover || popupCoverSize <= 0 || coverImage.isNull())
            return QStringLiteral("<qt>%1</qt>").arg(text);
        const qreal dpr = devicePixelRatioF();
        const QImage scaled = coverImage.scaled(QSize(popupCoverSize, popupCoverSize) * dpr,
                                                Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "PNG");
        const QSize logical = (QSizeF(scaled.size()) / dpr).toSize();
        return QStringLiteral(
                   "<qt><table cellspacing=\"0\" cellpadding=\"0\"><tr>"
                   "<td valign=\"top\"><img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%3\"></td>"
                   "<td width=\"10\"></td><td valign=\"top\">%4</td></tr></table></qt>")
            .arg(QString::fromLatin1(png.toBase64()))
            .arg(logical.width())
            .arg(logical.height())
            .arg(text);
    }

    void showPopup(const QPoint &globalPos)
    {
        popupPos = globalPos;
        popupShown = true;
        QToolTip::showText(globalPos, popupHtml(), this, rect());
    }

    void hidePopup()
    {
        if (popupShown)
            QToolTip::hideText();
        popupShown = false;
    }

    void loadCover(const QString &url)
    {
        if (url == coverUrl)
            return;
        coverUrl = url;
        coverImage = QImage();
        if (coverReply) {
            coverReply->abort();
            coverReply = nullptr;
        }
        if (url.isEmpty())
            return;
        const QUrl parsed(url);
        if (parsed.isLocalFile()) {
            QImageReader reader(parsed.toLocalFile());
            reader.setAutoTransform(true);
            coverImage = reader.read();
            return;
        }
        if (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))
            return;
        QNetworkRequest request(parsed);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply = network.get(request);
        coverReply = reply;
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
            if (received > maxCoverBytes)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            reply->deleteLater();
            if (coverReply == reply)
                coverReply = nullptr;
            if (url != coverUrl || reply->error() != QNetworkReply::NoError)
                return;
            QImage image;
            if (image.loadFromData(reply->readAll())) {
                coverImage = image;
                updateButton();
            }
        });
    }

    void send(const QString &command)
    {
        if (command.isEmpty() || selected.service.isEmpty())
            return;
        auto request = QDBusMessage::createMethodCall(selected.service, playerPath, playerInterface,
                                                       command);
        request.setAutoStartService(false);
        bus.asyncCall(request, 1000);
    }

    QDBusConnection bus;
    QNetworkAccessManager network;
    QNetworkReply *coverReply = nullptr;
    QString coverUrl;
    QImage coverImage;
    bool showCover = true;
    bool showTitle = true;
    int maxTextWidth = defaultTextWidth;
    int popupCoverSize = defaultPopupCover;
    QPoint popupPos;
    bool popupShown = false;
    const Qt::LayoutDirection dialogDirection;
    QTimer refreshTimer;
    Player selected;
    Player best;
    int remaining = 0;
    int wheelDelta = 0;
    QString playIconPath;
    QString pauseIconPath;
    QString clickAction;
    QString wheelUpAction;
    QString wheelDownAction;
    int customIconSize = 0;
    int customWidgetSize = 0;
    bool preferThemeIcons = false;
    bool refreshing = false;
    bool refreshPending = false;
};

class MediaControlLibrary final : public QObject, public ILXQtPanelPluginLibrary
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "lxqt.org/Panel/PluginInterface/3.0")
    Q_INTERFACES(ILXQtPanelPluginLibrary)

public:
    MediaControlLibrary()
    {
        if (translator.load(QLocale(), QStringLiteral("mediacontrol"), QStringLiteral("_"),
                            QStringLiteral(":/lxqt-mediacontrol/i18n"))) {
            QCoreApplication::installTranslator(&translator);
        }
    }

    ILXQtPanelPlugin *instance(const ILXQtPanelPluginStartupInfo &info) const override
    {
        const auto direction = QLocale(translator.language()).textDirection();
        return new MediaControl(info, direction);
    }

private:
    QTranslator translator;
};

#include "mediacontrol.moc"
