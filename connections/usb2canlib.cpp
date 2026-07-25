#include "usb2canlib.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

#define CANAL_ERROR_SUCCESS 0

//CanalMsg flags
#define CANAL_IDFLAG_EXTENDED 0x00000001
#define CANAL_IDFLAG_RTR 0x00000002
#define CANAL_IDFLAG_STATUS 0x00000004

#define USB2CAN_MAX_PER_TICK 100

Usb2CanLib::Usb2CanLib(QString portName, int busSpeed) :
    CANConnection(portName, "USB2CAN", CANCon::USB2CAN, 0, busSpeed, false, 0, 1, 4000, true),
    CanalOpen(nullptr), CanalClose(nullptr), CanalSend(nullptr), CanalReceive(nullptr),
    CanalGetStatus(nullptr), mTimer(this), mHandle(0), mOpen(false),
    mBusSpeed(busSpeed > 0 ? busSpeed : 500000), mTimeBasis(0), mHaveTimeBasis(false)
{
}

Usb2CanLib::~Usb2CanLib()
{
    stop();
}

bool Usb2CanLib::loadLibrary()
{
    if (mLib.isLoaded()) return true;

    mLib.setFileName("usb2can");
    if (!mLib.load())
    {
        qDebug() << "USB2CAN: could not load usb2can -" << mLib.errorString();
        emit debugOutput("USB2CAN: could not load usb2can.dll. Is the 8devices driver installed? "
                         "(on Linux use a SerialBus/SocketCAN connection instead)");
        return false;
    }

    CanalOpen = (fnCanalOpen)mLib.resolve("CanalOpen");
    CanalClose = (fnCanalClose)mLib.resolve("CanalClose");
    CanalSend = (fnCanalSend)mLib.resolve("CanalSend");
    CanalReceive = (fnCanalReceive)mLib.resolve("CanalReceive");
    CanalGetStatus = (fnCanalGetStatus)mLib.resolve("CanalGetStatus");

    if (!CanalOpen || !CanalClose || !CanalSend || !CanalReceive)
    {
        qDebug() << "USB2CAN: the CANAL library is missing entry points we need";
        emit debugOutput("USB2CAN: the usb2can.dll on this system is missing expected functions");
        unloadLibrary();
        return false;
    }

    return true;
}

void Usb2CanLib::unloadLibrary()
{
    CanalOpen = nullptr;
    CanalClose = nullptr;
    CanalSend = nullptr;
    CanalReceive = nullptr;
    CanalGetStatus = nullptr;

    if (mLib.isLoaded()) mLib.unload();
}

bool Usb2CanLib::openAdapter()
{
    const CANBus& bus = mBusData[0].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    //CANAL wants the config as "serial;bitrate" and its bitrate is in kbit/s
    const QString config = QString("%1;%2").arg(getPort().trimmed()).arg(speed / 1000);
    const QByteArray configBytes = config.toLatin1();

    qDebug() << "USB2CAN: opening with config" << config;

    mHandle = CanalOpen(configBytes.constData(), 0);
    if (mHandle <= 0)
    {
        qDebug() << "USB2CAN: CanalOpen failed, returned" << mHandle;
        emit debugOutput(QString("USB2CAN: could not open adapter '%1' (CanalOpen returned %2)")
                             .arg(getPort()).arg(mHandle));
        mHandle = 0;
        return false;
    }

    mOpen = true;
    return true;
}

void Usb2CanLib::closeAdapter()
{
    if (mOpen && CanalClose) CanalClose(mHandle);
    mOpen = false;
    mHandle = 0;
}

void Usb2CanLib::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void Usb2CanLib::piStarted()
{
    mHaveTimeBasis = false;

    mBusData[0].mBus.setSpeed(mBusSpeed);

    if (!loadLibrary() || !openAdapter())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setActive(true);

    setStatus(CANCon::CONNECTED);

    //UniqueConnection so a stop/start cycle cannot stack a second delivery per tick
    connect(&mTimer, &QTimer::timeout, this, &Usb2CanLib::handleTick, Qt::UniqueConnection);
    mTimer.setInterval(2);
    mTimer.setSingleShot(false);
    mTimer.start();

    sendStatus();
}

void Usb2CanLib::piStop()
{
    mTimer.stop();
    closeAdapter();
    unloadLibrary();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void Usb2CanLib::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    const int oldSpeed = mBusData[0].mBus.getSpeed();
    setBusConfig(0, pBus);

    //CANAL bakes the bit rate into the config string at open time, so a speed change means
    //closing and reopening the adapter
    if (mOpen && pBus.getSpeed() != oldSpeed)
    {
        qDebug() << "USB2CAN: bus speed changed, reopening the adapter";
        closeAdapter();
        if (!openAdapter())
        {
            mTimer.stop();
            setStatus(CANCon::NOT_CONNECTED);
            sendStatus();
        }
    }
}

bool Usb2CanLib::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void Usb2CanLib::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool Usb2CanLib::piSendFrame(const CANFrame& pFrame)
{
    if (!mOpen || !CanalSend) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    canal_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.flags = 0;
    if (pFrame.hasExtendedFrameFormat()) msg.flags |= CANAL_IDFLAG_EXTENDED;
    if (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) msg.flags |= CANAL_IDFLAG_RTR;
    msg.obid = 0;
    msg.id = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);
    msg.sizeData = (uint8_t)len;
    memcpy(msg.data, payload.constData(), len);
    msg.timestamp = 0;

    const int res = CanalSend(mHandle, &msg);
    if (res != CANAL_ERROR_SUCCESS)
    {
        qDebug() << "USB2CAN: CanalSend failed with" << res;
        return false;
    }
    return true;
}

void Usb2CanLib::handleTick()
{
    if (!mOpen || !CanalReceive) return;

    for (int i = 0; i < USB2CAN_MAX_PER_TICK; i++)
    {
        canal_msg msg;
        memset(&msg, 0, sizeof(msg));

        //CanalReceive is the non blocking read, it errors out as soon as the queue is empty
        if (CanalReceive(mHandle, &msg) != CANAL_ERROR_SUCCESS) return;

        //status frames aren't bus traffic
        if (msg.flags & CANAL_IDFLAG_STATUS) continue;

        if (isCapSuspended()) continue;

        CANFrame* frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "USB2CAN: can't get a frame, ERROR";
            return;
        }

        int len = msg.sizeData;
        if (len > 8) len = 8;

        const bool extended = (msg.flags & CANAL_IDFLAG_EXTENDED) != 0;

        frame_p->bus = 0;
        frame_p->isReceived = true;
        frame_p->setExtendedFrameFormat(extended);
        frame_p->setFrameType((msg.flags & CANAL_IDFLAG_RTR) ? QCanBusFrame::RemoteRequestFrame
                                                             : QCanBusFrame::DataFrame);
        frame_p->setFrameId(msg.id & (extended ? 0x1FFFFFFF : 0x7FF));
        frame_p->setPayload(QByteArray((const char*)msg.data, len));

        if (useSystemTime)
        {
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
        }
        else
        {
            //CANAL timestamps are microseconds from an arbitrary start, anchor the first one
            const qint64 deviceUs = (qint64)msg.timestamp;
            if (!mHaveTimeBasis)
            {
                mTimeBasis = (QDateTime::currentMSecsSinceEpoch() * 1000) - deviceUs;
                mHaveTimeBasis = true;
            }
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds((quint64)(mTimeBasis + deviceUs)));
        }

        checkTargettedFrame(*frame_p);
        getQueue().queue();
    }
}
