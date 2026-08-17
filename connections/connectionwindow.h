#ifndef CONNECTIONWINDOW_H
#define CONNECTIONWINDOW_H



#include <QDialog>
#include <QSerialPortInfo>
#include <QDebug>
#include <QSettings>
#include <QTimer>
#include <QItemSelection>
#include <QCanBusDeviceInfo>
#include <QUdpSocket>
#include "canconnectionmodel.h"
#include "connections/canconnection.h"


class CANConnectionModel;

namespace Ui {
class ConnectionWindow;
}


class ConnectionWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionWindow(QWidget *parent = 0);
    ~ConnectionWindow();

signals:
    void updateBusSettings(CANBus *bus);
    void updatePortName(QString port);
    void sendDebugData(QByteArray bytes);

public slots:
    void getDebugText(QString debugText);
    void setSuspendAll(bool pSuspend);


private slots:
    void currentRowChanged(const QModelIndex &current, const QModelIndex &previous);
    void currentTabChanged(int newIdx);
    void consoleEnableChanged(bool checked);
    void handleRemoveConn();
    void handleNewConn();
    void handleResetConn();
    void handleClearDebugText();
    void handleSendHex();
    void handleSendText();
    void saveBusSettings();
    void moveConnUp();
    void moveConnDown();
    void connectionStatus(CANConStatus);
    void readPendingDatagrams();
    //refreshes the bus health readout for whichever bus is selected
    void updateBusHealth();
    void saveProfile();
    void loadProfile();
    void deleteProfile();

private:
    Ui::ConnectionWindow *ui;    
    QSettings *settings;
    CANConnectionModel *connModel;
    QUdpSocket *rxBroadcastGVRET;
    QUdpSocket *rxBroadcastKayak;
    QVector<QString> remoteDeviceIPGVRET;
    QVector<QString> remoteDeviceKayak;
    QTimer healthTimer;

    CANConnection* create(CANCon::type pTye, QString pPortName, QString pDriver, int pSerialSpeed, int pBusSpeed, bool pCanFd, int pDataRate);
    //read every bus's configuration out of a live connection / push a saved set back into one
    QList<CANBus> captureBusConfig(CANConnection *conn_p);
    void applyBusConfig(CANConnection *conn_p, const QList<CANBus> &buses);
    void populateBusDetails(int offset);
    void loadConnections();
    void saveConnections();
    /* The same on-disk layout is used for the automatically restored set and for every named
     * profile, so both go through these with a different settings group. */
    void saveConnectionsToGroup(const QString &group);
    int loadConnectionsFromGroup(const QString &group);
    void removeAllConnections();
    void refreshProfileList(const QString &selectName = QString());
    void showEvent(QShowEvent *);
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void readSettings();
    void writeSettings();
};

#endif // CONNECTIONWINDOW_H
