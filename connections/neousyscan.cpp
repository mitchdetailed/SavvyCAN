#include "neousyscan.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>
#include <QMutex>

//message flags
#define NEOUSYS_CAN_MSG_EXTENDED_ID 0x0004
#define NEOUSYS_CAN_MSG_REMOTE_FRAME 0x0040

//recvConfig flag, only needed if we wanted hardware filtering
#define NEOUSYS_CAN_MSG_USE_ID_FILTER 0x00000008

/*
 * WDT_DIO takes a plain C function pointer with no user data argument, so the callback has to find
 * its way back to us through a file scope pointer. Only one Neousys connection can be registered for
 * a port at a time so this is enough. The mutex keeps a callback that is already in flight from
 * touching the object while we're tearing it down.
 */
static QMutex neousysCbMutex;
static NeousysCan *neousysActive = nullptr;

static void NEOUSYS_CALL neousysRxTrampoline(neousys_can_msg *msgs, unsigned int count)
{
    QMutexLocker locker(&neousysCbMutex);
    if (neousysActive) neousysActive->receivedFromDriver(msgs, count);
}

NeousysCan::NeousysCan(QString portName, int busSpeed) :
    CANConnection(portName, "NEOUSYS", CANCon::NEOUSYS, 0, busSpeed, false, 0, 1, 4000, true),
    CAN_Setup(nullptr), CAN_Start(nullptr), CAN_Stop(nullptr), CAN_Send(nullptr),
    CAN_RegisterReceived(nullptr), CAN_RegisterStatus(nullptr),
    mPort(0), mStarted(false), mBusSpeed(busSpeed > 0 ? busSpeed : 500000)
{
    bool ok = false;
    const int port = portName.trimmed().toInt(&ok);
    if (ok && port >= 0) mPort = (unsigned int)port;
    else qDebug() << "NEOUSYS: can't read a port number out of" << portName << "- using port 0";
}

NeousysCan::~NeousysCan()
{
    stop();
}

bool NeousysCan::loadLibrary()
{
    if (mLib.isLoaded()) return true;

#ifdef Q_OS_WIN
    mLib.setFileName("WDT_DIO");
#else
    mLib.setFileName("wdt_dio");
#endif

    if (!mLib.load())
    {
        qDebug() << "NEOUSYS: could not load WDT_DIO -" << mLib.errorString();
        emit debugOutput("NEOUSYS: could not load WDT_DIO. Is the Neousys driver installed?");
        return false;
    }

    CAN_Setup = (fnCanSetup)mLib.resolve("CAN_Setup");
    CAN_Start = (fnCanStart)mLib.resolve("CAN_Start");
    CAN_Stop = (fnCanStop)mLib.resolve("CAN_Stop");
    CAN_Send = (fnCanSend)mLib.resolve("CAN_Send");
    CAN_RegisterReceived = (fnCanRegisterReceived)mLib.resolve("CAN_RegisterReceived");
    CAN_RegisterStatus = (fnCanRegisterStatus)mLib.resolve("CAN_RegisterStatus");

    if (!CAN_Setup || !CAN_Start || !CAN_Stop || !CAN_Send || !CAN_RegisterReceived)
    {
        qDebug() << "NEOUSYS: WDT_DIO is missing entry points we need";
        emit debugOutput("NEOUSYS: the WDT_DIO on this system is missing expected functions");
        unloadLibrary();
        return false;
    }

    return true;
}

void NeousysCan::unloadLibrary()
{
    CAN_Setup = nullptr;
    CAN_Start = nullptr;
    CAN_Stop = nullptr;
    CAN_Send = nullptr;
    CAN_RegisterReceived = nullptr;
    CAN_RegisterStatus = nullptr;

    if (mLib.isLoaded()) mLib.unload();
}

bool NeousysCan::startPort()
{
    const CANBus& bus = mBusData[0].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    neousys_can_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.bitRate = (uint32_t)speed;
    //no hardware filtering, we want everything
    setup.recvConfig = 0;
    setup.recvId = 0;
    setup.recvMask = 0;

    if (CAN_Setup(mPort, &setup, (unsigned int)sizeof(setup)) == 0)
    {
        qDebug() << "NEOUSYS: CAN_Setup failed";
        emit debugOutput(QString("NEOUSYS: could not set up port %1").arg(mPort));
        return false;
    }

    //register before starting so we don't miss anything
    {
        QMutexLocker locker(&neousysCbMutex);
        neousysActive = this;
    }

    if (CAN_RegisterReceived(mPort, neousysRxTrampoline) == 0)
    {
        qDebug() << "NEOUSYS: CAN_RegisterReceived failed";
        emit debugOutput("NEOUSYS: the driver would not accept our receive callback");
        QMutexLocker locker(&neousysCbMutex);
        neousysActive = nullptr;
        return false;
    }

    if (CAN_Start(mPort) == 0)
    {
        qDebug() << "NEOUSYS: CAN_Start failed";
        emit debugOutput(QString("NEOUSYS: could not start port %1").arg(mPort));
        QMutexLocker locker(&neousysCbMutex);
        neousysActive = nullptr;
        return false;
    }

    mStarted = true;
    return true;
}

void NeousysCan::stopPort()
{
    //unhook ourselves first so a callback already running finishes before we go away
    {
        QMutexLocker locker(&neousysCbMutex);
        if (neousysActive == this) neousysActive = nullptr;
    }

    if (mStarted && CAN_Stop) CAN_Stop(mPort);
    mStarted = false;
}

void NeousysCan::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void NeousysCan::piStarted()
{
    mBusData[0].mBus.setSpeed(mBusSpeed);

    if (!loadLibrary() || !startPort())
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

void NeousysCan::piStop()
{
    stopPort();
    unloadLibrary();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void NeousysCan::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    const int oldSpeed = mBusData[0].mBus.getSpeed();
    setBusConfig(0, pBus);

    //the bit rate is part of the setup call so the port has to be restarted for it to take
    if (mStarted && pBus.getSpeed() != oldSpeed)
    {
        qDebug() << "NEOUSYS: bus speed changed, restarting the port";
        stopPort();
        if (!startPort())
        {
            setStatus(CANCon::NOT_CONNECTED);
            sendStatus();
        }
    }
}

bool NeousysCan::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void NeousysCan::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool NeousysCan::piSendFrame(const CANFrame& pFrame)
{
    if (!mStarted || !CAN_Send) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    neousys_can_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.id = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);
    msg.flags = 0;
    if (pFrame.hasExtendedFrameFormat()) msg.flags |= NEOUSYS_CAN_MSG_EXTENDED_ID;
    if (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) msg.flags |= NEOUSYS_CAN_MSG_REMOTE_FRAME;
    msg.extra = 0;
    msg.len = (uint8_t)len;
    memcpy(msg.data, payload.constData(), len);

    if (CAN_Send(mPort, &msg, (unsigned int)sizeof(msg)) == 0)
    {
        qDebug() << "NEOUSYS: CAN_Send failed";
        return false;
    }
    return true;
}

/*
 * Called on one of the driver's threads. Do the decoding here but hand the finished frames over to
 * the worker thread rather than touching the queue from here.
 */
void NeousysCan::receivedFromDriver(const neousys_can_msg *msgs, unsigned int count)
{
    if (!msgs) return;

    for (unsigned int i = 0; i < count; i++)
    {
        const neousys_can_msg &msg = msgs[i];

        int len = msg.len;
        if (len > 8) len = 8;

        const bool extended = (msg.flags & NEOUSYS_CAN_MSG_EXTENDED_ID) != 0;

        CANFrame frame;
        frame.bus = 0;
        frame.isReceived = true;
        frame.setExtendedFrameFormat(extended);
        frame.setFrameType((msg.flags & NEOUSYS_CAN_MSG_REMOTE_FRAME) ? QCanBusFrame::RemoteRequestFrame
                                                                      : QCanBusFrame::DataFrame);
        frame.setFrameId(msg.id & (extended ? 0x1FFFFFFF : 0x7FF));
        frame.setPayload(QByteArray((const char*)msg.data, len));
        //nothing in the message carries a device timestamp
        frame.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));

        QMetaObject::invokeMethod(this, "handleRxFrame", Qt::QueuedConnection, Q_ARG(CANFrame, frame));
    }
}

void NeousysCan::handleRxFrame(CANFrame frame)
{
    if (isCapSuspended()) return;

    CANFrame* frame_p = getQueue().get();
    if (!frame_p)
    {
        qDebug() << "NEOUSYS: can't get a frame, ERROR";
        return;
    }

    *frame_p = frame;
    checkTargettedFrame(*frame_p);
    getQueue().queue();
}
