#include "ixxatvci.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

#define VCI_OK 0

//message types, the low byte of uMsgInfo
#define CAN_MSGTYPE_DATA 0
#define CAN_MSGTYPE_INFO 1
#define CAN_MSGTYPE_ERROR 2
#define CAN_MSGTYPE_STATUS 3

/* uMsgInfo bit layout, straight from the VCI headers:
 *   bits 0-7   message type      bits 16-19 dlc
 *   bit  8     ssm               bit  20    ovr
 *   bit  9     hpm               bit  21    srr
 *   bit  10    edl               bit  22    rtr
 *   bit  11    fdr               bit  23    ext
 *   bit  12    esi               bits 24-31 accept code
 */
#define VCI_INFO_TYPE_MASK 0x000000FFu
#define VCI_INFO_DLC_SHIFT 16
#define VCI_INFO_DLC_MASK 0x000F0000u
#define VCI_INFO_SRR 0x00200000u
#define VCI_INFO_RTR 0x00400000u
#define VCI_INFO_EXT 0x00800000u

//CAN_OPMODE_xxx
#define CAN_OPMODE_STANDARD 0x01
#define CAN_OPMODE_EXTENDED 0x02
#define CAN_OPMODE_ERRFRAME 0x04
#define CAN_OPMODE_LISTONLY 0x08

#define IXXAT_RX_FIFO 1024
#define IXXAT_TX_FIFO 128

#define IXXAT_MAX_PER_TICK 200

IxxatVci::IxxatVci(QString portName, int busSpeed) :
    CANConnection(portName, "IXXAT", CANCon::IXXAT, 0, busSpeed, false, 0, 1, 4000, true),
    vciInitialize(nullptr), vciEnumDeviceOpen(nullptr), vciEnumDeviceNext(nullptr),
    vciEnumDeviceClose(nullptr), vciDeviceOpen(nullptr), vciDeviceClose(nullptr),
    canChannelOpen(nullptr), canChannelInitialize(nullptr), canChannelActivate(nullptr),
    canChannelClose(nullptr), canChannelReadMessage(nullptr), canChannelPostMessage(nullptr),
    canControlOpen(nullptr), canControlInitialize(nullptr), canControlStart(nullptr),
    canControlReset(nullptr), canControlClose(nullptr),
    mTimer(this), mDevice(nullptr), mChannelHandle(nullptr), mControlHandle(nullptr),
    mDeviceIdx(0), mChannel(0), mBusSpeed(busSpeed > 0 ? busSpeed : 500000),
    mTimeBasis(0), mHaveTimeBasis(false)
{
    //port name is "device" or "device:channel"
    const QStringList parts = portName.trimmed().split(':');
    bool ok = false;
    if (parts.size() > 0)
    {
        const int dev = parts[0].toInt(&ok);
        if (ok && dev >= 0) mDeviceIdx = dev;
    }
    if (parts.size() > 1)
    {
        const int chan = parts[1].toInt(&ok);
        if (ok && chan >= 0) mChannel = (uint32_t)chan;
    }
}

IxxatVci::~IxxatVci()
{
    stop();
}

//SJA1000 style BTR pair for a 16MHz controller clock, which is what VCI expects here
bool IxxatVci::bitTiming(int speed, uint8_t &btr0, uint8_t &btr1)
{
    switch (speed)
    {
    case 1000000: btr0 = 0x00; btr1 = 0x14; return true;
    case 800000:  btr0 = 0x00; btr1 = 0x16; return true;
    case 500000:  btr0 = 0x00; btr1 = 0x1C; return true;
    case 250000:  btr0 = 0x01; btr1 = 0x1C; return true;
    case 125000:  btr0 = 0x03; btr1 = 0x1C; return true;
    case 100000:  btr0 = 0x04; btr1 = 0x1C; return true;
    case 50000:   btr0 = 0x09; btr1 = 0x1C; return true;
    case 20000:   btr0 = 0x18; btr1 = 0x1C; return true;
    case 10000:   btr0 = 0x31; btr1 = 0x1C; return true;
    default: return false;
    }
}

/*
 * Runs without a connection object so it loads and unloads the VCI runtime itself.
 *
 * Each device gets probed for how many channels it has by opening a control handle on ascending
 * channel numbers until one refuses. A channel another program is already using will refuse too, so
 * this can under report - it's a convenience list, not the last word.
 */
QList<CANDeviceInfo> IxxatVci::enumerateDevices()
{
    QList<CANDeviceInfo> found;

    QLibrary lib;
    const char *candidates[] = {"vcinpl", "vcinpl2"};
    for (const char *name : candidates)
    {
        lib.setFileName(name);
        if (lib.load()) break;
    }

    if (!lib.isLoaded())
    {
        qDebug() << "IXXAT: the VCI runtime isn't available for a device scan -" << lib.errorString();
        return found;
    }

    fnVciInitialize init = (fnVciInitialize)lib.resolve("vciInitialize");
    fnVciEnumDeviceOpen enumOpen = (fnVciEnumDeviceOpen)lib.resolve("vciEnumDeviceOpen");
    fnVciEnumDeviceNext enumNext = (fnVciEnumDeviceNext)lib.resolve("vciEnumDeviceNext");
    fnVciEnumDeviceClose enumClose = (fnVciEnumDeviceClose)lib.resolve("vciEnumDeviceClose");
    fnVciDeviceOpen devOpen = (fnVciDeviceOpen)lib.resolve("vciDeviceOpen");
    fnVciDeviceClose devClose = (fnVciDeviceClose)lib.resolve("vciDeviceClose");
    fnCanControlOpen ctrlOpen = (fnCanControlOpen)lib.resolve("canControlOpen");
    fnCanControlClose ctrlClose = (fnCanControlClose)lib.resolve("canControlClose");

    if (init && enumOpen && enumNext && enumClose && init() == VCI_OK)
    {
        void *hEnum = nullptr;
        if (enumOpen(&hEnum) == VCI_OK)
        {
            for (int idx = 0; ; idx++)
            {
                vci_device_info info;
                memset(&info, 0, sizeof(info));
                if (enumNext(hEnum, &info) != VCI_OK) break;

                QString descr = QString::fromLatin1(info.Description).trimmed();
                if (descr.isEmpty()) descr = "IXXAT device";

                int channels = 0;
                if (devOpen && devClose && ctrlOpen && ctrlClose)
                {
                    void *dev = nullptr;
                    if (devOpen(&info.VciObjectId, &dev) == VCI_OK)
                    {
                        for (uint32_t chan = 0; chan < 4; chan++)
                        {
                            void *ctrl = nullptr;
                            if (ctrlOpen(dev, chan, &ctrl) != VCI_OK) break;
                            ctrlClose(ctrl);
                            channels++;
                        }
                        devClose(dev);
                    }
                }
                //if the probe told us nothing, still offer the first channel
                if (channels < 1) channels = 1;

                for (int chan = 0; chan < channels; chan++)
                {
                    found.append(CANDeviceInfo(QString("%1:%2").arg(idx).arg(chan),
                                               QString("%1 (device %2, channel %3)").arg(descr).arg(idx).arg(chan)));
                }
            }
            enumClose(hEnum);
        }
    }

    lib.unload();
    return found;
}

bool IxxatVci::loadLibrary()
{
    if (mLib.isLoaded()) return true;

    //vcinpl is the classic API. VCI4 installs ship it alongside the CAN-FD flavour.
    const char *candidates[] = {"vcinpl", "vcinpl2"};
    for (const char *name : candidates)
    {
        mLib.setFileName(name);
        if (mLib.load()) break;
    }

    if (!mLib.isLoaded())
    {
        qDebug() << "IXXAT: could not load the VCI runtime -" << mLib.errorString();
        emit debugOutput("IXXAT: could not load vcinpl. Is the IXXAT VCI driver installed?");
        return false;
    }

    vciInitialize = (fnVciInitialize)mLib.resolve("vciInitialize");
    vciEnumDeviceOpen = (fnVciEnumDeviceOpen)mLib.resolve("vciEnumDeviceOpen");
    vciEnumDeviceNext = (fnVciEnumDeviceNext)mLib.resolve("vciEnumDeviceNext");
    vciEnumDeviceClose = (fnVciEnumDeviceClose)mLib.resolve("vciEnumDeviceClose");
    vciDeviceOpen = (fnVciDeviceOpen)mLib.resolve("vciDeviceOpen");
    vciDeviceClose = (fnVciDeviceClose)mLib.resolve("vciDeviceClose");
    canChannelOpen = (fnCanChannelOpen)mLib.resolve("canChannelOpen");
    canChannelInitialize = (fnCanChannelInitialize)mLib.resolve("canChannelInitialize");
    canChannelActivate = (fnCanChannelActivate)mLib.resolve("canChannelActivate");
    canChannelClose = (fnCanChannelClose)mLib.resolve("canChannelClose");
    canChannelReadMessage = (fnCanChannelReadMessage)mLib.resolve("canChannelReadMessage");
    canChannelPostMessage = (fnCanChannelPostMessage)mLib.resolve("canChannelPostMessage");
    canControlOpen = (fnCanControlOpen)mLib.resolve("canControlOpen");
    canControlInitialize = (fnCanControlInitialize)mLib.resolve("canControlInitialize");
    canControlStart = (fnCanControlStart)mLib.resolve("canControlStart");
    canControlReset = (fnCanControlReset)mLib.resolve("canControlReset");
    canControlClose = (fnCanControlClose)mLib.resolve("canControlClose");

    if (!vciInitialize || !vciEnumDeviceOpen || !vciEnumDeviceNext || !vciEnumDeviceClose ||
        !vciDeviceOpen || !vciDeviceClose || !canChannelOpen || !canChannelInitialize ||
        !canChannelActivate || !canChannelClose || !canChannelReadMessage || !canChannelPostMessage ||
        !canControlOpen || !canControlInitialize || !canControlStart)
    {
        qDebug() << "IXXAT: the VCI runtime is missing entry points we need";
        emit debugOutput("IXXAT: the VCI runtime found on this system is missing expected functions");
        unloadLibrary();
        return false;
    }

    const int32_t res = vciInitialize();
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: vciInitialize failed with" << Qt::hex << res;
        emit debugOutput(QString("IXXAT: vciInitialize failed (0x%1)").arg((uint32_t)res, 0, 16));
        unloadLibrary();
        return false;
    }

    return true;
}

void IxxatVci::unloadLibrary()
{
    vciInitialize = nullptr;
    vciEnumDeviceOpen = nullptr;
    vciEnumDeviceNext = nullptr;
    vciEnumDeviceClose = nullptr;
    vciDeviceOpen = nullptr;
    vciDeviceClose = nullptr;
    canChannelOpen = nullptr;
    canChannelInitialize = nullptr;
    canChannelActivate = nullptr;
    canChannelClose = nullptr;
    canChannelReadMessage = nullptr;
    canChannelPostMessage = nullptr;
    canControlOpen = nullptr;
    canControlInitialize = nullptr;
    canControlStart = nullptr;
    canControlReset = nullptr;
    canControlClose = nullptr;

    if (mLib.isLoaded()) mLib.unload();
}

bool IxxatVci::openDevice()
{
    //walk the device list until we reach the index we were asked for
    void *hEnum = nullptr;
    int32_t res = vciEnumDeviceOpen(&hEnum);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: vciEnumDeviceOpen failed with" << Qt::hex << res;
        return false;
    }

    vci_device_info info;
    bool found = false;
    for (int idx = 0; ; idx++)
    {
        memset(&info, 0, sizeof(info));
        res = vciEnumDeviceNext(hEnum, &info);
        if (res != VCI_OK) break; //ran off the end of the list

        if (idx == mDeviceIdx)
        {
            found = true;
            qDebug() << "IXXAT: using device" << idx << info.Description << "by" << info.Manufacturer;
            break;
        }
    }
    vciEnumDeviceClose(hEnum);

    if (!found)
    {
        emit debugOutput(QString("IXXAT: no device at index %1").arg(mDeviceIdx));
        return false;
    }

    res = vciDeviceOpen(&info.VciObjectId, &mDevice);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: vciDeviceOpen failed with" << Qt::hex << res;
        emit debugOutput(QString("IXXAT: could not open the device (0x%1)").arg((uint32_t)res, 0, 16));
        mDevice = nullptr;
        return false;
    }

    //the message channel carries traffic, the control handle configures the line
    res = canChannelOpen(mDevice, mChannel, 0, &mChannelHandle);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canChannelOpen failed with" << Qt::hex << res;
        emit debugOutput(QString("IXXAT: could not open channel %1 (0x%2)").arg(mChannel).arg((uint32_t)res, 0, 16));
        closeDevice();
        return false;
    }

    res = canChannelInitialize(mChannelHandle, IXXAT_RX_FIFO, 1, IXXAT_TX_FIFO, 1);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canChannelInitialize failed with" << Qt::hex << res;
        closeDevice();
        return false;
    }

    res = canChannelActivate(mChannelHandle, 1);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canChannelActivate failed with" << Qt::hex << res;
        closeDevice();
        return false;
    }

    res = canControlOpen(mDevice, mChannel, &mControlHandle);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canControlOpen failed with" << Qt::hex << res;
        mControlHandle = nullptr;
        closeDevice();
        return false;
    }

    return true;
}

void IxxatVci::closeDevice()
{
    if (mControlHandle)
    {
        if (canControlReset) canControlReset(mControlHandle);
        if (canControlClose) canControlClose(mControlHandle);
        mControlHandle = nullptr;
    }

    if (mChannelHandle)
    {
        if (canChannelActivate) canChannelActivate(mChannelHandle, 0);
        if (canChannelClose) canChannelClose(mChannelHandle);
        mChannelHandle = nullptr;
    }

    if (mDevice)
    {
        if (vciDeviceClose) vciDeviceClose(mDevice);
        mDevice = nullptr;
    }
}

bool IxxatVci::applyBusSettings()
{
    if (!mControlHandle) return false;

    const CANBus& bus = mBusData[0].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    uint8_t btr0 = 0, btr1 = 0;
    if (!bitTiming(speed, btr0, btr1))
    {
        qDebug() << "IXXAT: unsupported bus speed" << speed;
        emit debugOutput(QString("IXXAT: bus speed %1 isn't one this driver knows how to set").arg(speed));
        return false;
    }

    //accept both ID widths plus error frames, and honour listen only
    uint8_t mode = CAN_OPMODE_STANDARD | CAN_OPMODE_EXTENDED | CAN_OPMODE_ERRFRAME;
    if (bus.isListenOnly()) mode |= CAN_OPMODE_LISTONLY;

    int32_t res = canControlInitialize(mControlHandle, mode, btr0, btr1);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canControlInitialize failed with" << Qt::hex << res;
        emit debugOutput(QString("IXXAT: could not configure the line (0x%1)").arg((uint32_t)res, 0, 16));
        return false;
    }

    res = canControlStart(mControlHandle, bus.isActive() ? 1 : 0);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canControlStart failed with" << Qt::hex << res;
        return false;
    }

    return true;
}

void IxxatVci::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void IxxatVci::piStarted()
{
    mHaveTimeBasis = false;

    if (!loadLibrary() || !openDevice())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setSpeed(mBusSpeed);
    mBusData[0].mBus.setActive(true);
    applyBusSettings();

    setStatus(CANCon::CONNECTED);

    //UniqueConnection so a stop/start cycle cannot stack a second delivery per tick
    connect(&mTimer, &QTimer::timeout, this, &IxxatVci::handleTick, Qt::UniqueConnection);
    mTimer.setInterval(2);
    mTimer.setSingleShot(false);
    mTimer.start();

    sendStatus();
}

void IxxatVci::piStop()
{
    mTimer.stop();

    if (mControlHandle && canControlStart) canControlStart(mControlHandle, 0);

    closeDevice();
    unloadLibrary();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void IxxatVci::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    setBusConfig(0, pBus);
    applyBusSettings();
}

bool IxxatVci::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void IxxatVci::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool IxxatVci::piSendFrame(const CANFrame& pFrame)
{
    if (!mChannelHandle || !canChannelPostMessage) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    vci_canmsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.dwTime = 0;
    msg.dwMsgId = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);

    uint32_t info = CAN_MSGTYPE_DATA;
    info |= ((uint32_t)len << VCI_INFO_DLC_SHIFT) & VCI_INFO_DLC_MASK;
    if (pFrame.hasExtendedFrameFormat()) info |= VCI_INFO_EXT;
    if (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) info |= VCI_INFO_RTR;
    msg.uMsgInfo = info;

    memcpy(msg.abData, payload.constData(), len);

    const int32_t res = canChannelPostMessage(mChannelHandle, &msg);
    if (res != VCI_OK)
    {
        qDebug() << "IXXAT: canChannelPostMessage failed with" << Qt::hex << res;
        return false;
    }
    return true;
}

void IxxatVci::handleTick()
{
    if (!mChannelHandle || !canChannelReadMessage) return;

    for (int i = 0; i < IXXAT_MAX_PER_TICK; i++)
    {
        vci_canmsg msg;
        memset(&msg, 0, sizeof(msg));

        //a zero timeout makes this a poll, it returns an error as soon as the fifo is empty
        const int32_t res = canChannelReadMessage(mChannelHandle, 0, &msg);
        if (res != VCI_OK) return;

        const uint32_t type = msg.uMsgInfo & VCI_INFO_TYPE_MASK;
        //status and info messages aren't bus traffic, only data and error frames interest us
        if (type != CAN_MSGTYPE_DATA && type != CAN_MSGTYPE_ERROR) continue;

        if (isCapSuspended()) continue;

        CANFrame* frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "IXXAT: can't get a frame, ERROR";
            return;
        }

        int len = (int)((msg.uMsgInfo & VCI_INFO_DLC_MASK) >> VCI_INFO_DLC_SHIFT);
        if (len > 8) len = 8;

        const bool extended = (msg.uMsgInfo & VCI_INFO_EXT) != 0;

        const bool isError = (type == CAN_MSGTYPE_ERROR);

        frame_p->bus = 0;
        frame_p->isReceived = true;
        frame_p->setExtendedFrameFormat(extended);
        //flag the type too, that is what makes the frame list render it as an error
        if (isError) frame_p->setFrameType(QCanBusFrame::ErrorFrame);
        else frame_p->setFrameType((msg.uMsgInfo & VCI_INFO_RTR) ? QCanBusFrame::RemoteRequestFrame
                                                                 : QCanBusFrame::DataFrame);
        //SavvyCAN marks an error frame by setting bit 29 of the ID
        frame_p->setFrameId((msg.dwMsgId & 0x1FFFFFFF) | (isError ? 0x20000000 : 0));
        frame_p->setPayload(QByteArray((const char*)msg.abData, len));

        if (useSystemTime)
        {
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
        }
        else
        {
            /* dwTime counts in units of the controller's timestamp divisor which we'd have to read
             * out of canControlGetCaps to interpret. Rather than guess, anchor the counter to the
             * wall clock and treat the ticks as microseconds - close enough for ordering, and the
             * Main/TimeClock setting gives exact wall time if that matters. */
            const qint64 deviceUs = (qint64)msg.dwTime;
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
