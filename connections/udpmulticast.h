#ifndef UDPMULTICAST_H
#define UDPMULTICAST_H

#include <QHostAddress>
#include <QList>
#include <QUdpSocket>

#include "canconnection.h"

/*
 * python-can's "udp_multicast" backend (can.interfaces.udp_multicast).
 *
 * Frames are msgpack maps sent as UDP datagrams to a multicast group, so any number of tools on the
 * machine (or the LAN) can share one virtual bus. python-can defaults to an IPv6 group but the IPv4
 * group is the friendlier default here, so the port name field accepts either:
 *
 *   (empty)                 -> 239.74.163.2:43113
 *   239.74.163.2            -> that group on the default port
 *   239.74.163.2:43113      -> group and port
 *   ff15:7079:...:6d63:6173 -> python-can's IPv6 group (wrap in [] to add a port)
 */
class UDPMulticast : public CANConnection
{
    Q_OBJECT

public:
    UDPMulticast(QString address);
    virtual ~UDPMulticast();

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;

private slots:
    void readPendingDatagrams();

private:
    bool openSocket();
    void closeSocket();
    void parseTarget(const QString &target);
    void handleDatagram(const QByteArray &data);
    void sendStatus();

    QUdpSocket *socket;
    QHostAddress mGroup;
    quint16 mPort;
    //multicast loops back to the local host so we get our own datagrams too. They're byte for byte
    //what we sent, so remembering the last few lets us drop the echoes instead of double counting.
    QList<QByteArray> mRecentSent;
};

#endif // UDPMULTICAST_H
