#include <QCanBus>
#include <QMessageBox>
#include <QGridLayout>
#include <QLabel>
#include "newconnectiondialog.h"
#include "ui_newconnectiondialog.h"
#include "gsusbconnection.h"
#include "canalystii.h"
#include "kvasercanlib.h"
#include "ixxatvci.h"
#include "cantriple.h"

NewConnectionDialog::NewConnectionDialog(QVector<QString>* gvretips, QVector<QString>* kayakhosts, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NewConnectionDialog),
    remoteDeviceIPGVRET(gvretips),
    remoteBusKayak(kayakhosts),
    gbBuses(nullptr)
{
    ui->setupUi(this);
    buildBusTable(CTSC_NUM_BUSES);
    if (isSerialBusAvailable())
    {
        ui->rbSocketCAN->setEnabled(true);
    }
    else
    {
        ui->rbSocketCAN->setEnabled(false);
        QString errorString;
        const QList<QCanBusDeviceInfo> devices = QCanBus::instance()->availableDevices(QStringLiteral("socketcan"), &errorString);
        if (!errorString.isEmpty()) ui->rbSocketCAN->setToolTip(errorString);
    }


    connect(ui->rbGVRET, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbSocketCAN, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbRemote, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbKayak, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbMQTT, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbLawicel, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbCANserver, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbCanlogserver, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbGSUSB, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbSeeed, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbRobotell, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbCanalystII, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbKvaser, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbIxxat, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbPythonCan, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbUdpMulticast, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbUsb2Can, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbIscan, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbNican, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbNeousys, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);
    connect(ui->rbCanTriple, &QAbstractButton::clicked, this, &NewConnectionDialog::handleConnTypeChanged);

    connect(ui->cbDeviceType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NewConnectionDialog::handleDeviceTypeChanged);
    connect(ui->btnOK, &QPushButton::clicked, this, &NewConnectionDialog::handleCreateButton);
    connect(ui->btnScanDevices, &QPushButton::clicked, this, &NewConnectionDialog::handleScanDevices);
    connect(ui->cbCanFd, &QCheckBox::toggled, this, &NewConnectionDialog::handleCanFdChanged);

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);

    //the speed / listen only / CAN FD controls are owned by updateBusOptionVisibility(), which
    //selectSerial() reaches through handleConnTypeChanged's usual path
    selectSerial();

    qDebug() << "Was passed " << remoteDeviceIPGVRET->count() << " remote GVRET IPs";
    qDebug() << "Was passed " << remoteBusKayak->count() << " remote Kayak Busses";
}

NewConnectionDialog::~NewConnectionDialog()
{
    delete ui;
}

void NewConnectionDialog::handleCreateButton()
{
    accept();
}

void NewConnectionDialog::handleConnTypeChanged()
{
    if (ui->rbGVRET->isChecked()) selectSerial();
    if (ui->rbSocketCAN->isChecked()) selectSocketCan();
    if (ui->rbLawicel->isChecked()) selectLawicel();
    if (ui->rbRemote->isChecked()) selectRemote();
    if (ui->rbKayak->isChecked()) selectKayak();
    if (ui->rbMQTT->isChecked()) selectMQTT();
    if (ui->rbCANserver->isChecked()) selectCANserver();
    if (ui->rbCanlogserver->isChecked()) selectCANlogserver();
    if (ui->rbGSUSB->isChecked()) selectGSUSB();
    if (ui->rbSeeed->isChecked()) selectSeeed();
    if (ui->rbRobotell->isChecked()) selectRobotell();
    if (ui->rbCanalystII->isChecked()) selectCanalystII();
    if (ui->rbKvaser->isChecked()) selectKvaser();
    if (ui->rbIxxat->isChecked()) selectIxxat();
    if (ui->rbPythonCan->isChecked()) selectPythonCan();
    if (ui->rbUdpMulticast->isChecked()) selectUdpMulticast();
    if (ui->rbUsb2Can->isChecked()) selectUsb2Can();
    if (ui->rbIscan->isChecked()) selectIscan();
    if (ui->rbNican->isChecked()) selectNican();
    if (ui->rbNeousys->isChecked()) selectNeousys();
    if (ui->rbCanTriple->isChecked()) selectCanTriple();
}

/*
 * Ask the selected driver what hardware it can see and fill the port list with the answer.
 *
 * The list entries show a readable description but carry the value the driver actually needs as
 * user data, which getPortName() picks up. Not every driver's library offers a way to enumerate,
 * and for those we say so rather than silently doing nothing.
 */
bool NewConnectionDialog::populateScannedDevices(CANCon::type type, bool quiet)
{
    QList<CANDeviceInfo> devices;
    QString what;

    switch (type)
    {
    case CANCon::GSUSB:
        what = "gs_usb";
        devices = GSUSBConnection::enumerateDevices();
        break;
    case CANCon::CANALYSTII:
        what = "CANalyst-II";
        devices = CanalystII::enumerateDevices();
        break;
    case CANCon::KVASER:
        what = "Kvaser";
        devices = KvaserCanlib::enumerateDevices();
        break;
    case CANCon::IXXAT:
        what = "IXXAT";
        devices = IxxatVci::enumerateDevices();
        break;
    case CANCon::CANTRIPLE:
        what = "CAN Triple";
        devices = CANTriple::enumerateDevices();
        break;

    default:
        if (!quiet)
        {
            QMessageBox::information(this, tr("Scan for devices"),
                                     tr("This connection type has no way to list attached devices, so "
                                        "the port has to be entered by hand."));
        }
        return false;
    }

    if (devices.isEmpty())
    {
        if (!quiet)
        {
            QMessageBox::information(this, tr("Scan for devices"),
                                     tr("No %1 devices were found.\n\nEither nothing is attached or the "
                                        "driver it needs isn't installed on this machine.").arg(what));
        }
        return false;
    }

    ui->cbPort->clear();
    foreach (const CANDeviceInfo &dev, devices)
    {
        //display the friendly name, hand the driver the key
        ui->cbPort->addItem(dev.description, dev.key);
    }
    ui->cbPort->setCurrentIndex(0);
    return true;
}

/*
 * Ask the selected driver what hardware it can see and fill the port list with the answer. Not
 * every driver's library offers a way to enumerate, and for those we say so rather than silently
 * doing nothing.
 */
void NewConnectionDialog::handleScanDevices()
{
    switch (getConnectionType())
    {
    case CANCon::GVRET_SERIAL:
    case CANCon::LAWICEL:
    case CANCon::SEEEDSTUDIO:
    case CANCon::ROBOTELL:
    case CANCon::PYCAN_SERIAL:
    case CANCon::SERIALBUS:
        //these are serial ports or QT plugins, rescanning just means asking again
        handleConnTypeChanged();
        return;
    default:
        populateScannedDevices(getConnectionType(), false);
    }
}

void NewConnectionDialog::handleDeviceTypeChanged()
{

    ui->cbPort->clear();
    canDevices = QCanBus::instance()->availableDevices(ui->cbDeviceType->currentText());

    for (int i = 0; i < canDevices.size(); i++)
        ui->cbPort->addItem(canDevices[i].name());
}

/*
 * Which of the bus options an adapter can actually make use of.
 *
 * This used to be decided by hand inside every select* function below, which meant twenty places
 * to keep in step and no guarantee that a control the dialog put on screen was one the driver
 * would go on to honour. The visibility of the controls and the values the getters hand back both
 * come from this one table now, so an option is offered exactly when it is going to be used.
 */
struct BusOptionSupport
{
    bool speed;      //the adapter lets us choose the arbitration bit rate
    bool listenOnly; //the driver has a silent / listen only mode
    bool canFd;      //the adapter does CAN FD, which brings the data rate with it
    bool termination;//the adapter can switch its own terminating resistor in
};

static BusOptionSupport busOptionsFor(CANCon::type pType)
{
    switch (pType)
    {
    //GVRET carries its own bus configuration, whether it is reached over serial or over the network
    case CANCon::GVRET_SERIAL:
    case CANCon::REMOTE:
        return { true, true, false, false };

    /* Whether CAN FD really is there depends on which model answers - a 2.x has it, a 1.x does
     * not - and that is not known until the device is reached. The driver runs a 1.x as classic
     * CAN and says so in the debug console, so offering it here costs nothing. */
    case CANCon::CANTRIPLE:
        return { true, true, true, true };

    /* QT SerialBus covers PEAK, Vector, IXXAT's own plugin and socketcan. Whether the bit rate is
     * ours to set depends on the plugin - socketcan takes it from the OS - but the value is still
     * what the bus load figures get worked out against, so it is worth asking for either way. */
    case CANCon::SERIALBUS:
        return { true, true, true, false };

    case CANCon::LAWICEL:
    case CANCon::KVASER:
        return { true, true, true, false };

    case CANCon::GSUSB:
    case CANCon::SEEEDSTUDIO:
    case CANCon::CANALYSTII:
    case CANCon::IXXAT:
        return { true, true, false, false };

    //these take a speed, but their drivers have no listen only mode to offer
    case CANCon::ROBOTELL:
    case CANCon::USB2CAN:
    case CANCon::ISCAN:
    case CANCon::NICAN:
    case CANCon::NEOUSYS:
        return { true, false, false, false };

    /* Whatever sits at the far end of these owns the bus - a socketcand host, an MQTT broker, a
     * CANserver, another python-can instance. There is nothing here for us to configure. */
    case CANCon::KAYAK:
    case CANCon::MQTT:
    case CANCon::CANSERVER:
    case CANCon::CANLOGSERVER:
    case CANCon::PYCAN_SERIAL:
    case CANCon::UDP_MULTICAST:
    default:
        return { false, false, false, false };
    }
}

/*
 * How many buses the dialog sets up one by one for a connection type.
 *
 * Most adapters get the single row of bus controls, which configures their first bus; a multi bus
 * GVRET works the rest out for itself. A CAN Triple is bought for its three buses, and each one can
 * sit on a different network at a different speed with its own termination, so it gets the per bus
 * table instead and every bus is set up before the connection is ever opened.
 */
static int dialogBusCount(CANCon::type pType)
{
    if (pType == CANCon::CANTRIPLE) return CTSC_NUM_BUSES;
    return 1;
}

static void fillCANSpeeds(QComboBox *box)
{
    for (const char *s : {"10000", "20000", "50000", "83333", "100000", "125000", "250000", "500000", "1000000"})
        box->addItem(s);
    /* Start on the speed a bus most often runs at rather than on the slowest entry in the list.
     * This dialog decides what a new connection opens with now, so whatever sits at the top has
     * to be a sane answer for somebody who never touches it. It matches CANBus's own default. */
    box->setCurrentIndex(box->findText("500000"));
}

/*
 * The per bus table: one row per bus, with the same options Bus Details offers for it afterwards.
 *
 * Only the first bus starts out enabled. A bus that is switched on joins in on whatever it is wired
 * to, and one at the wrong speed and not in listen only mode will put error frames onto a network
 * it was never meant to touch - so the other two wait for the user to ask for them.
 */
void NewConnectionDialog::buildBusTable(int count)
{
    gbBuses = new QGroupBox(tr("Buses"), this);
    QGridLayout *grid = new QGridLayout(gbBuses);

    const QStringList heads = { tr("Bus"), tr("Enabled"), tr("Speed"), tr("Listen Only"),
                                tr("Termination"), tr("CAN-FD"), tr("CAN-FD Data Rate") };
    for (int c = 0; c < heads.count(); c++)
        grid->addWidget(new QLabel("<b>" + heads[c] + "</b>", gbBuses), 0, c);

    for (int i = 0; i < count; i++)
    {
        BusRow row;
        const int r = i + 1;

        grid->addWidget(new QLabel(tr("Bus %1").arg(i + 1), gbBuses), r, 0);

        row.enabled = new QCheckBox(gbBuses);
        row.enabled->setChecked(i == 0);
        row.enabled->setToolTip(tr("Switch this bus on as soon as the connection opens"));
        grid->addWidget(row.enabled, r, 1);

        row.speed = new QComboBox(gbBuses);
        fillCANSpeeds(row.speed);
        grid->addWidget(row.speed, r, 2);

        row.listenOnly = new QCheckBox(gbBuses);
        row.listenOnly->setToolTip(ui->ckListenOnly->toolTip());
        grid->addWidget(row.listenOnly, r, 3);

        row.termination = new QCheckBox(gbBuses);
        row.termination->setToolTip(tr("Switch the CAN Triple's 120 Ohm terminating resistor across this bus"));
        grid->addWidget(row.termination, r, 4);

        row.canFd = new QCheckBox(gbBuses);
        row.canFd->setToolTip(tr("CAN Triple 2.x only - a 1.x runs this bus as classic CAN"));
        grid->addWidget(row.canFd, r, 5);

        row.dataRate = new QComboBox(gbBuses);
        for (const char *s : {"1000000", "2000000", "4000000", "5000000", "8000000"}) row.dataRate->addItem(s);
        //again, matching the default a CANBus is built with
        row.dataRate->setCurrentIndex(row.dataRate->findText("2000000"));
        grid->addWidget(row.dataRate, r, 6);

        busRows.append(row);
        connect(row.enabled, &QCheckBox::toggled, this, [this, i]() { updateBusRow(i); });
        connect(row.canFd, &QCheckBox::toggled, this, [this, i]() { updateBusRow(i); });
        updateBusRow(i);
    }
    grid->setColumnStretch(heads.count(), 1);

    //sits where the single row of bus controls does, and takes its place when it is shown
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->cbDataRate) + 1, gbBuses);
    gbBuses->setHidden(true);
}

//a bus that is off has nothing to configure, and the data rate means nothing without CAN FD
void NewConnectionDialog::updateBusRow(int row)
{
    if (row < 0 || row >= busRows.count()) return;
    const BusRow &r = busRows[row];
    const bool on = r.enabled->isChecked();
    r.speed->setEnabled(on);
    r.listenOnly->setEnabled(on);
    r.termination->setEnabled(on);
    r.canFd->setEnabled(on);
    r.dataRate->setEnabled(on && r.canFd->isChecked());
}

int NewConnectionDialog::getBusCount()
{
    return dialogBusCount(getConnectionType());
}

/*
 * Everything one bus should start out with. For the per bus table that is the row as the user left
 * it; for everything else it is the single row of controls, which only ever describes bus 0.
 */
CANBus NewConnectionDialog::getBusSettings(int bus)
{
    CANBus out;
    const CANCon::type type = getConnectionType();

    if (dialogBusCount(type) > 1)
    {
        if (bus < 0 || bus >= busRows.count()) return out;
        const BusRow &r = busRows[bus];
        const BusOptionSupport opts = busOptionsFor(type);
        out.setSpeed(r.speed->currentText().toInt());
        out.setListenOnly(opts.listenOnly && r.listenOnly->isChecked());
        out.setTermination(opts.termination && r.termination->isChecked());
        out.setCanFD(opts.canFd && r.canFd->isChecked());
        out.setDataRate(r.dataRate->currentText().toInt());
        out.setActive(r.enabled->isChecked());
        return out;
    }

    if (getBusSpeed() > 0) out.setSpeed(getBusSpeed());
    if (getDataRate() > 0) out.setDataRate(getDataRate());
    out.setCanFD(isCanFd());
    out.setListenOnly(isListenOnly());
    out.setTermination(isTermination());
    //a device the user has just asked for should come up running
    out.setActive(true);
    return out;
}

//put the bus options this connection type can use on screen, and no others
void NewConnectionDialog::updateBusOptionVisibility()
{
    BusOptionSupport opts = busOptionsFor(getConnectionType());

    //a multi bus adapter gets the per bus table in place of the single row of controls
    const bool table = dialogBusCount(getConnectionType()) > 1;
    if (gbBuses) gbBuses->setVisible(table);
    if (table) opts = { false, false, false, false };

    if (opts.speed) populateCANSpeeds();
    if (opts.canFd) populateDataRates();

    ui->lblCANSpeed->setHidden(!opts.speed);
    ui->cbCANSpeed->setHidden(!opts.speed);
    ui->ckListenOnly->setHidden(!opts.listenOnly);
    ui->ckTermination->setHidden(!opts.termination);
    ui->cbCanFd->setHidden(!opts.canFd);
    ui->lblDataRate->setHidden(!opts.canFd);
    ui->cbDataRate->setHidden(!opts.canFd);

    handleCanFdChanged();
}

//the data rate has no meaning until CAN FD is actually asked for
void NewConnectionDialog::handleCanFdChanged()
{
    const bool fd = ui->cbCanFd->isChecked();
    ui->lblDataRate->setEnabled(fd);
    ui->cbDataRate->setEnabled(fd);
}

//true when this connection type has any bus setting worth pushing into the new connection
bool NewConnectionDialog::hasBusOptions()
{
    const BusOptionSupport opts = busOptionsFor(getConnectionType());
    return opts.speed || opts.listenOnly || opts.canFd || opts.termination;
}

void NewConnectionDialog::populateCANSpeeds()
{
    if (ui->cbCANSpeed->count() > 0) return;
    fillCANSpeeds(ui->cbCANSpeed);
}

void NewConnectionDialog::populateDataRates()
{
    if (ui->cbDataRate->count() > 0) return;

    ui->cbDataRate->addItem("1000000");
    ui->cbDataRate->addItem("2000000");
    ui->cbDataRate->addItem("4000000");
    ui->cbDataRate->addItem("5000000");

    //again, matching the default a CANBus is built with
    ui->cbDataRate->setCurrentIndex(ui->cbDataRate->findText("2000000"));
}

void NewConnectionDialog::populateSerialSpeeds()
{
    if (ui->cbSerialSpeed->count() > 0) return;

    ui->cbSerialSpeed->addItem("115200");
    ui->cbSerialSpeed->addItem("150000");
    ui->cbSerialSpeed->addItem("250000");
    ui->cbSerialSpeed->addItem("500000");
    ui->cbSerialSpeed->addItem("1000000");
    ui->cbSerialSpeed->addItem("2000000");
    ui->cbSerialSpeed->addItem("3000000");
}

//fill the port list with whatever serial ports the machine currently has
void NewConnectionDialog::populateSerialPorts()
{
    ui->cbPort->clear();
    ports = QSerialPortInfo::availablePorts();

    for (int i = 0; i < ports.size(); i++)
        ui->cbPort->addItem(ports[i].portName());
}

void NewConnectionDialog::selectLawicel()
{
    ui->lPort->setText("Serial Port:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbSerialSpeed->setHidden(false);
    ui->lblSerialSpeed->setHidden(false);
    updateBusOptionVisibility();

    populateSerialPorts();
    populateSerialSpeeds();
}

/*
 * Most of the adapters added for python-can parity are just "pick a serial port, pick a CAN speed",
 * so they all share this. defaultSerialSpeed picks the entry the device actually ships with.
 */
void NewConnectionDialog::selectSerialAdapter(bool showSerialSpeed, int defaultSerialSpeed)
{
    ui->lPort->setText("Serial Port:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbSerialSpeed->setHidden(!showSerialSpeed);
    ui->lblSerialSpeed->setHidden(!showSerialSpeed);
    updateBusOptionVisibility();

    populateSerialPorts();

    if (showSerialSpeed)
    {
        populateSerialSpeeds();
        //the device's own default may not be in the list, so add it and pick it
        const QString wanted = QString::number(defaultSerialSpeed);
        if (ui->cbSerialSpeed->findText(wanted) < 0) ui->cbSerialSpeed->addItem(wanted);
        ui->cbSerialSpeed->setCurrentIndex(ui->cbSerialSpeed->findText(wanted));
    }
}

void NewConnectionDialog::selectSeeed()
{
    //the analyzer ships running at 2Mbps on the USB side
    selectSerialAdapter(true, 2000000);
}

void NewConnectionDialog::selectRobotell()
{
    selectSerialAdapter(true, 115200);
}

void NewConnectionDialog::selectPythonCan()
{
    //whatever is on the other end of the port owns the bus, so there is no CAN speed to pick here
    selectSerialAdapter(true, 115200);
}

//these take an index or an address rather than a serial port
static void hideSerialBits(Ui::NewConnectionDialog *ui)
{
    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
}

void NewConnectionDialog::selectCanalystII()
{
    ui->lPort->setText("CANalyst-II Device:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    //scanning USB is cheap and needs no vendor driver, so just show what's plugged in
    if (!populateScannedDevices(CANCon::CANALYSTII, true)) ui->cbPort->addItem("0");
}

void NewConnectionDialog::selectKvaser()
{
    ui->lPort->setText("CANlib Channel:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    //channel numbers, the driver decides what is actually behind them
    ui->cbPort->clear();
    for (int i = 0; i < 8; i++) ui->cbPort->addItem(QString::number(i));
}

void NewConnectionDialog::selectIxxat()
{
    ui->lPort->setText("VCI Device:Channel:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    ui->cbPort->addItem("0:0");
    ui->cbPort->addItem("0:1");
}

void NewConnectionDialog::selectUdpMulticast()
{
    ui->lPort->setText("Multicast Group:");

    hideSerialBits(ui);
    //there's no hardware to configure, the group members just agree on a bus
    updateBusOptionVisibility();

    ui->cbPort->clear();
    //python-can's IPv4 group, and its IPv6 default for anyone who needs to match that
    ui->cbPort->addItem("239.74.163.2:43113");
    ui->cbPort->addItem("[ff15:7079:7468:6f6e:6465:6d6f:6d63:6173]:43113");
}

void NewConnectionDialog::selectUsb2Can()
{
    //CANAL identifies the adapter by its serial number, which has to be typed in
    ui->lPort->setText("Adapter Serial Number:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
}

void NewConnectionDialog::selectIscan()
{
    ui->lPort->setText("isCAN Channel:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    for (int i = 0; i < 4; i++) ui->cbPort->addItem(QString::number(i));
}

void NewConnectionDialog::selectNican()
{
    ui->lPort->setText("NI-CAN Interface:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    //NI-CAN addresses its interfaces by name rather than by number
    ui->cbPort->clear();
    for (int i = 0; i < 4; i++) ui->cbPort->addItem(QString("CAN%1").arg(i));
}

void NewConnectionDialog::selectNeousys()
{
    ui->lPort->setText("CAN Port:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    for (int i = 0; i < 2; i++) ui->cbPort->addItem(QString::number(i));
}

void NewConnectionDialog::selectSerial()
{
    ui->lPort->setText("Serial Port:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    populateSerialPorts();
}

void NewConnectionDialog::selectSocketCan()
{
    ui->lPort->setText("Port:");
    ui->lblDeviceType->setHidden(false);
    ui->cbDeviceType->setHidden(false);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    updateBusOptionVisibility();

    ui->cbDeviceType->clear();
    QStringList plugins;
    plugins = QCanBus::instance()->plugins();
    for (int i = 0; i < plugins.size(); i++)
        ui->cbDeviceType->addItem(plugins[i]);

}

void NewConnectionDialog::selectRemote()
{
    ui->lPort->setText("IP Address:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    foreach(QString pName, *remoteDeviceIPGVRET)
    {
        ui->cbPort->addItem(pName);
    }
}

void NewConnectionDialog::selectKayak()
{
    ui->lPort->setText("Available Bus(ses):");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    foreach(QString pName, *remoteBusKayak)
    {
        ui->cbPort->addItem(pName);
    }
}

void NewConnectionDialog::selectMQTT()
{
    ui->lPort->setText("Topic Name:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
}

void NewConnectionDialog::selectCANserver()
{
    ui->lPort->setText("CANserver IP Address:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
}

void NewConnectionDialog::selectCANlogserver()
{
    ui->lPort->setText("CANlogserver IP Address:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
}

/*
 * A CAN Triple is a serial port either way - its ST-LINK's VCP on a 1.x, its own USB CDC port on
 * a 2.x - so start from every port the machine has and then narrow it to the ones that look like
 * one. If none do the full list stays, since a port can sit behind something that hides its USB IDs.
 */
void NewConnectionDialog::selectCanTriple()
{
    ui->lPort->setText("CAN Triple Port:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    populateSerialPorts();
    populateScannedDevices(CANCon::CANTRIPLE, true);
}

void NewConnectionDialog::selectGSUSB()
{
    ui->lPort->setText("GSUSB Port:");

    hideSerialBits(ui);
    updateBusOptionVisibility();

    ui->cbPort->clear();
    //scanning USB is cheap and needs no vendor driver, so just show what's plugged in
    populateScannedDevices(CANCon::GSUSB, true);
}


void NewConnectionDialog::setPortName(CANCon::type pType, QString pPortName, QString pDriver)
{

    switch(pType)
    {
        case CANCon::GVRET_SERIAL:
            ui->rbGVRET->setChecked(true);
            break;
        case CANCon::SERIALBUS:
            ui->rbSocketCAN->setChecked(true);
            break;
        case CANCon::REMOTE:
            ui->rbRemote->setChecked(true);
            break;
        case CANCon::KAYAK:
            ui->rbKayak->setChecked(true);
            break;
        case CANCon::MQTT:
            ui->rbMQTT->setChecked(true);
            break;
        case CANCon::LAWICEL:
            ui->rbLawicel->setChecked(true);
            break;
        case CANCon::CANSERVER:
          ui->rbCANserver->setChecked(true);
          break;
        case CANCon::CANLOGSERVER:
          ui->rbCanlogserver->setChecked(true);
          break;
        case CANCon::GSUSB:
          ui->rbGSUSB->setChecked(true);
          break;
        case CANCon::SEEEDSTUDIO:
          ui->rbSeeed->setChecked(true);
          break;
        case CANCon::ROBOTELL:
          ui->rbRobotell->setChecked(true);
          break;
        case CANCon::CANALYSTII:
          ui->rbCanalystII->setChecked(true);
          break;
        case CANCon::KVASER:
          ui->rbKvaser->setChecked(true);
          break;
        case CANCon::IXXAT:
          ui->rbIxxat->setChecked(true);
          break;
        case CANCon::PYCAN_SERIAL:
          ui->rbPythonCan->setChecked(true);
          break;
        case CANCon::UDP_MULTICAST:
          ui->rbUdpMulticast->setChecked(true);
          break;
        case CANCon::USB2CAN:
          ui->rbUsb2Can->setChecked(true);
          break;
        case CANCon::ISCAN:
          ui->rbIscan->setChecked(true);
          break;
        case CANCon::NICAN:
          ui->rbNican->setChecked(true);
          break;
        case CANCon::NEOUSYS:
          ui->rbNeousys->setChecked(true);
          break;
        case CANCon::CANTRIPLE:
          ui->rbCanTriple->setChecked(true);
          break;
        default: {}
    }

    /* refresh names whenever needed */
    //handleConnTypeChanged();

    switch(pType)
    {
        case CANCon::GVRET_SERIAL:
        case CANCon::LAWICEL:
        case CANCon::SEEEDSTUDIO:
        case CANCon::ROBOTELL:
        case CANCon::PYCAN_SERIAL:
        case CANCon::CANTRIPLE:
        {
            int idx = ui->cbPort->findText(pPortName);
            //a scanned entry shows a description and keeps the port name as its data
            if (idx < 0) idx = ui->cbPort->findData(pPortName);
            if( idx<0 ) idx=0;
            ui->cbPort->setCurrentIndex(idx);
            break;
        }
        case CANCon::CANALYSTII:
        case CANCon::KVASER:
        case CANCon::IXXAT:
        case CANCon::UDP_MULTICAST:
        case CANCon::USB2CAN:
        case CANCon::ISCAN:
        case CANCon::NICAN:
        case CANCon::NEOUSYS:
        {
            //these are typed in rather than picked from real hardware, so keep whatever was saved
            int idx = ui->cbPort->findText(pPortName);
            if (idx > -1) ui->cbPort->setCurrentIndex(idx);
            else ui->cbPort->setCurrentText(pPortName);
            break;
        }
        case CANCon::SERIALBUS:
        {
            int idx = ui->cbDeviceType->findText(pDriver);
            if (idx < 0) idx = 0;
            ui->cbDeviceType->setCurrentIndex(idx);
            idx = ui->cbPort->findText(pPortName);
            if( idx < 0 ) idx = 0;
            ui->cbPort->setCurrentIndex(idx);
            break;
        }
        case CANCon::REMOTE:
        {
            int idx = ui->cbPort->findText(pPortName);
            if (idx > -1) ui->cbPort->setCurrentIndex(idx);
            else ui->cbPort->addItem(pPortName);
            break;
        }
        case CANCon::KAYAK:
        {
            int idx = ui->cbPort->findText(pPortName);
            if (idx > -1) ui->cbPort->setCurrentIndex(idx);
            else ui->cbPort->addItem(pPortName);
            break;
        }
        case CANCon::MQTT:
            ui->cbPort->setCurrentText(pPortName);
            break;
        case CANCon::CANSERVER:
        case CANCon::CANLOGSERVER:
        case CANCon::GSUSB:
        {
            ui->cbPort->setCurrentText(pPortName);
            break;
        }
        default: {}
    }
}

QString NewConnectionDialog::getPortName()
{
    /* An entry that came from a device scan shows a friendly description but carries the value the
     * driver needs as its user data. Anything typed in by hand has no user data, so the visible text
     * is what gets used. */
    const QVariant scanned = ui->cbPort->currentData();
    if (scanned.isValid() && !scanned.toString().isEmpty() &&
        ui->cbPort->currentText() == ui->cbPort->itemText(ui->cbPort->currentIndex()))
    {
        return scanned.toString();
    }

    switch( getConnectionType() ) {
    case CANCon::GVRET_SERIAL:
    case CANCon::SERIALBUS:
    case CANCon::REMOTE:
    case CANCon::MQTT:
    case CANCon::LAWICEL:
        return ui->cbPort->currentText();
    case CANCon::KAYAK:
        return ui->cbPort->currentText();
    case CANCon::CANSERVER:
    case CANCon::CANLOGSERVER:
    case CANCon::GSUSB:
    case CANCon::SEEEDSTUDIO:
    case CANCon::ROBOTELL:
    case CANCon::PYCAN_SERIAL:
    case CANCon::CANALYSTII:
    case CANCon::KVASER:
    case CANCon::IXXAT:
    case CANCon::UDP_MULTICAST:
    case CANCon::USB2CAN:
    case CANCon::ISCAN:
    case CANCon::NICAN:
    case CANCon::NEOUSYS:
    case CANCon::CANTRIPLE:
        return ui->cbPort->currentText();

    default:
        qDebug() << "getPortName: can't get port";
    }

    return "";
}

QString NewConnectionDialog::getDriverName()
{
    if (getConnectionType() == CANCon::SERIALBUS)
    {
        return ui->cbDeviceType->currentText();
    }
    return "N/A";
}

int NewConnectionDialog::getSerialSpeed()
{
    switch (getConnectionType())
    {
    case CANCon::LAWICEL:
    case CANCon::SEEEDSTUDIO:
    case CANCon::ROBOTELL:
    case CANCon::PYCAN_SERIAL:
        return ui->cbSerialSpeed->currentText().toInt();
    default:
        return 0;
    }
}

/* With the per bus table on screen these getters report its first row, which is what the
 * connection factory is handed. Otherwise they all key off the same table the controls are shown
 * from, so a value can only come back for an option the user was actually offered. */
int NewConnectionDialog::getBusSpeed()
{
    if (getBusCount() > 1) return getBusSettings(0).getSpeed();
    if (!busOptionsFor(getConnectionType()).speed) return 0;
    return ui->cbCANSpeed->currentText().toInt();
}

bool NewConnectionDialog::isListenOnly()
{
    if (getBusCount() > 1) return getBusSettings(0).isListenOnly();
    if (!busOptionsFor(getConnectionType()).listenOnly) return false;
    return ui->ckListenOnly->isChecked();
}

CANCon::type NewConnectionDialog::getConnectionType()
{
    if (ui->rbGVRET->isChecked()) return CANCon::GVRET_SERIAL;
    if (ui->rbSocketCAN->isChecked()) return CANCon::SERIALBUS;
    if (ui->rbRemote->isChecked()) return CANCon::REMOTE;
    if (ui->rbKayak->isChecked()) return CANCon::KAYAK;
    if (ui->rbMQTT->isChecked()) return CANCon::MQTT;
    if (ui->rbLawicel->isChecked()) return CANCon::LAWICEL;
    if (ui->rbCANserver->isChecked()) return CANCon::CANSERVER;
    if (ui->rbCanlogserver->isChecked()) return CANCon::CANLOGSERVER;
    if (ui->rbGSUSB->isChecked()) return CANCon::GSUSB;
    if (ui->rbSeeed->isChecked()) return CANCon::SEEEDSTUDIO;
    if (ui->rbRobotell->isChecked()) return CANCon::ROBOTELL;
    if (ui->rbCanalystII->isChecked()) return CANCon::CANALYSTII;
    if (ui->rbKvaser->isChecked()) return CANCon::KVASER;
    if (ui->rbIxxat->isChecked()) return CANCon::IXXAT;
    if (ui->rbPythonCan->isChecked()) return CANCon::PYCAN_SERIAL;
    if (ui->rbUdpMulticast->isChecked()) return CANCon::UDP_MULTICAST;
    if (ui->rbUsb2Can->isChecked()) return CANCon::USB2CAN;
    if (ui->rbIscan->isChecked()) return CANCon::ISCAN;
    if (ui->rbNican->isChecked()) return CANCon::NICAN;
    if (ui->rbNeousys->isChecked()) return CANCon::NEOUSYS;
    if (ui->rbCanTriple->isChecked()) return CANCon::CANTRIPLE;
    qDebug() << "getConnectionType: error";

    return CANCon::NONE;
}

bool NewConnectionDialog::isSerialBusAvailable()
{
    if (QCanBus::instance()->plugins().size() > 0) return true;
    return false;
}

int NewConnectionDialog::getDataRate()
{
    if (getBusCount() > 1) return getBusSettings(0).getDataRate();
    if (!busOptionsFor(getConnectionType()).canFd) return 0;
    return ui->cbDataRate->currentText().toInt();
}

bool NewConnectionDialog::isTermination()
{
    if (getBusCount() > 1) return getBusSettings(0).isTermination();
    if (!busOptionsFor(getConnectionType()).termination) return false;
    return ui->ckTermination->isChecked();
}

bool NewConnectionDialog::isCanFd()
{
    if (getBusCount() > 1) return getBusSettings(0).isCanFD();
    if (!busOptionsFor(getConnectionType()).canFd) return false;
    /* NB: this used to return the checkbox pointer itself, which is never null, so CAN FD was
     * reported as enabled no matter what the user had ticked. */
    return ui->cbCanFd->isChecked();
}
