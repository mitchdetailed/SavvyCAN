#include "udpmulticast.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>
#include <QNetworkDatagram>
#include <QtEndian>
#include "utils/msgpackcodec.h"

//python-can's defaults
#define PYCAN_MCAST_GROUP_V4 "239.74.163.2"
#define PYCAN_MCAST_PORT 43113
//python-can uses a hop limit of 1 so traffic stays on the local segment
#define PYCAN_MCAST_TTL 1

#define MAX_ECHO_MEMORY 64

/* The msgpack codec now lives in utils/msgpackcodec so it can be unit tested on its own. */

UDPMulticast::UDPMulticast(QString address) :
    CANConnection(address, "UDPMULTICAST", CANCon::UDP_MULTICAST, 0, 0, false, 0, 1, 4000, true),
    socket(nullptr), mGroup(QHostAddress(QStringLiteral(PYCAN_MCAST_GROUP_V4))), mPort(PYCAN_MCAST_PORT)
{
    parseTarget(address);
}

UDPMulticast::~UDPMulticast()
{
    stop();
}

void UDPMulticast::parseTarget(const QString &target)
{
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) return;

    QString addr = trimmed;

    //[v6addr]:port for IPv6, plain addr:port for IPv4
    if (addr.startsWith('['))
    {
        const int close = addr.indexOf(']');
        if (close > 0)
        {
            const QString portPart = addr.mid(close + 1);
            if (portPart.startsWith(':'))
            {
                const int p = portPart.mid(1).toInt();
                if (p > 0 && p < 65536) mPort = (quint16)p;
            }
            addr = addr.mid(1, close - 1);
        }
    }
    else if (addr.count(':') == 1) //a single colon means IPv4 with a port
    {
        const QStringList parts = addr.split(':');
        const int p = parts[1].toInt();
        if (p > 0 && p < 65536) mPort = (quint16)p;
        addr = parts[0];
    }

    QHostAddress parsed(addr);
    if (parsed.isNull()) qDebug() << "UDPMULTICAST: can't make sense of address" << addr << "- using the default group";
    else mGroup = parsed;
}

bool UDPMulticast::openSocket()
{
    socket = new QUdpSocket(this);

    //other tools on this machine bind the same port so sharing is mandatory
    const QHostAddress bindAddr = (mGroup.protocol() == QAbstractSocket::IPv6Protocol)
                                      ? QHostAddress(QHostAddress::AnyIPv6) : QHostAddress(QHostAddress::AnyIPv4);

    if (!socket->bind(bindAddr, mPort, QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint))
    {
        qDebug() << "UDPMULTICAST: could not bind port" << mPort << socket->errorString();
        delete socket;
        socket = nullptr;
        return false;
    }

    if (!socket->joinMulticastGroup(mGroup))
    {
        qDebug() << "UDPMULTICAST: could not join" << mGroup.toString() << socket->errorString();
        delete socket;
        socket = nullptr;
        return false;
    }

    socket->setSocketOption(QAbstractSocket::MulticastTtlOption, PYCAN_MCAST_TTL);
    //loopback stays on so a python-can process on this same machine can see us
    socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);

    connect(socket, &QUdpSocket::readyRead, this, &UDPMulticast::readPendingDatagrams);

    qDebug() << "UDPMULTICAST: joined" << mGroup.toString() << "port" << mPort;
    return true;
}

void UDPMulticast::closeSocket()
{
    if (socket)
    {
        socket->leaveMulticastGroup(mGroup);
        socket->close();
        delete socket;
        socket = nullptr;
    }
    mRecentSent.clear();
}

void UDPMulticast::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void UDPMulticast::piStarted()
{
    if (!openSocket())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setActive(true);

    setStatus(CANCon::CONNECTED);
    sendStatus();
}

void UDPMulticast::piStop()
{
    closeSocket();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void UDPMulticast::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    //there's no hardware here, the bus speed is whatever the participants agree on
    setBusConfig(0, pBus);
}

bool UDPMulticast::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void UDPMulticast::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool UDPMulticast::piSendFrame(const CANFrame& pFrame)
{
    if (!socket) return false;
    if (pFrame.bus != 0) return false;

    QByteArray data = pFrame.payload();
    if (data.length() > 64) data.truncate(64);

    const bool isError = (pFrame.frameId() & 0x20000000) != 0;

    //eleven keys, exactly the arguments python-can's Message constructor takes
    QByteArray out;
    MsgPack::packMapHeader(out, 11);

    MsgPack::packStr(out, "timestamp");
    MsgPack::packDouble(out, (double)QDateTime::currentMSecsSinceEpoch() / 1000.0);

    MsgPack::packStr(out, "arbitration_id");
    MsgPack::packUInt(out, pFrame.frameId() & 0x1FFFFFFF);

    MsgPack::packStr(out, "is_extended_id");
    MsgPack::packBool(out, pFrame.hasExtendedFrameFormat());

    MsgPack::packStr(out, "is_remote_frame");
    MsgPack::packBool(out, pFrame.frameType() == QCanBusFrame::RemoteRequestFrame);

    MsgPack::packStr(out, "is_error_frame");
    MsgPack::packBool(out, isError);

    MsgPack::packStr(out, "channel");
    MsgPack::packNil(out);

    //python-can validates dlc against the payload length so they have to agree
    MsgPack::packStr(out, "dlc");
    MsgPack::packUInt(out, data.length());

    MsgPack::packStr(out, "data");
    MsgPack::packBin(out, data);

    MsgPack::packStr(out, "is_fd");
    MsgPack::packBool(out, false);

    MsgPack::packStr(out, "bitrate_switch");
    MsgPack::packBool(out, false);

    MsgPack::packStr(out, "error_state_indicator");
    MsgPack::packBool(out, false);

    //remember it so we can recognise the copy that loops back to us
    mRecentSent.append(out);
    while (mRecentSent.length() > MAX_ECHO_MEMORY) mRecentSent.removeFirst();

    return (socket->writeDatagram(out, mGroup, mPort) == out.length());
}

void UDPMulticast::readPendingDatagrams()
{
    while (socket && socket->hasPendingDatagrams())
    {
        const QNetworkDatagram datagram = socket->receiveDatagram();
        handleDatagram(datagram.data());
    }
}

void UDPMulticast::handleDatagram(const QByteArray &data)
{
    //our own transmissions come back to us thanks to loopback. They're identical to what we sent
    //(the float timestamp makes each one unique) and sendFrame already queued them.
    const int echoIdx = mRecentSent.indexOf(data);
    if (echoIdx >= 0)
    {
        mRecentSent.removeAt(echoIdx);
        return;
    }

    if (isCapSuspended()) return;

    int pos = 0;
    int count = 0;
    if (!MsgPack::readMapHeader(data, pos, count))
    {
        qDebug() << "UDPMULTICAST: datagram wasn't a msgpack map, ignoring";
        return;
    }

    quint32 id = 0;
    bool extended = false;
    bool remote = false;
    bool isError = false;
    QByteArray payload;
    double timestamp = 0.0;
    bool haveTimestamp = false;

    for (int i = 0; i < count; i++)
    {
        MsgPack::Value key;
        if (!MsgPack::readValue(data, pos, key)) return;

        MsgPack::Value val;
        if (!MsgPack::readValue(data, pos, val)) return;

        if (key.type != MsgPack::Value::Str) continue; //not a key we can act on, value already consumed

        const QByteArray name = key.bytes;
        if (name == "arbitration_id" && val.type == MsgPack::Value::UInt) id = (quint32)val.u;
        else if (name == "is_extended_id" && val.type == MsgPack::Value::Bool) extended = val.b;
        else if (name == "is_remote_frame" && val.type == MsgPack::Value::Bool) remote = val.b;
        else if (name == "is_error_frame" && val.type == MsgPack::Value::Bool) isError = val.b;
        else if (name == "data" && (val.type == MsgPack::Value::Bin || val.type == MsgPack::Value::Str)) payload = val.bytes;
        else if (name == "timestamp")
        {
            if (val.type == MsgPack::Value::Double) { timestamp = val.d; haveTimestamp = true; }
            else if (val.type == MsgPack::Value::UInt) { timestamp = (double)val.u; haveTimestamp = true; }
        }
    }

    CANFrame* frame_p = getQueue().get();
    if (!frame_p)
    {
        qDebug() << "UDPMULTICAST: can't get a frame, ERROR";
        return;
    }

    frame_p->bus = 0;
    frame_p->isReceived = true;
    frame_p->setExtendedFrameFormat(extended);
    frame_p->setFrameType(remote ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
    //SavvyCAN flags an error frame by setting bit 29 of the ID
    frame_p->setFrameId((id & 0x1FFFFFFF) | (isError ? 0x20000000 : 0));
    frame_p->setPayload(payload);

    if (useSystemTime || !haveTimestamp)
        frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
    else
        frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds((quint64)(timestamp * 1000000.0)));

    checkTargettedFrame(*frame_p);
    getQueue().queue();
}
