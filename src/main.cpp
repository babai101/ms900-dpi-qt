// ms900-dpi-gui - Qt Widgets DPI switcher for the Dell Pro Premium Mouse MS900.
#include "ms900.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSocketNotifier>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <errno.h>
#include <string.h>
#include <unistd.h>

static const int PRESETS[] = {800, 1600, 2400, 3200, 4000, 8000};

class DpiWindow : public QWidget {
    Q_OBJECT
public:
    explicit DpiWindow(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowTitle(tr("MS900 DPI"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("input-mouse")));

        // device row
        m_devices = new QComboBox;
        m_devices->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *refreshBtn = new QPushButton(tr("Refresh"));
        auto *devRow = new QHBoxLayout;
        devRow->addWidget(m_devices);
        devRow->addWidget(refreshBtn);

        // slider + spinbox, kept in sync, 200-DPI steps
        const int steps = (MS900_DPI_MAX - MS900_DPI_MIN) / MS900_DPI_STEP;
        m_slider = new QSlider(Qt::Horizontal);
        m_slider->setRange(0, steps);
        m_slider->setPageStep(1);
        m_slider->setTickPosition(QSlider::TicksBelow);
        m_slider->setTickInterval(5);           // a tick every 1000 DPI
        m_spin = new QSpinBox;
        m_spin->setRange(MS900_DPI_MIN, MS900_DPI_MAX);
        m_spin->setSingleStep(MS900_DPI_STEP);
        m_spin->setSuffix(tr(" DPI"));
        m_spin->setKeyboardTracking(false);
        auto *dpiRow = new QHBoxLayout;
        dpiRow->addWidget(m_slider, 1);
        dpiRow->addWidget(m_spin);

        // presets apply immediately
        auto *presetRow = new QHBoxLayout;
        for (int p : PRESETS) {
            auto *b = new QPushButton(QString::number(p));
            b->setToolTip(tr("Set %1 DPI now").arg(p));
            connect(b, &QPushButton::clicked, this, [this, p] { setDpi(p); apply(); });
            presetRow->addWidget(b);
            m_controls << b;
        }

        auto *form = new QFormLayout;
        form->addRow(tr("Device"), devRow);
        form->addRow(tr("DPI"), dpiRow);
        form->addRow(tr("Presets"), presetRow);

        m_apply = new QPushButton(tr("Apply"));
        m_apply->setDefault(true);
        m_apply->setMinimumHeight(m_apply->sizeHint().height() * 4 / 3);

        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        m_status = new QLabel;
        m_status->setWordWrap(true);
        m_current = new QLabel;
        m_current->setEnabled(false);          // greyed, secondary text

        auto *root = new QVBoxLayout(this);
        root->addLayout(form);
        root->addWidget(line);
        root->addWidget(m_apply);
        root->addWidget(m_status);
        root->addWidget(m_current);

        m_controls << m_slider << m_spin << m_apply;

        connect(m_slider, &QSlider::valueChanged, this, [this](int i) {
            setDpi(MS900_DPI_MIN + i * MS900_DPI_STEP);
        });
        connect(m_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
            setDpi(v);
        });
        connect(m_apply, &QPushButton::clicked, this, &DpiWindow::apply);
        connect(refreshBtn, &QPushButton::clicked, this, &DpiWindow::refresh);

        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] { finish(-1); });

        m_applied = ms900_load_last();
        setDpi(m_applied ? m_applied : 1600);
        updateCurrent();
        setStatus(tr("Pick a DPI and press Apply."), Neutral);
        refresh();
    }

    ~DpiWindow() override { closeAck(); }

private:
    enum Tone { Neutral, Ok, Warn, Error };

    void setDpi(int v)
    {
        // snap to the 200-DPI grid (typed values like 1700 round to 1800)
        v = qBound(MS900_DPI_MIN, v, MS900_DPI_MAX);
        v = MS900_DPI_MIN + ((v - MS900_DPI_MIN + MS900_DPI_STEP / 2) / MS900_DPI_STEP) * MS900_DPI_STEP;
        m_dpi = v;
        QSignalBlocker a(m_slider), b(m_spin);
        m_slider->setValue((v - MS900_DPI_MIN) / MS900_DPI_STEP);
        m_spin->setValue(v);
    }

    void refresh()
    {
        if (busy()) return;
        m_ndevs = ms900_find(m_devs, MS900_MAX_DEVS);
        m_devices->clear();
        for (int i = 0; i < m_ndevs; i++)
            m_devices->addItem(QStringLiteral("%1  (%2)")
                               .arg(QString::fromLocal8Bit(m_devs[i].path),
                                    QString::fromLocal8Bit(m_devs[i].kind)));
        if (!m_ndevs) {
            m_devices->addItem(tr("none found"));
            setStatus(tr("No MS900 found. Plug in the receiver, then Refresh."), Warn);
        }
        updateEnabled();
    }

    void apply()
    {
        if (busy() || !m_ndevs) return;
        const int idx = qMax(0, m_devices->currentIndex());
        const char *path = m_devs[idx].path;
        const int r = ms900_send(path, m_dpi);
        if (r == -EACCES || r == -EPERM) {
            setStatus(tr("Permission denied on %1. Install the udev rule (see README).")
                      .arg(QString::fromLocal8Bit(path)), Error);
            return;
        }
        if (r < 0) {
            setStatus(tr("Error: %1").arg(QString::fromLocal8Bit(strerror(-r))), Error);
            return;
        }

        startWait(r, m_dpi);
    }

    // wait for the mouse's ack on fd without blocking the UI
    void startWait(int fd, int dpi)
    {
        m_fd = fd;
        m_pending = dpi;
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this] {
            int a = ms900_poll_ack(m_fd);
            if (a != -1) finish(a);
        });
        m_timeout.start(1000);
        setStatus(tr("Setting %1 DPI…").arg(m_pending), Neutral);
        updateEnabled();
    }

    // a: 1 = accepted, 0 = rejected, -1 = no reply in time
    void finish(int a)
    {
        if (!busy()) return;
        m_timeout.stop();
        closeAck();
        if (a == 0) {
            setStatus(tr("Mouse rejected %1 DPI.").arg(m_pending), Error);
        } else {
            m_applied = m_pending;
            ms900_save_last(m_pending);
            if (a == 1) setStatus(tr("DPI set to %1.").arg(m_pending), Ok);
            else        setStatus(tr("Sent %1 DPI (no confirmation from mouse).").arg(m_pending), Warn);
            updateCurrent();
        }
        updateEnabled();
    }

    void closeAck()
    {
        if (m_notifier) { m_notifier->setEnabled(false); m_notifier->deleteLater(); m_notifier = nullptr; }
        if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
    }

    bool busy() const { return m_fd >= 0; }

    void updateEnabled()
    {
        const bool on = !busy() && m_ndevs > 0;
        for (QWidget *w : m_controls) w->setEnabled(on);
        m_devices->setEnabled(!busy() && m_ndevs > 1);
    }

    void updateCurrent()
    {
        m_current->setText(m_applied ? tr("Current: %1 DPI").arg(m_applied)
                                     : tr("Current: unknown"));
    }

    void setStatus(const QString &text, Tone tone)
    {
        static const char *colors[] = {"", "#3a9a3a", "#b8860b", "#c0392b"};
        m_status->setStyleSheet(tone == Neutral ? QString()
                                : QStringLiteral("color: %1;").arg(QLatin1String(colors[tone])));
        m_status->setText(text);
    }

    ms900_dev m_devs[MS900_MAX_DEVS];
    int m_ndevs = 0;
    int m_dpi = 1600;
    int m_applied = 0;
    int m_pending = 0;
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_timeout;

    QComboBox *m_devices;
    QSlider *m_slider;
    QSpinBox *m_spin;
    QPushButton *m_apply;
    QLabel *m_status;
    QLabel *m_current;
    QList<QWidget *> m_controls;

    friend class DpiWindowTest;
};

#ifndef MS900_NO_MAIN
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ms900-dpi"));
    QApplication::setDesktopFileName(QStringLiteral("ms900-dpi"));
    DpiWindow w;
    w.show();
    return app.exec();
}
#endif

#include "main.moc"
