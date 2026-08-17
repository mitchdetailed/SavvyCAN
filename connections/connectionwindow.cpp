#include <QCanBus>
#include <QNetworkDatagram>
#include <QThread>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>

//how many buses per connection we keep settings for. Nothing SavvyCAN talks to has more than this.
#define MAX_SAVED_BUSES 8

#include "connectionwindow.h"
#include "mainwindow.h"
#include "helpwindow.h"
#include "ui_connectionwindow.h"
#include "connections/canconfactory.h"
#include "connections/canconmanager.h"
#include "canbus.h"
#include <QSettings>
#include <connections/newconnectiondialog.h>

ConnectionWindow::ConnectionWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConnectionWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    QSettings settings;

    qRegisterMetaType<CANBus>("CANBus");
    qRegisterMetaType<const CANFrame *>("const CANFrame *");
    qRegisterMetaType<const QList<CANFrame> *>("const QList<CANFrame> *");


    //List of devices with details. None of it can be edited. connection type, serialbus type, port name, number of buses, status
    connModel = new CANConnectionModel(this);
    ui->tableConnections->setModel(connModel);
    ui->tableConnections->setColumnWidth(0, 100);
    ui->tableConnections->setColumnWidth(1, 100);
    ui->tableConnections->setColumnWidth(2, 130);
    ui->tableConnections->setColumnWidth(3, 70);
    ui->tableConnections->setColumnWidth(4, 200);
    QHeaderView *HorzHdr = ui->tableConnections->horizontalHeader();
    HorzHdr->setStretchLastSection(true); //causes the data column to automatically fill the tableview

    ui->textConsole->setEnabled(false);
    ui->btnClearDebug->setEnabled(false);
    ui->btnSendHex->setEnabled(false);
    ui->btnSendText->setEnabled(false);
    ui->lineSend->setEnabled(false);

    if (settings.value("Main/SaveRestoreConnections", false).toBool())
    {
        /* load connection configuration */
        loadConnections();
    }    

    connect(ui->btnDisconnect, &QPushButton::clicked, this, &ConnectionWindow::handleRemoveConn);
    connect(ui->btnSendHex, &QPushButton::clicked, this, &ConnectionWindow::handleSendHex);
    connect(ui->btnSendText, &QPushButton::clicked, this, &ConnectionWindow::handleSendText);
    connect(ui->ckEnableConsole, &QCheckBox::toggled, this, &ConnectionWindow::consoleEnableChanged);
    connect(ui->btnClearDebug, &QPushButton::clicked, this, &ConnectionWindow::handleClearDebugText);
    connect(ui->btnNewConnection, &QPushButton::clicked, this, &ConnectionWindow::handleNewConn);
    connect(ui->btnResetConn, &QPushButton::clicked, this, &ConnectionWindow::handleResetConn);

    //the health readout ticks on its own rather than on every frame, which would be far too often
    connect(&healthTimer, &QTimer::timeout, this, &ConnectionWindow::updateBusHealth);
    healthTimer.setInterval(1000);
    healthTimer.start();
    connect(ui->tableConnections->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ConnectionWindow::currentRowChanged);
    connect(ui->tabBuses, &QTabBar::currentChanged, this, &ConnectionWindow::currentTabChanged);
    connect(ui->btnSaveBus, &QPushButton::clicked, this, &ConnectionWindow::saveBusSettings);
    connect(ui->btnMoveUp, &QPushButton::clicked, this, &ConnectionWindow::moveConnUp);
    connect(ui->btnMoveDown, &QPushButton::clicked, this, &ConnectionWindow::moveConnDown);
    connect(ui->btnProfileSave, &QPushButton::clicked, this, &ConnectionWindow::saveProfile);
    connect(ui->btnProfileLoad, &QPushButton::clicked, this, &ConnectionWindow::loadProfile);
    connect(ui->btnProfileDelete, &QPushButton::clicked, this, &ConnectionWindow::deleteProfile);
    refreshProfileList();

    ui->cbBusSpeed->addItem("33333");
    ui->cbBusSpeed->addItem("50000");
    ui->cbBusSpeed->addItem("83333");
    ui->cbBusSpeed->addItem("100000");
    ui->cbBusSpeed->addItem("125000");
    ui->cbBusSpeed->addItem("250000");
    ui->cbBusSpeed->addItem("500000");
    ui->cbBusSpeed->addItem("1000000");
    //ui->cbBusSpeed->addItem("75000");
    //ui->cbBusSpeed->addItem("166666");
    //ui->cbBusSpeed->addItem("233333");
    //ui->cbBusSpeed->addItem("400000");

    rxBroadcastGVRET = new QUdpSocket(this);
    //Need to make sure it tries to share the address in case there are
    //multiple instances of SavvyCAN running.
    rxBroadcastGVRET->bind(QHostAddress::AnyIPv4, 17222, QAbstractSocket::ShareAddress);
    connect(rxBroadcastGVRET, &QUdpSocket::readyRead, this, &ConnectionWindow::readPendingDatagrams);

    //Doing the same for socketcand/kayak hosts:
    rxBroadcastKayak = new QUdpSocket(this);
    rxBroadcastKayak->bind(QHostAddress::AnyIPv4, 42000, QAbstractSocket::ShareAddress);
    connect(rxBroadcastKayak, &QUdpSocket::readyRead, this, &ConnectionWindow::readPendingDatagrams);

}


void ConnectionWindow::readPendingDatagrams()
{
    //qDebug() << "Got a UDP frame!";
    while (rxBroadcastGVRET->hasPendingDatagrams()) {
        QNetworkDatagram datagram = rxBroadcastGVRET->receiveDatagram();
        if (!remoteDeviceIPGVRET.contains(datagram.senderAddress().toString()))
        {
            remoteDeviceIPGVRET.append(datagram.senderAddress().toString());
            //qDebug() << "Add new remote IP " << datagram.senderAddress().toString();
        }
    }
    while (rxBroadcastKayak->hasPendingDatagrams()) {
        QNetworkDatagram datagram = rxBroadcastKayak->receiveDatagram();
        //qDebug() << "Broadcast Datagram: " << QString::fromUtf8(datagram.data());
        QXmlStreamReader CANBeaconXml(QString::fromUtf8(datagram.data()));
        QString KayakHost;
        QString KayakBus;
        while(!CANBeaconXml.atEnd() && !CANBeaconXml.hasError())
        {
          CANBeaconXml.readNext();
          if(CANBeaconXml.name() == QString("CANBeacon") && !CANBeaconXml.isEndElement())
                KayakHost.append(CANBeaconXml.attributes().value("name"));

          if(CANBeaconXml.name() == QString("URL"))
                KayakHost.append(" (" + CANBeaconXml.readElementText() + ')');

          //Kayak can theoretically send multiple busses over one ports
          //TODO: implement this case in socketcand.cpp
          if(CANBeaconXml.name() == QString("Bus") && !CANBeaconXml.isEndElement())
                KayakBus.append(CANBeaconXml.attributes().value("name").toUtf8() + ",");

        }
        KayakHost = KayakBus.left(KayakBus.length() - 1) + "@" + KayakHost;

        QVector<QString> connectedPorts;
        if (connModel->rowCount() > 0)
        {
            for (int i = 0; i < connModel->rowCount(); i++)
            {
                CANConnection *var_conn = connModel->getAtIdx(i);
                connectedPorts.append(var_conn->getPort());
            }
        }

        if (connectedPorts.contains(KayakHost))
        {
            remoteDeviceKayak.removeOne(KayakHost);
        }

        if (!remoteDeviceKayak.contains(KayakHost) && !connectedPorts.contains(KayakHost))
        {
            remoteDeviceKayak.append(KayakHost);
            //qDebug() << "Add new remote IP " << datagram.senderAddress().toString();
        }
    }
}
ConnectionWindow::~ConnectionWindow()
{
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();
    CANConnection* conn_p;

    /* save configuration */
    saveConnections();

    /* delete connections */
    while(!conns.isEmpty())
    {
        conn_p = conns.takeFirst();
        conn_p->stop();
        delete conn_p;
    }

    delete ui;
}


void ConnectionWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    qDebug() << "Show connectionwindow";
    installEventFilter(this);
    readSettings();
    ui->tableConnections->selectRow(0);
    currentRowChanged(ui->tableConnections->currentIndex(), ui->tableConnections->currentIndex());
}

void ConnectionWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    removeEventFilter(this);
    writeSettings();
}

bool ConnectionWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key())
        {
        case Qt::Key_F1:
            HelpWindow::getRef()->showHelp("connectionwindow.md");
            break;
        }
        return true;
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
    //return false;
}

void ConnectionWindow::readSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("ConnWindow/WindowSize", QSize(956, 665)).toSize());
        move(Utility::constrainedWindowPos(settings.value("ConnWindow/WindowPos", QPoint(100, 100)).toPoint()));
    }
}

void ConnectionWindow::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("ConnWindow/WindowSize", size());
        settings.setValue("ConnWindow/WindowPos", pos());
    }
}

void ConnectionWindow::consoleEnableChanged(bool checked) {
    ui->textConsole->setEnabled(checked);
    ui->btnClearDebug->setEnabled(checked);
    ui->btnSendHex->setEnabled(checked);
    ui->btnSendText->setEnabled(checked);
    ui->lineSend->setEnabled(checked);

    int selIdx = ui->tableConnections->currentIndex().row();

    if (selIdx == -1)
        return;

    CANConnection* conn_p = connModel->getAtIdx(selIdx);

    if (checked) { //enable console
        connect(conn_p, &CANConnection::debugOutput, this, &ConnectionWindow::getDebugText, Qt::UniqueConnection);
        connect(this, &ConnectionWindow::sendDebugData, conn_p, &CANConnection::debugInput, Qt::UniqueConnection);
    }
    else { //turn it off
        disconnect(conn_p, &CANConnection::debugOutput, nullptr, nullptr);
        disconnect(this, &ConnectionWindow::sendDebugData, conn_p, &CANConnection::debugInput);
    }
}

void ConnectionWindow::handleNewConn()
{
    NewConnectionDialog *thisDialog = new NewConnectionDialog(&remoteDeviceIPGVRET, &remoteDeviceKayak);
    CANCon::type newType;
    QString newPort;
    QString newDriver;
    int newSerialSpeed;
    int newBusSpeed;
    bool newCanFd;
    int newDataRate;
    CANConnection *conn;

    if (thisDialog->exec() == QDialog::Accepted)
    {
        newType = thisDialog->getConnectionType();
        newPort = thisDialog->getPortName();
        newDriver = thisDialog->getDriverName();
        newSerialSpeed = thisDialog->getSerialSpeed();
        newBusSpeed = thisDialog->getBusSpeed();
        newCanFd=thisDialog->isCanFd();
        newDataRate = thisDialog->getDataRate();
        conn = create(newType, newPort, newDriver, newSerialSpeed, newBusSpeed, newCanFd, newDataRate);
        if (conn)
        {
            connModel->add(conn);
            ui->tableConnections->setCurrentIndex(connModel->index(connModel->rowCount() - 1, 1));
        }
    }
    delete thisDialog;
}

void ConnectionWindow::handleRemoveConn()
{
    int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    if (selIdx <0) return;

    qDebug() << "remove connection at index: " << selIdx;

    CANConnection* conn_p = connModel->getAtIdx(selIdx);
    if(!conn_p) return;

    /* remove connection from model & manager */
    connModel->remove(conn_p);

    /* stop and delete connection. deleteLater rather than delete: other parts of the program
     * (frame sender, script windows) can be holding this pointer across a tick, and there may be
     * queued signal deliveries still pointed at the object. Letting the event loop reap it once
     * the stack unwinds turns a use-after-free into a no-op. */
    conn_p->stop();
    conn_p->deleteLater();

    /* select first connection in list */
    ui->tableConnections->selectRow(0);
}

//grab the settings of every bus a connection has so they can be restored onto another one
QList<CANBus> ConnectionWindow::captureBusConfig(CANConnection *conn_p)
{
    QList<CANBus> buses;
    if (!conn_p) return buses;

    for (int i = 0; i < conn_p->getNumBuses(); i++)
    {
        CANBus bus;
        if (conn_p->getBusSettings(i, bus)) buses.append(bus);
        else break; //nothing configured past this point
    }

    return buses;
}

//push a captured set of bus settings back into a connection
void ConnectionWindow::applyBusConfig(CANConnection *conn_p, const QList<CANBus> &buses)
{
    if (!conn_p) return;

    /* Deliberately not bounded by getNumBuses() here. A connection that has only just been started
     * may not have worked out how many buses its hardware has yet, and every driver bound checks the
     * index for itself anyway, so an extra call is harmless. */
    for (int i = 0; i < buses.count(); i++)
    {
        conn_p->setBusSettings(i, buses[i]);
    }
}

void ConnectionWindow::handleResetConn()
{
    QString port, driver;
    CANCon::type type;
    int serSpeed, busSpeed, dataRate;
    bool canFd;

    int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    if (selIdx <0) return;

    qDebug() << "reset connection at index: " << selIdx;

    CANConnection* conn_p = connModel->getAtIdx(selIdx);
    if(!conn_p) return;

    type = conn_p->getType();
    port = conn_p->getPort();
    driver = conn_p->getDriver();
    serSpeed = conn_p->getSerialSpeed();

    /* Take a copy of how the device is set up before we tear it down, otherwise the replacement
     * comes back with nothing but defaults. The constructor only takes the first bus so the rest
     * get pushed back in once the new connection exists. */
    const QList<CANBus> buses = captureBusConfig(conn_p);

    if (buses.count() > 0)
    {
        busSpeed = buses[0].getSpeed();
        canFd = buses[0].isCanFD();
        dataRate = buses[0].getDataRate();
    }
    else
    {
        qDebug() << "reset: the connection had no bus settings to preserve";
        busSpeed = 0;
        dataRate = 0;
        canFd = false;
    }

    /* stop and delete connection */
    conn_p->stop();

    conn_p = nullptr;

    conn_p = create(type, port, driver, serSpeed, busSpeed,canFd,dataRate);
    if (conn_p)
    {
        applyBusConfig(conn_p, buses);
        connModel->replace(selIdx, conn_p);
    }
}

/* status */
void ConnectionWindow::connectionStatus(CANConStatus pStatus)
{
    Q_UNUSED(pStatus);

    qDebug() << "Connectionstatus changed";
    int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    connModel->refresh();
    ui->tableConnections->selectRow(selIdx);
}

void ConnectionWindow::setSuspendAll(bool pSuspend)
{
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    foreach(CANConnection* conn_p, conns)
        conn_p->suspend(pSuspend);

    connModel->refresh();
}

void ConnectionWindow::saveBusSettings()
{
    int selIdx = ui->tableConnections->currentIndex().row();
    int offset = ui->tabBuses->currentIndex();

    /* set parameters */
    if (selIdx == -1) {
        return;
    }
    else
    {
        CANConnection* conn_p = connModel->getAtIdx(selIdx);
        CANBus bus;
        if(!conn_p) return;

        if (!conn_p->getBusSettings(offset, bus))
        {
            qDebug() << "Could not retrieve bus settings!";
            return;
        }

        bus.setSpeed(ui->cbBusSpeed->currentText().toInt());
        bus.setActive(ui->ckEnable->isChecked());
        bus.setListenOnly(ui->ckListenOnly->isChecked());
        bus.setCanFD(ui->canFDEnable->isChecked());
        bus.setDataRate(ui->cbDataRate->currentText().toInt());
        conn_p->setBusSettings(offset, bus);
    }
}

/*
 * Shows how busy the selected bus is. The figures come from the traffic itself rather than from
 * the adapter, so they mean the same thing for every connection type - a socketcand or MQTT link
 * reports load just as a local adapter does.
 */
void ConnectionWindow::updateBusHealth()
{
    const int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    CANConnection *conn_p = connModel->getAtIdx(selIdx);

    //nothing selected, so there is nothing to report on
    if (!conn_p)
    {
        ui->lblHealthLoad->setText("-");
        ui->lblHealthRate->setText("-");
        ui->lblHealthCounts->setText("-");
        ui->lblHealthErrors->setText("-");
        ui->lblHealthErrors->setStyleSheet("");
        return;
    }

    //the tab index is the connection's own bus number, statistics are keyed by global bus number
    const int localBus = qMax(0, ui->tabBuses->currentIndex());
    const int busBase = CANConManager::getInstance()->getBusBase(conn_p);

    /* Default constructed stats are all zero, which is exactly the right answer for a bus that is
     * connected but has not carried a frame yet. Showing dashes there reads as "broken" when the
     * bus is merely quiet. */
    CANBusStats stats;
    if (busBase >= 0) CANConManager::getInstance()->getBusStats(busBase + localBus, stats);

    CANBus bus;
    const bool haveSpeed = conn_p->getBusSettings(localBus, bus) && bus.getSpeed() > 0;

    if (haveSpeed) ui->lblHealthLoad->setText(QString::number(stats.busLoadPercent, 'f', 1) + " %");
    else ui->lblHealthLoad->setText("(needs bus speed)");

    ui->lblHealthRate->setText(QString::number(stats.frameRate));
    ui->lblHealthCounts->setText(QString("%1 / %2").arg(stats.framesReceived).arg(stats.framesSent));

    ui->lblHealthErrors->setText(QString::number(stats.errorFrames));
    //make error frames stand out, they are the thing worth noticing here
    ui->lblHealthErrors->setStyleSheet(stats.errorFrames > 0 ? "color: red; font-weight: bold;" : "");
}

void ConnectionWindow::populateBusDetails(int offset)
{
    int selIdx = ui->tableConnections->currentIndex().row();

    /* set parameters */
    if (selIdx == -1) {
        return;
    }
    else
    {
        //bool ret;
        //int numBuses;
        ui->canFDEnable->setVisible(false);
        ui->canFDEnable_label->setVisible(false);
        ui->dataRate_label->setVisible(false);
        ui->cbDataRate->setVisible(false);
        CANConnection* conn_p = connModel->getAtIdx(selIdx);
        CANBus bus;
        if(!conn_p) return;

        if (!conn_p->getBusSettings(offset, bus))
        {
            qDebug() << "Could not retrieve bus settings!";
            return;
        }

        //int busBase = CANConManager::getInstance()->getBusBase(conn_p);
        //ui->lblBusNum->setText(QString::number(busBase + offset));
        ui->ckListenOnly->setChecked(bus.isListenOnly());
        ui->ckEnable->setChecked(bus.isActive());
        if (conn_p->getType() == CANCon::type::SERIALBUS || conn_p->getType() == CANCon::type::LAWICEL)
        {
            ui->canFDEnable->setVisible(true);
            ui->canFDEnable_label->setVisible(true);
            ui->canFDEnable->setChecked(bus.isCanFD());
            ui->cbDataRate->setVisible(true);
            ui->dataRate_label->setVisible(true);
        }

        bool found = false;
        for (int i = 0; i < ui->cbBusSpeed->count(); i++)
        {
            if (bus.getSpeed() == ui->cbBusSpeed->itemText(i).toInt())
            {
                found = true;
                ui->cbBusSpeed->setCurrentIndex(i);
                break;
            }
        }

        if (!found) ui->cbBusSpeed->addItem(QString::number(bus.getSpeed()));
        found = false;
        for (int i = 0; i < ui->cbDataRate->count(); i++)
        {
            if (bus.getDataRate() == ui->cbDataRate->itemText(i).toInt())
            {
                found = true;
                ui->cbDataRate->setCurrentIndex(i);
                break;
            }
        }
        if (!found) ui->cbDataRate->addItem(QString::number(bus.getDataRate()));
    }
}

void ConnectionWindow::currentTabChanged(int newIdx)
{
    populateBusDetails(newIdx);
}

void ConnectionWindow::currentRowChanged(const QModelIndex &current, const QModelIndex &previous)
{
    int selIdx = current.row();
    CANConnection* prevConn = connModel->getAtIdx(previous.row());
    if(prevConn != nullptr)
        disconnect(prevConn, &CANConnection::debugOutput, nullptr, nullptr);
    disconnect(this, &ConnectionWindow::sendDebugData, nullptr, nullptr);

    /* set parameters */
    if (selIdx == -1) {
        ui->groupBus->setEnabled(false);
        return;
    }
    else
    {
        //bool ret;
        ui->groupBus->setEnabled(true);
        int numBuses;

        CANConnection* conn_p = connModel->getAtIdx(selIdx);
        if(!conn_p) return;

        //because this might have already been setup during the initial setup so tear that one down and then create the normal one.
        //disconnect(conn_p, &CANConnection::debugOutput, 0, 0);

        numBuses = conn_p->getNumBuses();
        int numB = ui->tabBuses->count();
        for (int i = 0; i < numB; i++) ui->tabBuses->removeTab(0);

        int busBase = CANConManager::getInstance()->getBusBase(conn_p);

        /*if (numBuses > 1)*/ for (int i = 0; i < numBuses; i++) ui->tabBuses->addTab(QString::number(busBase + i));

        populateBusDetails(0);
        if (ui->ckEnableConsole->isChecked())
        {
            connect(conn_p, &CANConnection::debugOutput, this, &ConnectionWindow::getDebugText, Qt::UniqueConnection);
            connect(this, &ConnectionWindow::sendDebugData, conn_p, &CANConnection::debugInput, Qt::UniqueConnection);
        }
    }
}

void ConnectionWindow::getDebugText(QString debugText) {
    ui->textConsole->append(debugText);
}

void ConnectionWindow::handleClearDebugText() {
    ui->textConsole->clear();
}

void ConnectionWindow::handleSendHex() {
    QByteArray bytes;
    QStringList tokens = ui->lineSend->text().split(' ');
    foreach (QString token, tokens) {
        bytes.append(token.toInt(nullptr, 16));
    }
    emit sendDebugData(bytes);
}

void ConnectionWindow::handleSendText() {
    QByteArray bytes;
    bytes = ui->lineSend->text().toLatin1();
    bytes.append('\r'); //add carriage return for line ending
    emit sendDebugData(bytes);
}

CANConnection* ConnectionWindow::create(CANCon::type pTye, QString pPortName, QString pDriver, int pSerialSpeed, int pBusSpeed, bool pCanFd, int pDataRate)
{
    CANConnection* conn_p;

    /* create connection */
    conn_p = CanConFactory::create(pTye, pPortName, pDriver, pSerialSpeed, pBusSpeed, pCanFd, pDataRate);
    if(conn_p)
    {
        /* connect signal */
        connect(conn_p, &CANConnection::status, this, &ConnectionWindow::connectionStatus);
        if (ui->ckEnableConsole->isChecked())
        {            
            //set up the debug console to operate if we've selected it. Doing so here allows debugging right away during set up
            connect(conn_p, &CANConnection::debugOutput, this, &ConnectionWindow::getDebugText, Qt::UniqueConnection);
        }
        /*TODO add return value and checks */
        conn_p->start();
    }
    return conn_p;
}


/* Connection settings are stored as plain string lists.
 *
 * They used to be written as QVector<int> / QVector<QString> wrapped in a QVariant. A list of
 * strings is a type QSettings encodes natively, but a list of ints is not - it goes through
 * QVariant's data stream instead, and on Qt 6.8 that comes out as "@Invalid()". Every integer
 * array then read back empty, the length check below threw the whole set away, and the user got
 * no connections restored with nothing in the log to say why. Decimal strings avoid the
 * question entirely and are legible in the ini file. Settings written the old way are still
 * read, so nobody loses a configuration on upgrade. */
static void writeStringVec(QSettings &s, const QString &key, const QVector<QString> &vec)
{
    QStringList out;
    out.reserve(vec.size());
    for (const QString &entry : vec) out.append(entry);
    s.setValue(key, out);
}

static void writeIntVec(QSettings &s, const QString &key, const QVector<int> &vec)
{
    QStringList out;
    out.reserve(vec.size());
    for (int entry : vec) out.append(QString::number(entry));
    s.setValue(key, out);
}

static QVector<QString> readStringVec(QSettings &s, const QString &key)
{
    const QVariant val = s.value(key);
    if (!val.isValid()) return QVector<QString>();

    //a one entry list comes back from the ini as a bare string
    if (val.typeId() == QMetaType::QStringList || val.typeId() == QMetaType::QString)
    {
        const QStringList in = val.toStringList();
        return QVector<QString>(in.begin(), in.end());
    }

    return val.value<QVector<QString>>(); //written by an older build
}

static QVector<int> readIntVec(QSettings &s, const QString &key)
{
    const QVariant val = s.value(key);
    if (!val.isValid()) return QVector<int>();

    if (val.typeId() == QMetaType::QStringList || val.typeId() == QMetaType::QString)
    {
        QVector<int> out;
        const QStringList in = val.toStringList();
        out.reserve(in.size());
        for (const QString &entry : in) out.append(entry.toInt());
        return out;
    }

    return val.value<QVector<int>>(); //written by an older build
}

void ConnectionWindow::loadConnections()
{
    loadConnectionsFromGroup("connections");
}

//returns how many connections were restored, so a profile load can report an empty or broken set
int ConnectionWindow::loadConnectionsFromGroup(const QString &group)
{
#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
    qRegisterMetaTypeStreamOperators<CANBus>();
    qRegisterMetaTypeStreamOperators<QList<CANBus>>();
#endif

    QSettings settings;

    /* fill connection list */
    QVector<QString> portNames = readStringVec(settings, group + "/portNames");
    QVector<QString> driverNames = readStringVec(settings, group + "/driverNames");
    QVector<int>    devTypes = readIntVec(settings, group + "/types");

    QVector<int> busSpeeds = readIntVec(settings, group + "/busSpeeds_0");
    QVector<int> DataRates = readIntVec(settings, group + "/DataRates_0");
    QVector<int> isCanFds = readIntVec(settings, group + "/isCanFds_0");
    QVector<int> serialSpeeds = readIntVec(settings, group + "/serialSpeeds");
    //don't load the connections if the three setting arrays above aren't all the same size.
    if (portNames.size() != driverNames.size() || devTypes.size() != driverNames.size() ||  busSpeeds.size() != driverNames.size() || isCanFds.size() != driverNames.size() ||
	DataRates.size() != driverNames.size() || serialSpeeds.size() != driverNames.size() ) return 0;

    /* Bus 0 is handled through the constructor above for backwards compatibility, the rest of the
     * buses on a multi bus device get pushed in afterwards. Anything a settings file doesn't have
     * simply isn't restored, so an older file still loads. */
    QVector<int> allSpeeds[MAX_SAVED_BUSES];
    QVector<int> allDataRates[MAX_SAVED_BUSES];
    QVector<int> allCanFds[MAX_SAVED_BUSES];
    QVector<int> allListenOnly[MAX_SAVED_BUSES];
    QVector<int> allActive[MAX_SAVED_BUSES];

    for (int busIdx = 0; busIdx < MAX_SAVED_BUSES; busIdx++)
    {
        allSpeeds[busIdx] = readIntVec(settings, QString("%1/busSpeeds_%2").arg(group).arg(busIdx));
        allDataRates[busIdx] = readIntVec(settings, QString("%1/DataRates_%2").arg(group).arg(busIdx));
        allCanFds[busIdx] = readIntVec(settings, QString("%1/isCanFds_%2").arg(group).arg(busIdx));
        allListenOnly[busIdx] = readIntVec(settings, QString("%1/listenOnly_%2").arg(group).arg(busIdx));
        allActive[busIdx] = readIntVec(settings, QString("%1/isActive_%2").arg(group).arg(busIdx));
    }

    for(int i = 0 ; i < portNames.size() ; i++)
    {
      CANConnection* conn_p = create((CANCon::type)devTypes[i], portNames[i], driverNames[i], serialSpeeds[i], busSpeeds[i], isCanFds[i] ? true : false, DataRates[i]);
        if (!conn_p) continue;

        QList<CANBus> buses;
        for (int busIdx = 0; busIdx < MAX_SAVED_BUSES; busIdx++)
        {
            //an array that isn't the right length was never saved for this many connections
            if (allSpeeds[busIdx].size() != portNames.size()) break;

            CANBus bus;
            bus.setSpeed(allSpeeds[busIdx][i]);
            if (allDataRates[busIdx].size() == portNames.size()) bus.setDataRate(allDataRates[busIdx][i]);
            if (allCanFds[busIdx].size() == portNames.size()) bus.setCanFD(allCanFds[busIdx][i] ? true : false);
            if (allListenOnly[busIdx].size() == portNames.size()) bus.setListenOnly(allListenOnly[busIdx][i] ? true : false);
            //settings written before buses had an enabled flag should come back enabled
            if (allActive[busIdx].size() == portNames.size()) bus.setActive(allActive[busIdx][i] ? true : false);
            else bus.setActive(true);

            buses.append(bus);
        }
        applyBusConfig(conn_p, buses);

        /* add connection to model */
        connModel->add(conn_p);
    }

    if (connModel->rowCount() > 0) {
        ui->tableConnections->selectRow(0);
    }

    return portNames.size();
}

void ConnectionWindow::saveConnections()
{
    saveConnectionsToGroup("connections");
}

void ConnectionWindow::saveConnectionsToGroup(const QString &group)
{
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    QSettings settings;
    QVector<QString> portNames;
    QVector<int> devTypes;
    QVector<QString> driverNames;
    QVector<int> serialSpeeds;

    //one array per bus index so a multi bus device keeps the settings of all of its buses
    QVector<int> busSpeeds[MAX_SAVED_BUSES];
    QVector<int> dataRates[MAX_SAVED_BUSES];
    QVector<int> canFds[MAX_SAVED_BUSES];
    QVector<int> listenOnly[MAX_SAVED_BUSES];
    QVector<int> isActive[MAX_SAVED_BUSES];

    /* save connections */
    foreach(CANConnection* conn_p, conns)
    {
        serialSpeeds.append(conn_p->getSerialSpeed());
        portNames.append(conn_p->getPort());
        devTypes.append(conn_p->getType());
        driverNames.append(conn_p->getDriver());

        /* Every array has to end up the same length as the others, the loader throws the whole lot
         * away otherwise. So a bus we can't read still gets a placeholder entry. */
        for (int busIdx = 0; busIdx < MAX_SAVED_BUSES; busIdx++)
        {
            CANBus bus;
            if (busIdx < conn_p->getNumBuses() && conn_p->getBusSettings(busIdx, bus))
            {
                busSpeeds[busIdx].append(bus.getSpeed());
                canFds[busIdx].append(bus.isCanFD() ? 1 : 0);
                dataRates[busIdx].append(bus.getDataRate());
                listenOnly[busIdx].append(bus.isListenOnly() ? 1 : 0);
                isActive[busIdx].append(bus.isActive() ? 1 : 0);
            }
            else
            {
                busSpeeds[busIdx].append(0);
                canFds[busIdx].append(0);
                dataRates[busIdx].append(0);
                listenOnly[busIdx].append(0);
                isActive[busIdx].append(0);
            }
        }
    }

    writeStringVec(settings, group + "/portNames", portNames);
    writeIntVec(settings, group + "/types", devTypes);
    writeStringVec(settings, group + "/driverNames", driverNames);
    writeIntVec(settings, group + "/serialSpeeds", serialSpeeds);

    for (int busIdx = 0; busIdx < MAX_SAVED_BUSES; busIdx++)
    {
        writeIntVec(settings, QString("%1/busSpeeds_%2").arg(group).arg(busIdx), busSpeeds[busIdx]);
        writeIntVec(settings, QString("%1/isCanFds_%2").arg(group).arg(busIdx), canFds[busIdx]);
        writeIntVec(settings, QString("%1/DataRates_%2").arg(group).arg(busIdx), dataRates[busIdx]);
        writeIntVec(settings, QString("%1/listenOnly_%2").arg(group).arg(busIdx), listenOnly[busIdx]);
        writeIntVec(settings, QString("%1/isActive_%2").arg(group).arg(busIdx), isActive[busIdx]);
    }

    settings.sync();
}

/* Named connection profiles. Everything lives under connProfiles/<name>/ in the same layout the
 * automatically restored set uses, so a profile is just another group passed to the two functions
 * above. Profile names are used as a settings group, so '/' and '\' would silently nest a subgroup
 * and are rejected. */
static QString profileGroup(const QString &name)
{
    return QString("connProfiles/%1").arg(name);
}

void ConnectionWindow::refreshProfileList(const QString &selectName)
{
    QSettings settings;
    settings.beginGroup("connProfiles");
    const QStringList names = settings.childGroups();
    settings.endGroup();

    //repopulating fires currentIndexChanged, which must not be mistaken for the user picking one
    const QSignalBlocker blocker(ui->cbConnProfiles);
    ui->cbConnProfiles->clear();
    ui->cbConnProfiles->addItems(names);

    if (!selectName.isEmpty())
    {
        const int idx = ui->cbConnProfiles->findText(selectName);
        if (idx >= 0) ui->cbConnProfiles->setCurrentIndex(idx);
    }

    const bool any = !names.isEmpty();
    ui->btnProfileLoad->setEnabled(any);
    ui->btnProfileDelete->setEnabled(any);
}

void ConnectionWindow::saveProfile()
{
    if (CANConManager::getInstance()->getConnections().isEmpty())
    {
        QMessageBox::information(this, tr("Save Profile"),
                                 tr("There are no connections to save. Add a connection first."));
        return;
    }

    bool ok = false;
    const QString suggested = ui->cbConnProfiles->currentText();
    QString name = QInputDialog::getText(this, tr("Save Profile"), tr("Profile name:"),
                                         QLineEdit::Normal, suggested, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (name.contains('/') || name.contains('\\'))
    {
        QMessageBox::warning(this, tr("Save Profile"), tr("A profile name cannot contain / or \\."));
        return;
    }

    QSettings settings;
    if (settings.childGroups().contains("connProfiles"))
    {
        settings.beginGroup("connProfiles");
        const bool exists = settings.childGroups().contains(name);
        settings.endGroup();
        if (exists && QMessageBox::question(this, tr("Save Profile"),
                                            tr("Profile \"%1\" already exists. Overwrite it?").arg(name),
                                            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    }

    saveConnectionsToGroup(profileGroup(name));
    refreshProfileList(name);
}

void ConnectionWindow::loadProfile()
{
    const QString name = ui->cbConnProfiles->currentText().trimmed();
    if (name.isEmpty()) return;

    if (!CANConManager::getInstance()->getConnections().isEmpty() &&
        QMessageBox::question(this, tr("Load Profile"),
                              tr("This closes the current connections and replaces them with \"%1\". Continue?").arg(name),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    removeAllConnections();

    if (loadConnectionsFromGroup(profileGroup(name)) == 0)
    {
        QMessageBox::warning(this, tr("Load Profile"),
                             tr("Profile \"%1\" restored no connections. It may have been saved by an "
                                "incompatible version.").arg(name));
    }
}

void ConnectionWindow::deleteProfile()
{
    const QString name = ui->cbConnProfiles->currentText().trimmed();
    if (name.isEmpty()) return;

    if (QMessageBox::question(this, tr("Delete Profile"), tr("Delete profile \"%1\"?").arg(name),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QSettings settings;
    settings.beginGroup(profileGroup(name));
    settings.remove(QString());
    settings.endGroup();

    refreshProfileList();
}

//teardown for every live connection, using the same safe path as removing one by hand
void ConnectionWindow::removeAllConnections()
{
    QList<CANConnection*> conns = CANConManager::getInstance()->getConnections();
    foreach (CANConnection *conn_p, conns)
    {
        if (!conn_p) continue;
        connModel->remove(conn_p);
        conn_p->stop();
        conn_p->deleteLater();
    }
}

void ConnectionWindow::moveConnUp()
{
    int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    if (selIdx > 0)
    {
        connModel->swap(selIdx - 1, selIdx);
        ui->tableConnections->selectRow(selIdx - 1);
    }
}

void ConnectionWindow::moveConnDown()
{
    int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    if (selIdx < connModel->rowCount() - 1)
    {
        connModel->swap(selIdx, selIdx + 1);
        ui->tableConnections->selectRow(selIdx + 1);
    }
}
