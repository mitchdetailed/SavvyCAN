#include <QCanBus>
#include <QMessageBox>
#include "newconnectiondialog.h"
#include "ui_newconnectiondialog.h"
#include "gsusbconnection.h"
#include "canalystii.h"
#include "kvasercanlib.h"
#include "ixxatvci.h"

NewConnectionDialog::NewConnectionDialog(QVector<QString>* gvretips, QVector<QString>* kayakhosts, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NewConnectionDialog),
    remoteDeviceIPGVRET(gvretips),
    remoteBusKayak(kayakhosts)
{
    ui->setupUi(this);
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

    connect(ui->cbDeviceType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NewConnectionDialog::handleDeviceTypeChanged);
    connect(ui->btnOK, &QPushButton::clicked, this, &NewConnectionDialog::handleCreateButton);
    connect(ui->btnScanDevices, &QPushButton::clicked, this, &NewConnectionDialog::handleScanDevices);

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

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

void NewConnectionDialog::selectLawicel()
{
    ui->lPort->setText("Serial Port:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);

    ui->cbCANSpeed->setHidden(false);
    ui->cbSerialSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    ui->lblSerialSpeed->setHidden(false);
    ui->cbCanFd->setHidden(false);
    ui->cbDataRate->setHidden(false);
    ui->lblDataRate->setHidden(false);

    ui->cbPort->clear();
    ports = QSerialPortInfo::availablePorts();

    for (int i = 0; i < ports.size(); i++)
        ui->cbPort->addItem(ports[i].portName());

    if (ui->cbCANSpeed->count() == 0)
    {
        ui->cbCANSpeed->addItem("10000");
        ui->cbCANSpeed->addItem("20000");
        ui->cbCANSpeed->addItem("50000");
        ui->cbCANSpeed->addItem("83333");
        ui->cbCANSpeed->addItem("100000");
        ui->cbCANSpeed->addItem("125000");
        ui->cbCANSpeed->addItem("250000");
        ui->cbCANSpeed->addItem("500000");
        ui->cbCANSpeed->addItem("1000000");
    }
    if (ui->cbDataRate->count() == 0)
    {
        ui->cbDataRate->addItem("1000000");
        ui->cbDataRate->addItem("2000000");
        ui->cbDataRate->addItem("4000000");
        ui->cbDataRate->addItem("5000000");
    }
    if (ui->cbSerialSpeed->count() == 0)
    {
        ui->cbSerialSpeed->addItem("115200");
        ui->cbSerialSpeed->addItem("150000");
        ui->cbSerialSpeed->addItem("250000");
        ui->cbSerialSpeed->addItem("500000");
        ui->cbSerialSpeed->addItem("1000000");
        ui->cbSerialSpeed->addItem("2000000");
        ui->cbSerialSpeed->addItem("3000000");
    }

}

void NewConnectionDialog::populateCANSpeeds()
{
    if (ui->cbCANSpeed->count() > 0) return;

    ui->cbCANSpeed->addItem("10000");
    ui->cbCANSpeed->addItem("20000");
    ui->cbCANSpeed->addItem("50000");
    ui->cbCANSpeed->addItem("83333");
    ui->cbCANSpeed->addItem("100000");
    ui->cbCANSpeed->addItem("125000");
    ui->cbCANSpeed->addItem("250000");
    ui->cbCANSpeed->addItem("500000");
    ui->cbCANSpeed->addItem("1000000");
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

/*
 * Most of the adapters added for python-can parity are just "pick a serial port, pick a CAN speed",
 * so they all share this. defaultSerialSpeed picks the entry the device actually ships with.
 */
void NewConnectionDialog::selectSerialAdapter(bool showBusSpeed, bool showSerialSpeed, int defaultSerialSpeed)
{
    ui->lPort->setText("Serial Port:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(!showBusSpeed);
    ui->lblCANSpeed->setHidden(!showBusSpeed);
    ui->cbSerialSpeed->setHidden(!showSerialSpeed);
    ui->lblSerialSpeed->setHidden(!showSerialSpeed);
    //none of these adapters do CAN-FD
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
    ports = QSerialPortInfo::availablePorts();
    for (int i = 0; i < ports.size(); i++)
        ui->cbPort->addItem(ports[i].portName());

    if (showBusSpeed) populateCANSpeeds();

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
    selectSerialAdapter(true, true, 2000000);
}

void NewConnectionDialog::selectRobotell()
{
    selectSerialAdapter(true, true, 115200);
}

void NewConnectionDialog::selectPythonCan()
{
    //no CAN speed here, whatever is on the other end of the port owns the bus
    selectSerialAdapter(false, true, 115200);
}

//these three take an index or address rather than a serial port
static void hideSerialBits(Ui::NewConnectionDialog *ui)
{
    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);
}

void NewConnectionDialog::selectCanalystII()
{
    ui->lPort->setText("CANalyst-II Device:");

    hideSerialBits(ui);
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    ui->cbPort->clear();
    //scanning USB is cheap and needs no vendor driver, so just show what's plugged in
    if (!populateScannedDevices(CANCon::CANALYSTII, true)) ui->cbPort->addItem("0");
}

void NewConnectionDialog::selectKvaser()
{
    ui->lPort->setText("CANlib Channel:");

    hideSerialBits(ui);
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    //Kvaser FD adapters can run CAN FD, so offer the FD controls for this one
    ui->cbCanFd->setHidden(false);
    ui->cbDataRate->setHidden(false);
    ui->lblDataRate->setHidden(false);
    if (ui->cbDataRate->count() == 0)
    {
        ui->cbDataRate->addItem("1000000");
        ui->cbDataRate->addItem("2000000");
        ui->cbDataRate->addItem("4000000");
        ui->cbDataRate->addItem("5000000");
    }

    //channel numbers, the driver decides what is actually behind them
    ui->cbPort->clear();
    for (int i = 0; i < 8; i++) ui->cbPort->addItem(QString::number(i));
}

void NewConnectionDialog::selectIxxat()
{
    ui->lPort->setText("VCI Device:Channel:");

    hideSerialBits(ui);
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    ui->cbPort->clear();
    ui->cbPort->addItem("0:0");
    ui->cbPort->addItem("0:1");
}

void NewConnectionDialog::selectUdpMulticast()
{
    ui->lPort->setText("Multicast Group:");

    hideSerialBits(ui);
    //there's no hardware to configure, the group members just agree on a bus
    ui->cbCANSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);

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
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    ui->cbPort->clear();
}

void NewConnectionDialog::selectIscan()
{
    ui->lPort->setText("isCAN Channel:");

    hideSerialBits(ui);
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    ui->cbPort->clear();
    for (int i = 0; i < 4; i++) ui->cbPort->addItem(QString::number(i));
}

void NewConnectionDialog::selectNican()
{
    ui->lPort->setText("NI-CAN Interface:");

    hideSerialBits(ui);
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    //NI-CAN addresses its interfaces by name rather than by number
    ui->cbPort->clear();
    for (int i = 0; i < 4; i++) ui->cbPort->addItem(QString("CAN%1").arg(i));
}

void NewConnectionDialog::selectNeousys()
{
    ui->lPort->setText("CAN Port:");

    hideSerialBits(ui);
    ui->cbCANSpeed->setHidden(false);
    ui->lblCANSpeed->setHidden(false);
    populateCANSpeeds();

    ui->cbPort->clear();
    for (int i = 0; i < 2; i++) ui->cbPort->addItem(QString::number(i));
}

void NewConnectionDialog::selectSerial()
{
    ui->lPort->setText("Serial Port:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
    ports = QSerialPortInfo::availablePorts();

    for (int i = 0; i < ports.size(); i++)
        ui->cbPort->addItem(ports[i].portName());
}

void NewConnectionDialog::selectSocketCan()
{
    ui->lPort->setText("Port:");
    ui->lblDeviceType->setHidden(false);
    ui->cbDeviceType->setHidden(false);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbDeviceType->clear();
    QStringList plugins;
    plugins = QCanBus::instance()->plugins();
    for (int i = 0; i < plugins.size(); i++)
        ui->cbDeviceType->addItem(plugins[i]);

}

void NewConnectionDialog::selectRemote()
{
    ui->lPort->setText("IP Address:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
    foreach(QString pName, *remoteDeviceIPGVRET)
    {
        ui->cbPort->addItem(pName);
    }
}

void NewConnectionDialog::selectKayak()
{
    ui->lPort->setText("Available Bus(ses):");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
    foreach(QString pName, *remoteBusKayak)
    {
        ui->cbPort->addItem(pName);
    }
}

void NewConnectionDialog::selectMQTT()
{
    ui->lPort->setText("Topic Name:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
}

void NewConnectionDialog::selectCANserver()
{
    ui->lPort->setText("CANserver IP Address:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
}

void NewConnectionDialog::selectCANlogserver()
{
    ui->lPort->setText("CANlogserver IP Address:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(true);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(true);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
}

void NewConnectionDialog::selectGSUSB()
{
    ui->lPort->setText("GSUSB Port:");

    ui->lblDeviceType->setHidden(true);
    ui->cbDeviceType->setHidden(true);
    ui->cbCANSpeed->setHidden(false);
    ui->cbSerialSpeed->setHidden(true);
    ui->lblCANSpeed->setHidden(false);
    ui->lblSerialSpeed->setHidden(true);
    ui->cbCanFd->setHidden(true);
    ui->cbDataRate->setHidden(true);
    ui->lblDataRate->setHidden(true);

    ui->cbPort->clear();
    //scanning USB is cheap and needs no vendor driver, so just show what's plugged in
    populateScannedDevices(CANCon::GSUSB, true);

    if (ui->cbCANSpeed->count() == 0)
    {
        ui->cbCANSpeed->addItem("10000");
        ui->cbCANSpeed->addItem("20000");
        ui->cbCANSpeed->addItem("50000");
        ui->cbCANSpeed->addItem("83333");
        ui->cbCANSpeed->addItem("100000");
        ui->cbCANSpeed->addItem("125000");
        ui->cbCANSpeed->addItem("250000");
        ui->cbCANSpeed->addItem("500000");
        ui->cbCANSpeed->addItem("1000000");
    }
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
        {
            int idx = ui->cbPort->findText(pPortName);
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

int NewConnectionDialog::getBusSpeed()
{
    switch (getConnectionType())
    {
    case CANCon::LAWICEL:
    case CANCon::GSUSB:
    case CANCon::SEEEDSTUDIO:
    case CANCon::ROBOTELL:
    case CANCon::CANALYSTII:
    case CANCon::KVASER:
    case CANCon::IXXAT:
    case CANCon::USB2CAN:
    case CANCon::ISCAN:
    case CANCon::NICAN:
    case CANCon::NEOUSYS:
        return ui->cbCANSpeed->currentText().toInt();
    default:
        return 0;
    }
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
    switch (getConnectionType())
    {
    case CANCon::LAWICEL:
    case CANCon::KVASER:
        return ui->cbDataRate->currentText().toInt();
    default:
        return 0;
    }
}

bool NewConnectionDialog::isCanFd()
{
    switch (getConnectionType())
    {
    case CANCon::LAWICEL:
    case CANCon::KVASER:
        /* NB: this used to return the checkbox pointer itself, which is never null, so CAN FD was
         * reported as enabled no matter what the user had ticked. */
        return ui->cbCanFd->isChecked();
    default:
        return false;
    }
}
