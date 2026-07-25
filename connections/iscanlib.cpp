#include "iscanlib.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

//every isCAN call hands back zero on success and an error code otherwise
#define ISCAN_OK 0

#define ISCAN_MAX_PER_TICK 100

IscanLib::IscanLib(QString portName, int busSpeed) :
    CANConnection(portName, "ISCAN", CANCon::ISCAN, 0, busSpeed, false, 0, 1, 4000, true),
    isCAN_DeviceInitEx(nullptr), isCAN_ReceiveMessageEx(nullptr), isCAN_TransmitMessageEx(nullptr),
    isCAN_CloseDevice(nullptr), mTimer(this), mChannel(0), mOpen(false),
    mBusSpeed(busSpeed > 0 ? busSpeed : 500000)
{
    bool ok = false;
    const int chan = portName.trimmed().toInt(&ok);
    if (ok && chan >= 0 && chan <= 255) mChannel = (uint8_t)chan;
    else qDebug() << "ISCAN: can't read a channel number out of" << portName << "- using channel 0";
}

IscanLib::~IscanLib()
{
    stop();
}

bool IscanLib::baudrateCode(int speed, uint8_t &code)
{
    switch (speed)
    {
    case 5000:    code = 0; return true;
    case 10000:   code = 1; return true;
    case 20000:   code = 2; return true;
    case 50000:   code = 3; return true;
    case 100000:  code = 4; return true;
    case 125000:  code = 5; return true;
    case 250000:  code = 6; return true;
    case 500000:  code = 7; return true;
    case 800000:  code = 8; return true;
    case 1000000: code = 9; return true;
    default: return false;
    }
}

bool IscanLib::loadLibrary()
{
    if (mLib.isLoaded()) return true;

    mLib.setFileName("iscandrv");
    if (!mLib.load())
    {
        qDebug() << "ISCAN: could not load iscandrv -" << mLib.errorString();
        emit debugOutput("ISCAN: could not load iscandrv. Is the isCAN driver installed?");
        return false;
    }

    isCAN_DeviceInitEx = (fnDeviceInitEx)mLib.resolve("isCAN_DeviceInitEx");
    isCAN_ReceiveMessageEx = (fnReceiveMessageEx)mLib.resolve("isCAN_ReceiveMessageEx");
    isCAN_TransmitMessageEx = (fnTransmitMessageEx)mLib.resolve("isCAN_TransmitMessageEx");
    isCAN_CloseDevice = (fnCloseDevice)mLib.resolve("isCAN_CloseDevice");

    if (!isCAN_DeviceInitEx || !isCAN_ReceiveMessageEx || !isCAN_TransmitMessageEx || !isCAN_CloseDevice)
    {
        qDebug() << "ISCAN: iscandrv is missing entry points we need";
        emit debugOutput("ISCAN: the iscandrv on this system is missing expected functions");
        unloadLibrary();
        return false;
    }

    return true;
}

void IscanLib::unloadLibrary()
{
    isCAN_DeviceInitEx = nullptr;
    isCAN_ReceiveMessageEx = nullptr;
    isCAN_TransmitMessageEx = nullptr;
    isCAN_CloseDevice = nullptr;

    if (mLib.isLoaded()) mLib.unload();
}

bool IscanLib::openChannel()
{
    const CANBus& bus = mBusData[0].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    uint8_t code = 0;
    if (!baudrateCode(speed, code))
    {
        qDebug() << "ISCAN: unsupported bus speed" << speed;
        emit debugOutput(QString("ISCAN: bus speed %1 isn't one the driver accepts").arg(speed));
        return false;
    }

    const uint8_t res = isCAN_DeviceInitEx(mChannel, code);
    if (res != ISCAN_OK)
    {
        qDebug() << "ISCAN: isCAN_DeviceInitEx failed with" << res;
        emit debugOutput(QString("ISCAN: could not initialise channel %1 (error %2)").arg(mChannel).arg(res));
        return false;
    }

    mOpen = true;
    return true;
}

void IscanLib::closeChannel()
{
    if (mOpen && isCAN_CloseDevice) isCAN_CloseDevice(mChannel);
    mOpen = false;
}

void IscanLib::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void IscanLib::piStarted()
{
    mBusData[0].mBus.setSpeed(mBusSpeed);

    if (!loadLibrary() || !openChannel())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setActive(true);

    setStatus(CANCon::CONNECTED);

    //UniqueConnection so a stop/start cycle cannot stack a second delivery per tick
    connect(&mTimer, &QTimer::timeout, this, &IscanLib::handleTick, Qt::UniqueConnection);
    mTimer.setInterval(2);
    mTimer.setSingleShot(false);
    mTimer.start();

    sendStatus();
}

void IscanLib::piStop()
{
    mTimer.stop();
    closeChannel();
    unloadLibrary();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void IscanLib::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    const int oldSpeed = mBusData[0].mBus.getSpeed();
    setBusConfig(0, pBus);

    //the speed is only settable as part of initialising the device, so redo that
    if (mOpen && pBus.getSpeed() != oldSpeed)
    {
        qDebug() << "ISCAN: bus speed changed, reinitialising the channel";
        closeChannel();
        if (!openChannel())
        {
            mTimer.stop();
            setStatus(CANCon::NOT_CONNECTED);
            sendStatus();
        }
    }
}

bool IscanLib::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void IscanLib::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool IscanLib::piSendFrame(const CANFrame& pFrame)
{
    if (!mOpen || !isCAN_TransmitMessageEx) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    iscan_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.message_id = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);
    msg.is_extended = pFrame.hasExtendedFrameFormat() ? 1 : 0;
    msg.remote_req = (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) ? 1 : 0;
    msg.data_len = (uint8_t)len;
    memcpy(msg.data, payload.constData(), len);

    const uint8_t res = isCAN_TransmitMessageEx(mChannel, &msg);
    if (res != ISCAN_OK)
    {
        qDebug() << "ISCAN: isCAN_TransmitMessageEx failed with" << res;
        return false;
    }
    return true;
}

void IscanLib::handleTick()
{
    if (!mOpen || !isCAN_ReceiveMessageEx) return;

    for (int i = 0; i < ISCAN_MAX_PER_TICK; i++)
    {
        iscan_message msg;
        memset(&msg, 0, sizeof(msg));

        /* Anything other than zero means there was nothing to read (or something worse). Either way
         * we're done for this tick - the driver has no separate "queue empty" code we can rely on. */
        if (isCAN_ReceiveMessageEx(mChannel, &msg) != ISCAN_OK) return;

        if (isCapSuspended()) continue;

        CANFrame* frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "ISCAN: can't get a frame, ERROR";
            return;
        }

        int len = msg.data_len;
        if (len > 8) len = 8;

        frame_p->bus = 0;
        frame_p->isReceived = true;
        frame_p->setExtendedFrameFormat(msg.is_extended != 0);
        frame_p->setFrameType(msg.remote_req ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
        frame_p->setFrameId(msg.message_id & (msg.is_extended ? 0x1FFFFFFF : 0x7FF));
        frame_p->setPayload(QByteArray((const char*)msg.data, len));
        //the message struct carries no timestamp so this is the best we can do
        frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));

        checkTargettedFrame(*frame_p);
        getQueue().queue();
    }
}
