#ifndef NEWCONNECTIONDIALOG_H
#define NEWCONNECTIONDIALOG_H

#include <QDialog>
#include <QCanBusDeviceInfo>
#include <QSerialPortInfo>
#include <QDebug>
#include <QUdpSocket>
#include "canconnectionmodel.h"
#include "connections/canconnection.h"

namespace Ui {
class NewConnectionDialog;
}

class NewConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewConnectionDialog(QVector<QString>* gvretips, QVector<QString>* kayakips, QWidget *parent = nullptr);
    ~NewConnectionDialog();

    CANCon::type getConnectionType();
    QString getPortName();
    QString getDriverName();
    int getSerialSpeed();
    int getBusSpeed();
    bool isCanFd();
    int getDataRate();

public slots:
    void handleConnTypeChanged();
    void handleDeviceTypeChanged();
    void handleCreateButton();
    void handleScanDevices();

private:
    Ui::NewConnectionDialog *ui;
    QList<QSerialPortInfo> ports;
    QList<QCanBusDeviceInfo> canDevices;
    QVector<QString>* remoteDeviceIPGVRET;
    QVector<QString>* remoteBusKayak;

    void selectSerial();
    void selectKvaser();
    void selectSocketCan();
    void selectRemote();
    void selectKayak();
    void selectMQTT();
    void selectLawicel();
    void selectCANserver();
    void selectCANlogserver();
    void selectGSUSB();
    void selectSeeed();
    void selectRobotell();
    void selectCanalystII();
    void selectIxxat();
    void selectPythonCan();
    void selectUdpMulticast();
    //fills the port list from a driver's device scan. quiet keeps it silent when nothing turns up.
    bool populateScannedDevices(CANCon::type type, bool quiet);
    void selectUsb2Can();
    void selectIscan();
    void selectNican();
    void selectNeousys();
    //shared setup for the plain "serial port plus a CAN speed" style adapters
    void selectSerialAdapter(bool showBusSpeed, bool showSerialSpeed, int defaultSerialSpeed);
    void populateCANSpeeds();
    void populateSerialSpeeds();
    bool isSerialBusAvailable();
    void setPortName(CANCon::type pType, QString pPortName, QString pDriver);
};

#endif // NEWCONNECTIONDIALOG_H
