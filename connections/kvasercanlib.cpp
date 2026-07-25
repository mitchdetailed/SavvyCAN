#include "kvasercanlib.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

//canStatus values we care about
#define canOK 0
#define canERR_NOMSG (-2)

//canOPEN_xxx flags
#define canOPEN_EXCLUSIVE 0x0008
#define canOPEN_ACCEPT_VIRTUAL 0x0020
#define canOPEN_ACCEPT_LARGE_DLC 0x0200
#define canOPEN_CAN_FD 0x0400

//canFDMSG_xxx flags, only meaningful on a channel opened for CAN FD
#define canFDMSG_FDF 0x010000
#define canFDMSG_BRS 0x020000
#define canFDMSG_ESI 0x040000

/* CANlib's CAN FD data phase presets. The trailing number is the sample point, so 80P means the
 * sample point sits at 80% of the bit - the usual choice for the data phase. */
#define canFD_BITRATE_500K_80P (-1000)
#define canFD_BITRATE_1M_80P (-1001)
#define canFD_BITRATE_2M_80P (-1002)
#define canFD_BITRATE_4M_80P (-1003)
#define canFD_BITRATE_8M_60P (-1004)

//canMSG_xxx flags
#define canMSG_RTR 0x0001
#define canMSG_STD 0x0002
#define canMSG_EXT 0x0004
#define canMSG_ERROR_FRAME 0x0020
#define canMSG_TXACK 0x0040
#define canMSG_TXRQ 0x0080

//canDRIVER_xxx output control
#define canDRIVER_SILENT 1
#define canDRIVER_NORMAL 4

//canBITRATE_xxx presets, passed to canSetBusParams in place of a real frequency
#define canBITRATE_1M (-1)
#define canBITRATE_500K (-2)
#define canBITRATE_250K (-3)
#define canBITRATE_125K (-4)
#define canBITRATE_100K (-5)
#define canBITRATE_62K (-6)
#define canBITRATE_50K (-7)
#define canBITRATE_83K (-8)
#define canBITRATE_10K (-9)

#define canCHANNELDATA_DEVDESCR_ASCII 26

//how many frames to drain per tick so a busy bus can't lock up the worker thread
#define KVASER_MAX_PER_TICK 100

KvaserCanlib::KvaserCanlib(QString portName, int busSpeed, bool canFd, int dataRate) :
    CANConnection(portName, "KVASER", CANCon::KVASER, 0, busSpeed, canFd, dataRate, 1, 4000, true),
    canInitializeLibrary(nullptr), canUnloadLibrary(nullptr), canGetNumberOfChannels(nullptr),
    canOpenChannel(nullptr), canClose(nullptr), canSetBusParams(nullptr), canSetBusParamsFd(nullptr),
    canSetBusOutputControl(nullptr), canBusOn(nullptr), canBusOff(nullptr), canWrite(nullptr),
    canRead(nullptr), canGetChannelData(nullptr),
    mTimer(this), mHandle(-1), mChannel(0),
    mBusSpeed(busSpeed > 0 ? busSpeed : 500000), mFdEnabled(false), mTimeBasis(0), mHaveTimeBasis(false)
{
    //the port name carries the CANlib channel number
    bool ok = false;
    const int chan = portName.trimmed().toInt(&ok);
    if (ok && chan >= 0) mChannel = chan;
    else qDebug() << "KVASER: can't read a channel number out of" << portName << "- using channel 0";
}

KvaserCanlib::~KvaserCanlib()
{
    stop();
}

//CANlib has presets for the common rates and they pick better sample points than we would
long KvaserCanlib::bitrateConstant(int speed)
{
    switch (speed)
    {
    case 1000000: return canBITRATE_1M;
    case 500000:  return canBITRATE_500K;
    case 250000:  return canBITRATE_250K;
    case 125000:  return canBITRATE_125K;
    case 100000:  return canBITRATE_100K;
    case 83000:
    case 83333:   return canBITRATE_83K;
    case 62000:
    case 62500:   return canBITRATE_62K;
    case 50000:   return canBITRATE_50K;
    case 10000:   return canBITRATE_10K;
    default: return 0; //0 means "no preset", fall back to explicit timing
    }
}

//the data phase of a CAN FD frame has its own presets, separate from the arbitration phase above
long KvaserCanlib::fdDataRateConstant(int dataRate)
{
    switch (dataRate)
    {
    case 500000:  return canFD_BITRATE_500K_80P;
    case 1000000: return canFD_BITRATE_1M_80P;
    case 2000000: return canFD_BITRATE_2M_80P;
    case 4000000: return canFD_BITRATE_4M_80P;
    case 8000000: return canFD_BITRATE_8M_60P;
    default: return 0; //no preset for this rate
    }
}

/*
 * Runs without a connection object, so it loads CANlib itself and puts it back afterwards. If the
 * driver isn't installed this simply comes back empty, which is a useful answer in itself.
 */
QList<CANDeviceInfo> KvaserCanlib::enumerateDevices()
{
    QList<CANDeviceInfo> found;

    QLibrary lib;
#ifdef Q_OS_WIN
    lib.setFileName("canlib32");
#else
    lib.setFileName("canlib");
#endif

    if (!lib.load())
    {
        qDebug() << "KVASER: CANlib isn't available for a device scan -" << lib.errorString();
        return found;
    }

    fnInitializeLibrary initLib = (fnInitializeLibrary)lib.resolve("canInitializeLibrary");
    fnGetNumberOfChannels getCount = (fnGetNumberOfChannels)lib.resolve("canGetNumberOfChannels");
    fnGetChannelData getData = (fnGetChannelData)lib.resolve("canGetChannelData");
    fnUnloadLibrary unloadLib = (fnUnloadLibrary)lib.resolve("canUnloadLibrary");

    if (initLib && getCount)
    {
        initLib();

        int channelCount = 0;
        if (getCount(&channelCount) == canOK && channelCount > 0)
        {
            for (int i = 0; i < channelCount; i++)
            {
                QString descr = "Kvaser channel";
                if (getData)
                {
                    char buffer[128];
                    memset(buffer, 0, sizeof(buffer));
                    if (getData(i, canCHANNELDATA_DEVDESCR_ASCII, buffer, sizeof(buffer) - 1) == canOK)
                    {
                        const QString str = QString::fromLatin1(buffer).trimmed();
                        if (!str.isEmpty()) descr = str;
                    }
                }

                found.append(CANDeviceInfo(QString::number(i), QString("%1 (channel %2)").arg(descr).arg(i)));
            }
        }

        if (unloadLib) unloadLib();
    }

    lib.unload();
    return found;
}

bool KvaserCanlib::loadLibrary()
{
    if (mLib.isLoaded()) return true;

#ifdef Q_OS_WIN
    mLib.setFileName("canlib32");
#else
    mLib.setFileName("canlib");
#endif

    if (!mLib.load())
    {
        qDebug() << "KVASER: could not load CANlib -" << mLib.errorString();
        emit debugOutput("KVASER: could not load CANlib. Is the Kvaser driver package installed?");
        return false;
    }

    canInitializeLibrary = (fnInitializeLibrary)mLib.resolve("canInitializeLibrary");
    canUnloadLibrary = (fnUnloadLibrary)mLib.resolve("canUnloadLibrary");
    canGetNumberOfChannels = (fnGetNumberOfChannels)mLib.resolve("canGetNumberOfChannels");
    canOpenChannel = (fnOpenChannel)mLib.resolve("canOpenChannel");
    canClose = (fnClose)mLib.resolve("canClose");
    canSetBusParams = (fnSetBusParams)mLib.resolve("canSetBusParams");
    //optional: only present in CANlib builds with CAN FD support
    canSetBusParamsFd = (fnSetBusParamsFd)mLib.resolve("canSetBusParamsFd");
    canSetBusOutputControl = (fnSetBusOutputControl)mLib.resolve("canSetBusOutputControl");
    canBusOn = (fnBusOn)mLib.resolve("canBusOn");
    canBusOff = (fnBusOff)mLib.resolve("canBusOff");
    canWrite = (fnWrite)mLib.resolve("canWrite");
    canRead = (fnRead)mLib.resolve("canRead");
    canGetChannelData = (fnGetChannelData)mLib.resolve("canGetChannelData");

    //everything except the optional extras has to be there or we can't work
    if (!canInitializeLibrary || !canOpenChannel || !canClose || !canSetBusParams ||
        !canSetBusOutputControl || !canBusOn || !canBusOff || !canWrite || !canRead)
    {
        qDebug() << "KVASER: CANlib loaded but is missing entry points we need";
        emit debugOutput("KVASER: the CANlib found on this system is missing expected functions");
        unloadLibrary();
        return false;
    }

    canInitializeLibrary();
    return true;
}

void KvaserCanlib::unloadLibrary()
{
    if (canUnloadLibrary) canUnloadLibrary();

    canInitializeLibrary = nullptr;
    canUnloadLibrary = nullptr;
    canGetNumberOfChannels = nullptr;
    canOpenChannel = nullptr;
    canClose = nullptr;
    canSetBusParams = nullptr;
    canSetBusParamsFd = nullptr;
    canSetBusOutputControl = nullptr;
    canBusOn = nullptr;
    canBusOff = nullptr;
    canWrite = nullptr;
    canRead = nullptr;
    canGetChannelData = nullptr;

    if (mLib.isLoaded()) mLib.unload();
}

bool KvaserCanlib::openChannel()
{
    int channelCount = 0;
    if (canGetNumberOfChannels && canGetNumberOfChannels(&channelCount) == canOK)
    {
        qDebug() << "KVASER: CANlib reports" << channelCount << "channels";
        if (channelCount <= 0)
        {
            emit debugOutput("KVASER: CANlib found no channels");
            return false;
        }
        if (mChannel >= channelCount)
        {
            emit debugOutput(QString("KVASER: channel %1 doesn't exist, there are %2").arg(mChannel).arg(channelCount));
            return false;
        }
    }

    if (canGetChannelData)
    {
        char descr[128];
        memset(descr, 0, sizeof(descr));
        if (canGetChannelData(mChannel, canCHANNELDATA_DEVDESCR_ASCII, descr, sizeof(descr) - 1) == canOK)
            qDebug() << "KVASER: channel" << mChannel << "is" << descr;
    }

    //accept virtual channels so Kvaser's own virtual bus can be used for testing
    int openFlags = canOPEN_EXCLUSIVE | canOPEN_ACCEPT_VIRTUAL | canOPEN_ACCEPT_LARGE_DLC;

    /* CAN FD has to be asked for when the channel is opened - it cannot be turned on later. If the
     * CANlib on this machine has no FD support we fall back to classic rather than failing, since
     * an FD adapter still talks to a classic bus. */
    mFdEnabled = mBusData[0].mBus.isCanFD();
    if (mFdEnabled && !canSetBusParamsFd)
    {
        qDebug() << "KVASER: this CANlib has no CAN FD support, opening the channel as classic CAN";
        emit debugOutput("KVASER: the installed CANlib does not support CAN FD, using classic CAN");
        mFdEnabled = false;
    }
    if (mFdEnabled) openFlags |= canOPEN_CAN_FD;

    mHandle = canOpenChannel(mChannel, openFlags);
    if (mHandle < 0)
    {
        qDebug() << "KVASER: canOpenChannel failed with" << mHandle;
        emit debugOutput(QString("KVASER: could not open channel %1 (error %2)").arg(mChannel).arg(mHandle));
        return false;
    }

    return true;
}

void KvaserCanlib::closeChannel()
{
    if (mHandle >= 0)
    {
        if (canBusOff) canBusOff(mHandle);
        if (canClose) canClose(mHandle);
        mHandle = -1;
    }
}

bool KvaserCanlib::applyBusSettings()
{
    if (mHandle < 0) return false;

    const CANBus& bus = mBusData[0].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    //taking the bus down first is required before the timing can be changed
    canBusOff(mHandle);

    int res;
    const long preset = bitrateConstant(speed);
    if (preset != 0)
    {
        res = canSetBusParams(mHandle, preset, 0, 0, 0, 0, 0);
    }
    else
    {
        //no preset for this rate, hand over an explicit 16 quanta setup
        qDebug() << "KVASER: no CANlib preset for" << speed << "- using explicit timing";
        res = canSetBusParams(mHandle, speed, 13, 2, 1, 1, 0);
    }

    if (res != canOK)
    {
        qDebug() << "KVASER: canSetBusParams failed with" << res;
        emit debugOutput(QString("KVASER: could not set bus speed %1 (error %2)").arg(speed).arg(res));
        return false;
    }

    /* On an FD channel the data phase has its own timing, set separately from the arbitration
     * phase above. Without this the channel stays at the arbitration rate for the whole frame. */
    if (mFdEnabled && canSetBusParamsFd)
    {
        const int dataRate = bus.getDataRate();
        const long fdPreset = fdDataRateConstant(dataRate);
        if (fdPreset != 0)
        {
            res = canSetBusParamsFd(mHandle, fdPreset, 0, 0, 0);
            if (res != canOK)
            {
                qDebug() << "KVASER: canSetBusParamsFd failed with" << res;
                emit debugOutput(QString("KVASER: could not set the CAN FD data rate %1 (error %2)")
                                     .arg(dataRate).arg(res));
            }
        }
        else
        {
            qDebug() << "KVASER: no CAN FD preset for data rate" << dataRate;
            emit debugOutput(QString("KVASER: %1 is not a CAN FD data rate this driver knows, "
                                     "the data phase is unchanged").arg(dataRate));
        }
    }

    res = canSetBusOutputControl(mHandle, bus.isListenOnly() ? canDRIVER_SILENT : canDRIVER_NORMAL);
    if (res != canOK) qDebug() << "KVASER: canSetBusOutputControl failed with" << res;

    if (!bus.isActive()) return true;

    res = canBusOn(mHandle);
    if (res != canOK)
    {
        qDebug() << "KVASER: canBusOn failed with" << res;
        emit debugOutput(QString("KVASER: could not go on bus (error %1)").arg(res));
        return false;
    }

    return true;
}

void KvaserCanlib::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void KvaserCanlib::piStarted()
{
    mHaveTimeBasis = false;

    if (!loadLibrary() || !openChannel())
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
    connect(&mTimer, &QTimer::timeout, this, &KvaserCanlib::handleTick, Qt::UniqueConnection);
    mTimer.setInterval(2);
    mTimer.setSingleShot(false);
    mTimer.start();

    sendStatus();
}

void KvaserCanlib::piStop()
{
    mTimer.stop();
    closeChannel();
    unloadLibrary();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void KvaserCanlib::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    setBusConfig(0, pBus);
    applyBusSettings();
}

bool KvaserCanlib::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void KvaserCanlib::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool KvaserCanlib::piSendFrame(const CANFrame& pFrame)
{
    if (mHandle < 0 || !canWrite) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    QByteArray payload = pFrame.payload();
    int len = payload.length();
    //classic CAN tops out at 8 bytes, CAN FD at 64
    const int maxLen = mFdEnabled ? 64 : 8;
    if (len > maxLen) len = maxLen;

    unsigned int flags = pFrame.hasExtendedFrameFormat() ? canMSG_EXT : canMSG_STD;
    if (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) flags |= canMSG_RTR;

    if (mFdEnabled && pFrame.hasFlexibleDataRateFormat())
    {
        flags |= canFDMSG_FDF;
        //the bit rate switch is what actually makes the data phase run faster
        if (pFrame.hasBitrateSwitch()) flags |= canFDMSG_BRS;
    }

    uint8_t data[64];
    memset(data, 0, sizeof(data));
    memcpy(data, payload.constData(), len);

    const int res = canWrite(mHandle, (long)(pFrame.frameId() & 0x1FFFFFFF), data, (unsigned int)len, flags);
    if (res != canOK)
    {
        qDebug() << "KVASER: canWrite failed with" << res;
        return false;
    }
    return true;
}

void KvaserCanlib::handleTick()
{
    if (mHandle < 0 || !canRead) return;

    for (int i = 0; i < KVASER_MAX_PER_TICK; i++)
    {
        long id = 0;
        unsigned int dlc = 0;
        unsigned int flags = 0;
        unsigned long timestamp = 0;
        uint8_t data[64];
        memset(data, 0, sizeof(data));

        const int res = canRead(mHandle, &id, data, &dlc, &flags, &timestamp);
        if (res == canERR_NOMSG) return; //nothing left waiting
        if (res != canOK)
        {
            qDebug() << "KVASER: canRead failed with" << res;
            return;
        }

        //a transmit acknowledgement is our own frame coming back, sendFrame already queued it
        if (flags & (canMSG_TXACK | canMSG_TXRQ)) continue;

        if (isCapSuspended()) continue;

        CANFrame* frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "KVASER: can't get a frame, ERROR";
            return;
        }

        const bool isFd = (flags & canFDMSG_FDF) != 0;

        int len = (int)dlc;
        //an FD frame can carry up to 64 bytes, a classic one 8
        const int maxLen = isFd ? 64 : 8;
        if (len > maxLen) len = maxLen;

        const bool isError = (flags & canMSG_ERROR_FRAME) != 0;

        frame_p->bus = 0;
        frame_p->isReceived = true;
        frame_p->setExtendedFrameFormat((flags & canMSG_EXT) != 0);
        /* Flag the frame type as well as the ID bit. The frame list decodes both, and marking the
         * type is what makes it render as an error rather than as odd looking data. */
        if (isError) frame_p->setFrameType(QCanBusFrame::ErrorFrame);
        else frame_p->setFrameType((flags & canMSG_RTR) ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
        //SavvyCAN marks an error frame by setting bit 29 of the ID
        frame_p->setFrameId((((uint32_t)id) & 0x1FFFFFFF) | (isError ? 0x20000000 : 0));
        if (isFd)
        {
            frame_p->setFlexibleDataRateFormat(true);
            frame_p->setBitrateSwitch((flags & canFDMSG_BRS) != 0);
            frame_p->setErrorStateIndicator((flags & canFDMSG_ESI) != 0);
        }
        frame_p->setPayload(QByteArray((const char*)data, len));

        if (useSystemTime)
        {
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
        }
        else
        {
            //CANlib hands out milliseconds by default, anchor the first one to the wall clock
            if (!mHaveTimeBasis)
            {
                mTimeBasis = QDateTime::currentMSecsSinceEpoch() - (qint64)timestamp;
                mHaveTimeBasis = true;
            }
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds((quint64)((mTimeBasis + (qint64)timestamp) * 1000)));
        }

        checkTargettedFrame(*frame_p);
        getQueue().queue();
    }
}
