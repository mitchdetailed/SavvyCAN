#include "nicanlib.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

//NI-CAN returns 0 for success, negative for an error and positive for a warning
#define NICAN_SUCCESS 0

//configuration attribute ids
#define NC_ATTR_START_ON_OPEN 0x80000006
#define NC_ATTR_BAUD_RATE 0x80000007
#define NC_ATTR_READ_Q_LEN 0x80000013
#define NC_ATTR_WRITE_Q_LEN 0x80000014
#define NC_ATTR_CAN_COMP_STD 0x80010001
#define NC_ATTR_CAN_MASK_STD 0x80010002
#define NC_ATTR_CAN_COMP_XTD 0x80010003
#define NC_ATTR_CAN_MASK_XTD 0x80010004
#define NC_ATTR_LOG_COMM_ERRS 0x8001000A

#define NC_OP_RESET 0x80000003

#define NC_ST_READ_AVAIL 0x00000001

//frame_type values in a received message
#define NC_FRMTYPE_DATA 0
#define NC_FRMTYPE_REMOTE 1
#define NC_FRMTYPE_COMM_ERR 2

//NI-CAN flags an extended ID by setting bit 29 of the arbitration id
#define NC_FL_CAN_ARBID_XTD 0x20000000

#define NICAN_READ_Q_LEN 512
#define NICAN_WRITE_Q_LEN 64

#define NICAN_MAX_PER_TICK 100

NicanLib::NicanLib(QString portName, int busSpeed) :
    CANConnection(portName, "NICAN", CANCon::NICAN, 0, busSpeed, false, 0, 1, 4000, true),
    ncConfig(nullptr), ncOpenObject(nullptr), ncCloseObject(nullptr), ncRead(nullptr),
    ncWrite(nullptr), ncAction(nullptr), ncWaitForState(nullptr), ncStatusToString(nullptr),
    mTimer(this), mHandle(0), mOpen(false),
    mBusSpeed(busSpeed > 0 ? busSpeed : 500000), mTimeBasis(0), mHaveTimeBasis(false)
{
}

NicanLib::~NicanLib()
{
    stop();
}

bool NicanLib::loadLibrary()
{
    if (mLib.isLoaded()) return true;

    mLib.setFileName("nican");
    if (!mLib.load())
    {
        qDebug() << "NICAN: could not load nican -" << mLib.errorString();
        emit debugOutput("NICAN: could not load the NI-CAN driver library. Is NI-CAN installed?");
        return false;
    }

    ncConfig = (fnConfig)mLib.resolve("ncConfig");
    ncOpenObject = (fnOpenObject)mLib.resolve("ncOpenObject");
    ncCloseObject = (fnCloseObject)mLib.resolve("ncCloseObject");
    ncRead = (fnRead)mLib.resolve("ncRead");
    ncWrite = (fnWrite)mLib.resolve("ncWrite");
    ncAction = (fnAction)mLib.resolve("ncAction");
    ncWaitForState = (fnWaitForState)mLib.resolve("ncWaitForState");
    ncStatusToString = (fnStatusToString)mLib.resolve("ncStatusToString");

    if (!ncConfig || !ncOpenObject || !ncCloseObject || !ncRead || !ncWrite || !ncWaitForState)
    {
        qDebug() << "NICAN: the NI-CAN library is missing entry points we need";
        emit debugOutput("NICAN: the NI-CAN library on this system is missing expected functions");
        unloadLibrary();
        return false;
    }

    return true;
}

void NicanLib::unloadLibrary()
{
    ncConfig = nullptr;
    ncOpenObject = nullptr;
    ncCloseObject = nullptr;
    ncRead = nullptr;
    ncWrite = nullptr;
    ncAction = nullptr;
    ncWaitForState = nullptr;
    ncStatusToString = nullptr;

    if (mLib.isLoaded()) mLib.unload();
}

//NI-CAN can turn its own status codes into something readable, use that when it's available
QString NicanLib::statusText(int32_t status)
{
    if (!ncStatusToString) return QString::number(status);

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ncStatusToString(status, sizeof(buffer) - 1, buffer);
    return QString::fromLatin1(buffer).trimmed();
}

bool NicanLib::openObject()
{
    const CANBus& bus = mBusData[0].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    const QByteArray name = getPort().trimmed().toLatin1();

    /* Everything about the interface is set up as a list of attributes handed to ncConfig. The
     * comparators and masks are all left at zero which means "accept every ID". */
    uint32_t attrIds[] = {
        NC_ATTR_START_ON_OPEN,
        NC_ATTR_READ_Q_LEN,
        NC_ATTR_WRITE_Q_LEN,
        NC_ATTR_CAN_COMP_STD,
        NC_ATTR_CAN_MASK_STD,
        NC_ATTR_CAN_COMP_XTD,
        NC_ATTR_CAN_MASK_XTD,
        NC_ATTR_BAUD_RATE,
        NC_ATTR_LOG_COMM_ERRS
    };
    uint32_t attrValues[] = {
        1,                    //come up on the bus as soon as the object opens
        NICAN_READ_Q_LEN,
        NICAN_WRITE_Q_LEN,
        0,
        0,
        0,
        0,
        (uint32_t)speed,
        0                     //don't clutter the queue with bus error frames
    };

    int32_t res = ncConfig((const char*)name.constData(),
                           (uint32_t)(sizeof(attrIds) / sizeof(attrIds[0])), attrIds, attrValues);
    if (res < NICAN_SUCCESS)
    {
        qDebug() << "NICAN: ncConfig failed -" << statusText(res);
        emit debugOutput(QString("NICAN: could not configure %1 - %2").arg(getPort()).arg(statusText(res)));
        return false;
    }

    res = ncOpenObject((const char*)name.constData(), &mHandle);
    if (res < NICAN_SUCCESS)
    {
        qDebug() << "NICAN: ncOpenObject failed -" << statusText(res);
        emit debugOutput(QString("NICAN: could not open %1 - %2").arg(getPort()).arg(statusText(res)));
        return false;
    }

    mOpen = true;
    return true;
}

void NicanLib::closeObject()
{
    if (mOpen)
    {
        if (ncAction) ncAction(mHandle, NC_OP_RESET, 0);
        if (ncCloseObject) ncCloseObject(mHandle);
    }
    mOpen = false;
    mHandle = 0;
}

void NicanLib::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void NicanLib::piStarted()
{
    mHaveTimeBasis = false;
    mBusData[0].mBus.setSpeed(mBusSpeed);

    if (!loadLibrary() || !openObject())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setActive(true);

    setStatus(CANCon::CONNECTED);

    //UniqueConnection so a stop/start cycle cannot stack a second delivery per tick
    connect(&mTimer, &QTimer::timeout, this, &NicanLib::handleTick, Qt::UniqueConnection);
    mTimer.setInterval(2);
    mTimer.setSingleShot(false);
    mTimer.start();

    sendStatus();
}

void NicanLib::piStop()
{
    mTimer.stop();
    closeObject();
    unloadLibrary();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void NicanLib::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    const int oldSpeed = mBusData[0].mBus.getSpeed();
    setBusConfig(0, pBus);

    //the baud rate is a configuration attribute so it can only change while the object is closed
    if (mOpen && pBus.getSpeed() != oldSpeed)
    {
        qDebug() << "NICAN: bus speed changed, reopening the interface";
        closeObject();
        if (!openObject())
        {
            mTimer.stop();
            setStatus(CANCon::NOT_CONNECTED);
            sendStatus();
        }
    }
}

bool NicanLib::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void NicanLib::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool NicanLib::piSendFrame(const CANFrame& pFrame)
{
    if (!mOpen || !ncWrite) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    nican_tx_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.arb_id = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);
    //NI-CAN wants extended IDs marked in the arbitration id itself
    if (pFrame.hasExtendedFrameFormat()) msg.arb_id |= NC_FL_CAN_ARBID_XTD;
    msg.is_remote = (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) ? 1 : 0;
    msg.dlc = (uint8_t)len;
    memcpy(msg.data, payload.constData(), len);

    const int32_t res = ncWrite(mHandle, (uint32_t)sizeof(msg), &msg);
    if (res < NICAN_SUCCESS)
    {
        qDebug() << "NICAN: ncWrite failed -" << statusText(res);
        return false;
    }
    return true;
}

void NicanLib::handleTick()
{
    if (!mOpen || !ncRead || !ncWaitForState) return;

    for (int i = 0; i < NICAN_MAX_PER_TICK; i++)
    {
        //a zero timeout turns this into "is there anything waiting?"
        uint32_t currentState = 0;
        const int32_t waitRes = ncWaitForState(mHandle, NC_ST_READ_AVAIL, 0, &currentState);
        if (waitRes < NICAN_SUCCESS) return; //includes the timeout case, nothing to read
        if ((currentState & NC_ST_READ_AVAIL) == 0) return;

        nican_rx_message msg;
        memset(&msg, 0, sizeof(msg));

        const int32_t res = ncRead(mHandle, (uint32_t)sizeof(msg), &msg);
        if (res < NICAN_SUCCESS)
        {
            qDebug() << "NICAN: ncRead failed -" << statusText(res);
            return;
        }

        //bus error notifications aren't traffic
        if (msg.frame_type == NC_FRMTYPE_COMM_ERR) continue;

        if (isCapSuspended()) continue;

        CANFrame* frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "NICAN: can't get a frame, ERROR";
            return;
        }

        int len = msg.dlc;
        if (len > 8) len = 8;

        const bool extended = (msg.arb_id & NC_FL_CAN_ARBID_XTD) != 0;

        frame_p->bus = 0;
        frame_p->isReceived = true;
        frame_p->setExtendedFrameFormat(extended);
        frame_p->setFrameType((msg.frame_type == NC_FRMTYPE_REMOTE) ? QCanBusFrame::RemoteRequestFrame
                                                                   : QCanBusFrame::DataFrame);
        frame_p->setFrameId(msg.arb_id & (extended ? 0x1FFFFFFF : 0x7FF));
        frame_p->setPayload(QByteArray((const char*)msg.data, len));

        if (useSystemTime)
        {
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
        }
        else
        {
            //NI-CAN timestamps are 100ns ticks of the Windows FILETIME epoch, so convert to
            //microseconds and anchor the first one to our clock rather than trusting the epoch
            const qint64 deviceUs = (qint64)(msg.timestamp / 10);
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
