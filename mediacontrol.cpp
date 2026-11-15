#include <ilxqtpanelplugin.h>
#include <pluginsettings.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
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

struct Player
{
    QString service;
    QString status;
    int priority = 3;
};
}

class MediaControl final : public QToolButton, public ILXQtPanelPlugin
{
    Q_OBJECT

public:
    MediaControl(const ILXQtPanelPluginStartupInfo &info, Qt::LayoutDirection direction)
        : ILXQtPanelPlugin(info), bus(QDBusConnection::sessionBus()), dialogDirection(direction)
    {
        setAutoRaise(true);
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
        const auto status = QStringLiteral("PlaybackStatus");
        const auto control = QStringLiteral("CanControl");
        if (interface == playerInterface &&
            (changed.contains(status) || changed.contains(control) ||
             invalidated.contains(status) || invalidated.contains(control))) {
            refreshTimer.start();
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
                        best = {name, status, priority};
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
        updateButton();
        refreshing = false;
        if (refreshPending) {
            refreshPending = false;
            refreshTimer.start();
        }
    }

    void updateButton()
    {
        const bool playing = selected.status == QStringLiteral("Playing");
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
        setIcon(chosen);
        setToolTip(selected.service.isEmpty() ? tr("No media player available")
                                             : selected.service.mid(playerPrefix.size()));
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
