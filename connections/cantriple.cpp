#include <QDateTime>
#include <QDebug>
#include <QSerialPortInfo>
#include <QtEndian>

#include "cantriple.h"

//how long to wait before trying to open a port that wasn't there, or came unplugged
#define CT_REOPEN_MS        2000
//how long to wait for INFO before asking again; a device that is still booting just gets asked again
#define CT_HELLO_RETRY_MS   500
//how long the bus configs plus START get before the session is begun over again
#define CT_START_TIMEOUT_MS 1500
//silence from a streaming device for this long means the link is gone. Comfortably more than the
//STATUS every PING draws back, comfortably less than the device's own host timeout.
#define CT_LINK_TIMEOUT_MS  2000

static const char *stateName(uint8_t state)
{
    switch (state)
    {
    case CTSC_STATE_OFF:     return "Off";
    case CTSC_STATE_ACTIVE:  return "Error Active";
    case CTSC_STATE_WARNING: return "Error Warning";
    case CTSC_STATE_PASSIVE: return "Error Passive";
    case CTSC_STATE_BUS_OFF: return "Bus Off";
    default:                 return "Unknown";
    }
}

static const char *ackStatusName(uint8_t status)
{
    switch (status)
    {
    case CTSC_OK:              return "ok";
    case CTSC_ERR_BAD_BUS:     return "no such bus";
    case CTSC_ERR_UNSUPPORTED: return "not supported by this model";
    case CTSC_ERR_BAD_BITRATE: return "bit rate can't be reached";
    case CTSC_ERR_BAD_PACKET:  return "malformed request";
    case CTSC_ERR_BUSY:        return "device busy";
    default:                   return "unknown error";
    }
}

CANTriple::CANTriple(QString portName, int busSpeed, bool canFd, int dataRate) :
    CANConnection(portName, "CANTriple", CANCon::CANTRIPLE, 0, busSpeed, canFd, dataRate, CTSC_NUM_BUSES, 16384, true),
    mSerial(nullptr),
    mPortOpen(false),
    mTick(nullptr),
    mLink(Link::Closed),
    mRxOverflow(false),
    mRetries(0),
    mHaveInfo(false),
    mHaveDeviceTime(false),
    mLastDeviceTime(0),
    mDeviceTimeExt(0),
    mTimeBasis(0),
    mQueueDrops(0)
{
    memset(&mInfo, 0, sizeof(mInfo));

    /* Every bus starts out known but switched off. Bus 0 picks up whatever the factory was handed;
     * the new connection dialog and a restored connection both push their own settings in on top of
     * this straight afterwards. Having all three configured from the start is what lets Bus Details
     * show and edit buses 1 and 2 before the device has ever been reached. */
    for (int i = 0; i < CTSC_NUM_BUSES; i++)
    {
        CANBus bus = mBusData[0].mBus;
        if (busSpeed <= 0) bus.setSpeed(500000);
        if (i > 0) bus.setCanFD(false);
        bus.setActive(false);
        setBusConfig(i, bus);
    }
}

CANTriple::~CANTriple()
{
    stop();
}

void CANTriple::sendDebug(const QString &text)
{
    qDebug() << "CANTriple:" << text;
    emit debugOutput(text);
}

/*
 * The V1 carries its own ST-LINK, so it shows up as an ST-LINK virtual COM port. The V2 runs USB
 * itself and enumerates as ST's VCP ID with its MCU UID as the serial number. Neither is unique to
 * a CAN Triple, which is fine - INFO settles it once the port is open - but it is enough to put
 * the likely ports at the top of the list with a useful name.
 */
QList<CANDeviceInfo> CANTriple::enumerateDevices()
{
    QList<CANDeviceInfo> found;

    foreach (const QSerialPortInfo &port, QSerialPortInfo::availablePorts())
    {
        if (!port.hasVendorIdentifier() || port.vendorIdentifier() != 0x0483 || !port.hasProductIdentifier())
            continue;

        const quint16 pid = port.productIdentifier();
        QString what;

        if (pid == 0x5740) what = "CAN Triple 2.x (USB)";
        //ST-LINK/V2-1 and the V3 family, all of which carry a VCP
        else if (pid == 0x374B || pid == 0x374E || pid == 0x374F || pid == 0x3752 ||
                 pid == 0x3753 || pid == 0x3754 || pid == 0x3757)
            what = "CAN Triple 1.x (ST-LINK VCP)";
        else continue;

        QString desc = QString("%1 - %2").arg(port.portName(), what);
        if (!port.serialNumber().isEmpty()) desc += QString(" [%1]").arg(port.serialNumber());
        found.append(CANDeviceInfo(port.portName(), desc));
    }

    return found;
}

/***********************************************************************************************/
/*                                         lifecycle                                           */
/***********************************************************************************************/

void CANTriple::piStarted()
{
    //both live in the worker thread, which is where piStarted runs
    mTick = new QTimer(this);
    mTick->setInterval(100);
    connect(mTick, &QTimer::timeout, this, &CANTriple::handleTick);
    mTick->start();

    mSinceStep.start();
    if (openPort()) sendHello();
}

void CANTriple::piStop()
{
    if (mTick)
    {
        mTick->stop();
        delete mTick;
        mTick = nullptr;
    }

    //be polite: a device left streaming would otherwise carry on until its host timeout
    if (mPortOpen && mLink != Link::Closed)
    {
        sendPacket(CTSC_STOP, QByteArray());
        if (mSerial) mSerial->waitForBytesWritten(100);
    }

    closePort();
}

bool CANTriple::openPort()
{
    closePort();

    if (!transportOpen())
    {
        mLink = Link::Closed;
        mSinceStep.restart();
        return false;
    }

    mPortOpen = true;
    mRxEncoded.clear();
    mRxOverflow = false;
    return true;
}

void CANTriple::closePort()
{
    if (mPortOpen) transportClose();
    mPortOpen = false;

    mLink = Link::Closed;
    mSinceStep.restart();
    setStatus(CANCon::NOT_CONNECTED);
}

bool CANTriple::transportOpen()
{
    mSerial = new QSerialPort(this);
    mSerial->setPortName(getPort());
    //only the V1's UART cares about this; the V2's CDC port ignores it
    mSerial->setBaudRate(CTSC_V1_BAUD);
    mSerial->setDataBits(QSerialPort::Data8);
    mSerial->setParity(QSerialPort::NoParity);
    mSerial->setStopBits(QSerialPort::OneStop);
    mSerial->setFlowControl(QSerialPort::NoFlowControl);

    if (!mSerial->open(QIODevice::ReadWrite))
    {
        sendDebug(QString("Could not open %1: %2").arg(getPort(), mSerial->errorString()));
        delete mSerial;
        mSerial = nullptr;
        return false;
    }

    //some CDC stacks hold data back until the host raises DTR
    mSerial->setDataTerminalReady(true);
    mSerial->clear();

    connect(mSerial, &QSerialPort::readyRead, this, &CANTriple::readSerialData);
    connect(mSerial, &QSerialPort::errorOccurred, this, &CANTriple::serialError);

    sendDebug(QString("Opened %1").arg(getPort()));
    return true;
}

void CANTriple::transportClose()
{
    if (!mSerial) return;

    disconnect(mSerial, nullptr, this, nullptr);
    if (mSerial->isOpen()) mSerial->close();
    //later, because this can be reached from inside one of the port's own signals
    mSerial->deleteLater();
    mSerial = nullptr;
}

void CANTriple::transportWrite(const QByteArray &bytes)
{
    if (mSerial && mSerial->isOpen()) mSerial->write(bytes);
}

void CANTriple::serialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError) return;

    //a resource error is how an unplugged device shows up; anything else is worth reporting but not fatal
    sendDebug(QString("Serial error %1: %2").arg(err).arg(mSerial ? mSerial->errorString() : QString()));
    if (err == QSerialPort::ResourceError || err == QSerialPort::PermissionError)
    {
        closePort();
        sendDebug("Lost the device, will keep trying to reopen it");
    }
}

void CANTriple::sendHello()
{
    QByteArray payload;
    payload.append((char)CTSC_PROTOCOL_VERSION);
    mLink = Link::Hello;
    mSinceStep.restart();
    sendPacket(CTSC_HELLO, payload);
}

//INFO is in: push every bus's settings down and start the stream
void CANTriple::beginSession()
{
    for (int i = 0; i < CTSC_NUM_BUSES; i++) sendBusConfig(i);

    mHaveDeviceTime = false;
    QByteArray payload;
    payload.append((char)0); //no tx echo, CANConnection::sendFrame already logs what we send
    sendPacket(CTSC_START, payload);

    mLink = Link::Starting;
    mSinceStep.restart();
}

void CANTriple::handleTick()
{
    switch (mLink)
    {
    case Link::Closed:
        if (mSinceStep.elapsed() >= CT_REOPEN_MS && openPort()) sendHello();
        break;

    case Link::Hello:
        if (mSinceStep.elapsed() >= CT_HELLO_RETRY_MS)
        {
            //say so once in a while rather than on every retry
            if ((++mRetries % 10) == 0) sendDebug("No answer yet - is the SavvyCAN firmware loaded on this CAN Triple?");
            sendHello();
        }
        break;

    case Link::Starting:
        if (mSinceStep.elapsed() >= CT_START_TIMEOUT_MS)
        {
            sendDebug("Device did not confirm START, starting the session over");
            sendHello();
        }
        break;

    case Link::Streaming:
        if (mSinceRx.elapsed() >= CT_LINK_TIMEOUT_MS)
        {
            sendDebug("Device went quiet, reconnecting");
            setStatus(CANCon::NOT_CONNECTED);
            sendHello();
            break;
        }
        if (mSincePing.elapsed() >= CTSC_PING_INTERVAL_MS)
        {
            mSincePing.restart();
            sendPacket(CTSC_PING, QByteArray());
        }
        break;
    }
}

/***********************************************************************************************/
/*                                         bus settings                                        */
/***********************************************************************************************/

bool CANTriple::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void CANTriple::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx < 0 || pBusIdx >= getNumBuses()) return;

    setBusConfig(pBusIdx, pBus);

    //once a session is underway the change goes straight down; otherwise beginSession sends it
    if (mLink == Link::Starting || mLink == Link::Streaming) sendBusConfig(pBusIdx);
}

void CANTriple::sendBusConfig(int bus)
{
    CANBus cfg;
    if (!getBusConfig(bus, cfg)) return;

    const bool deviceHasFd = mHaveInfo && (qFromLittleEndian(mInfo.caps) & CTSC_CAP_FD);
    bool fd = cfg.isCanFD();
    if (fd && !deviceHasFd)
    {
        sendDebug(QString("Bus %1: this CAN Triple has no CAN FD, running it as classic CAN").arg(bus));
        fd = false;
    }

    ctsc_bus_config_t c;
    memset(&c, 0, sizeof(c));
    c.bus = (uint8_t)bus;
    if (!cfg.isActive()) c.mode = CTSC_MODE_OFF;
    else if (cfg.isListenOnly()) c.mode = CTSC_MODE_LISTEN_ONLY;
    else c.mode = CTSC_MODE_NORMAL;
    if (fd) c.flags |= CTSC_CFG_FD | CTSC_CFG_BRS;
    if (cfg.isTermination()) c.flags |= CTSC_CFG_TERMINATION;
    c.nominal_bps = qToLittleEndian((uint32_t)qMax(0, cfg.getSpeed()));
    c.data_bps = qToLittleEndian((uint32_t)(fd ? qMax(0, cfg.getDataRate()) : 0));

    sendPacket(CTSC_BUS_CONFIG, QByteArray((const char*)&c, sizeof(c)));
}

void CANTriple::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

/***********************************************************************************************/
/*                                         transmit                                            */
/***********************************************************************************************/

bool CANTriple::buildTxRecord(const CANFrame &frame, QByteArray &out)
{
    if (frame.bus < 0 || frame.bus >= CTSC_NUM_BUSES) return false;
    //an error frame is something a bus reports, not something to send
    if (frame.frameId() & 0x20000000) return false;

    CANBus cfg;
    if (!getBusConfig(frame.bus, cfg) || !cfg.isActive() || cfg.isListenOnly()) return false;

    const bool fd = frame.hasFlexibleDataRateFormat();
    if (fd && (!cfg.isCanFD() || !mHaveInfo || !(qFromLittleEndian(mInfo.caps) & CTSC_CAP_FD))) return false;

    const bool rtr = frame.frameType() == QCanBusFrame::RemoteRequestFrame;
    const QByteArray data = frame.payload();
    if (data.size() > (fd ? 64 : 8)) return false;

    uint8_t hdr = (uint8_t)(frame.bus & CTSC_HDR_BUS_MASK);
    const bool ext = frame.hasExtendedFrameFormat();
    if (ext) hdr |= CTSC_HDR_EXT;
    if (fd) hdr |= CTSC_HDR_FDF;
    if (fd && frame.hasBitrateSwitch()) hdr |= CTSC_HDR_BRS;
    if (rtr && !fd) hdr |= CTSC_HDR_RTR;

    const uint8_t dlc = ctsc_len_to_dlc((uint8_t)data.size());
    out.append((char)hdr);
    out.append((char)dlc);

    if (ext)
    {
        const uint32_t id = qToLittleEndian((uint32_t)(frame.frameId() & 0x1FFFFFFF));
        out.append((const char*)&id, 4);
    }
    else
    {
        const uint16_t id = qToLittleEndian((uint16_t)(frame.frameId() & 0x7FF));
        out.append((const char*)&id, 2);
    }

    if (!(hdr & CTSC_HDR_RTR))
    {
        //an FD length between the DLC steps gets padded up to the next one
        QByteArray padded = data;
        padded.append(QByteArray(ctsc_dlc_to_len(dlc) - data.size(), (char)0));
        out.append(padded);
    }
    return true;
}

bool CANTriple::piSendFrame(const CANFrame& pFrame)
{
    if (mLink != Link::Streaming) return false;

    QByteArray rec;
    if (!buildTxRecord(pFrame, rec)) return false;
    sendPacket(CTSC_TX, rec);
    return true;
}

//several frames at once share packets, which matters for playback at any real rate
bool CANTriple::piSendFrames(const QList<CANFrame>& pFrames)
{
    if (mLink != Link::Streaming) return false;

    //room for sync, type and crc inside one decoded packet
    const int maxPayload = CTSC_MAX_PACKET - 4;
    QByteArray batch;
    bool allOk = true;

    foreach (const CANFrame &frame, pFrames)
    {
        QByteArray rec;
        if (!buildTxRecord(frame, rec))
        {
            allOk = false;
            continue;
        }
        if (batch.size() + rec.size() > maxPayload)
        {
            sendPacket(CTSC_TX, batch);
            batch.clear();
        }
        batch.append(rec);
    }

    if (!batch.isEmpty()) sendPacket(CTSC_TX, batch);
    return allOk;
}

void CANTriple::sendPacket(uint8_t type, const QByteArray &payload)
{
    if (!mPortOpen) return;

    QByteArray raw;
    raw.reserve(payload.size() + 4);
    raw.append((char)CTSC_SYNC);
    raw.append((char)type);
    raw.append(payload);
    const uint16_t crc = ctsc_crc16((const uint8_t*)raw.constData(), raw.size());
    raw.append((char)(crc >> 8));
    raw.append((char)(crc & 0xFF));

    QByteArray encoded(raw.size() + raw.size() / 254 + 3, Qt::Uninitialized);
    size_t n = ctsc_cobs_encode((const uint8_t*)raw.constData(), raw.size(), (uint8_t*)encoded.data());
    encoded[(int)n] = 0;
    encoded.truncate((int)n + 1);

    transportWrite(encoded);
}

/***********************************************************************************************/
/*                                         receive                                             */
/***********************************************************************************************/

void CANTriple::readSerialData()
{
    if (mSerial) feedBytes(mSerial->readAll());
}

void CANTriple::feedBytes(const QByteArray &data)
{
    if (!mPortOpen) return;

    //the largest valid encoded packet: COBS adds one byte per 254 plus its leading code byte
    const int maxEncoded = CTSC_MAX_PACKET + CTSC_MAX_PACKET / 254 + 2;
    uint8_t decoded[CTSC_MAX_PACKET + 8];

    for (int i = 0; i < data.size(); i++)
    {
        const char c = data[i];
        if (c != 0)
        {
            if (mRxOverflow) continue;
            if (mRxEncoded.size() >= maxEncoded)
            {
                //runaway garbage, drop it and resynchronise on the next delimiter
                mRxOverflow = true;
                mRxEncoded.clear();
                continue;
            }
            mRxEncoded.append(c);
            continue;
        }

        //0x00 ends a packet
        if (!mRxOverflow && !mRxEncoded.isEmpty())
        {
            const size_t len = ctsc_cobs_decode((const uint8_t*)mRxEncoded.constData(), mRxEncoded.size(), decoded);
            if (len >= 4) processPacket(decoded, len);
        }
        mRxEncoded.clear();
        mRxOverflow = false;
    }
}

void CANTriple::processPacket(const uint8_t *pkt, size_t len)
{
    //a Device Manager packet means the normal firmware is on there, not ours
    if (pkt[0] == CTSC_MANAGER_SYNC)
    {
        if ((++mRetries % 10) == 1)
            sendDebug("This CAN Triple is running its normal firmware. Load the SavvyCAN .ctf with the Device Manager to use it here.");
        return;
    }
    if (pkt[0] != CTSC_SYNC) return;

    const uint16_t crc = ((uint16_t)pkt[len - 2] << 8) | pkt[len - 1];
    if (ctsc_crc16(pkt, len - 2) != crc)
    {
        sendDebug("Dropped a packet with a bad CRC");
        return;
    }

    mSinceRx.restart();

    const uint8_t type = pkt[1];
    const uint8_t *payload = pkt + 2;
    const size_t plen = len - 4;

    switch (type)
    {
    case CTSC_INFO:   handleInfo(payload, plen); break;
    case CTSC_ACK:    handleAck(payload, plen); break;
    case CTSC_FRAMES: handleFrames(payload, plen); break;
    case CTSC_STATUS: handleStatus(payload, plen); break;
    case CTSC_LOG:    sendDebug("Device: " + QString::fromLatin1((const char*)payload, (int)plen)); break;
    default:
        sendDebug(QString("Unknown packet type 0x%1").arg(type, 2, 16, QChar('0')));
    }
}

void CANTriple::handleInfo(const uint8_t *p, size_t len)
{
    if (len < 4) return;

    //copy what we got; a shorter INFO from a newer, slimmer firmware leaves the rest zero
    memset(&mInfo, 0, sizeof(mInfo));
    memcpy(&mInfo, p, qMin(len, sizeof(mInfo)));
    mHaveInfo = true;
    mRetries = 0;

    if (mInfo.proto_version != CTSC_PROTOCOL_VERSION)
    {
        sendDebug(QString("Device speaks protocol version %1, this SavvyCAN speaks %2. Update whichever is older.")
                  .arg(mInfo.proto_version).arg(CTSC_PROTOCOL_VERSION));
        mLink = Link::Hello;
        mSinceStep.restart();
        return;
    }

    const quint16 caps = qFromLittleEndian(mInfo.caps);
    const QString model = mInfo.model == CTSC_MODEL_V2 ? "2" : "1";
    sendDebug(QString("CAN Triple %1 (hw rev %2), SavvyCAN firmware %3.%4.%5, serial %6, CAN FD %7, termination %8")
              .arg(model).arg(mInfo.hw_revision)
              .arg(mInfo.fw_major).arg(mInfo.fw_minor).arg(mInfo.fw_patch)
              .arg(QString::fromLatin1(mInfo.serial, (int)qstrnlen(mInfo.serial, sizeof(mInfo.serial))))
              .arg((caps & CTSC_CAP_FD) ? "yes" : "no")
              .arg((caps & CTSC_CAP_TERMINATION) ? "yes" : "no"));

    //an INFO in the middle of a session means the device rebooted; either way, start afresh
    beginSession();
}

void CANTriple::handleAck(const uint8_t *p, size_t len)
{
    if (len < sizeof(ctsc_ack_t)) return;
    ctsc_ack_t ack;
    memcpy(&ack, p, sizeof(ack));

    if (ack.acked_type == CTSC_BUS_CONFIG && ack.bus < CTSC_NUM_BUSES)
    {
        if (ack.status != CTSC_OK)
        {
            mCtl[ack.bus].note = QString("Settings refused: %1").arg(ackStatusName(ack.status));
            sendDebug(QString("Bus %1 refused its settings: %2").arg(ack.bus).arg(ackStatusName(ack.status)));
        }
        else mCtl[ack.bus].note.clear();
        return;
    }

    if (ack.acked_type == CTSC_START && mLink == Link::Starting)
    {
        if (ack.status != CTSC_OK)
        {
            sendDebug(QString("Device refused START: %1").arg(ackStatusName(ack.status)));
            return;
        }

        //anchor the device clock to the wall clock at the moment it started streaming
        mHaveDeviceTime = false;
        const qint64 devNow = extendDeviceTime(qFromLittleEndian(ack.time_us));
        mTimeBasis = QDateTime::currentMSecsSinceEpoch() * 1000 - devNow;

        mLink = Link::Streaming;
        mSinceRx.restart();
        mSincePing.restart();
        setStatus(CANCon::CONNECTED);
        sendDebug("Streaming");
        return;
    }

    if (ack.status != CTSC_OK)
        sendDebug(QString("Command 0x%1 refused: %2").arg(ack.acked_type, 2, 16, QChar('0')).arg(ackStatusName(ack.status)));
}

qint64 CANTriple::extendDeviceTime(uint32_t t)
{
    if (!mHaveDeviceTime)
    {
        mHaveDeviceTime = true;
        mLastDeviceTime = t;
        mDeviceTimeExt = t;
        return mDeviceTimeExt;
    }

    /* Signed difference, so both the wrap every 71 minutes and a frame stamped a hair earlier than
     * the one before it (two buses interrupting close together) come out right. */
    mDeviceTimeExt += (int32_t)(t - mLastDeviceTime);
    mLastDeviceTime = t;
    return mDeviceTimeExt;
}

void CANTriple::handleFrames(const uint8_t *p, size_t len)
{
    if (mLink != Link::Streaming) return;

    size_t pos = 0;
    while (pos + CTSC_RX_RECORD_MIN <= len)
    {
        const uint8_t hdr = p[pos];
        const uint8_t dlc = p[pos + 1] & 0x0F;
        const bool ext = hdr & CTSC_HDR_EXT;
        const bool rtr = hdr & CTSC_HDR_RTR;
        const size_t idLen = ext ? 4 : 2;
        const size_t dataLen = rtr ? 0 : ctsc_dlc_to_len(dlc);
        const size_t recLen = 2 + 4 + idLen + dataLen;
        if (pos + recLen > len)
        {
            sendDebug("Truncated frame record, rest of packet dropped");
            return;
        }

        const uint32_t ts = qFromLittleEndian<uint32_t>(p + pos + 2);
        const uint32_t id = ext ? (qFromLittleEndian<uint32_t>(p + pos + 6) & 0x1FFFFFFF)
                                : (qFromLittleEndian<uint16_t>(p + pos + 6) & 0x7FF);
        const uint8_t *data = p + pos + 6 + idLen;
        pos += recLen;

        //keep the clock moving even for frames we end up not keeping
        const qint64 devTime = extendDeviceTime(ts);

        const int bus = hdr & CTSC_HDR_BUS_MASK;
        if (bus >= CTSC_NUM_BUSES || isCapSuspended()) continue;

        CANFrame *frame_p = getQueue().get();
        if (!frame_p)
        {
            mQueueDrops++;
            continue;
        }

        *frame_p = CANFrame();
        frame_p->bus = bus;
        frame_p->isReceived = !(hdr & CTSC_HDR_ECHO);
        frame_p->setFrameId(id);
        frame_p->setExtendedFrameFormat(ext);
        if (hdr & CTSC_HDR_FDF)
        {
            frame_p->setFlexibleDataRateFormat(true);
            frame_p->setBitrateSwitch(hdr & CTSC_HDR_BRS);
            frame_p->setErrorStateIndicator(hdr & CTSC_HDR_ESI);
        }
        if (rtr) frame_p->setFrameType(QCanBusFrame::RemoteRequestFrame);
        else frame_p->setPayload(QByteArray((const char*)data, (int)dataLen));

        /* The whole time goes in the microseconds field, which is where SavvyCAN reads it from.
         * TimeStamp::fromMicroSeconds() would split it into seconds and a 0-999999 remainder, and
         * the frame list keeps only the remainder - every time column and delta then wraps each
         * second. */
        if (useSystemTime)
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp(0, QDateTime::currentMSecsSinceEpoch() * 1000));
        else
            frame_p->setTimeStamp(QCanBusFrame::TimeStamp(0, mTimeBasis + devTime));

        checkTargettedFrame(*frame_p);
        getQueue().queue();
    }
}

void CANTriple::handleStatus(const uint8_t *p, size_t len)
{
    if (len < sizeof(ctsc_status_t)) return;
    ctsc_status_t st;
    memcpy(&st, p, sizeof(st));

    extendDeviceTime(qFromLittleEndian(st.time_us));
    const quint16 hostDrops = qFromLittleEndian(st.host_drops);

    const int n = qMin<int>(st.num_buses, CTSC_NUM_BUSES);
    for (int i = 0; i < n; i++)
    {
        const size_t off = sizeof(ctsc_status_t) + i * sizeof(ctsc_bus_status_t);
        if (off + sizeof(ctsc_bus_status_t) > len) break;
        ctsc_bus_status_t bs;
        memcpy(&bs, p + off, sizeof(bs));

        CANControllerState &c = mCtl[i];
        c.valid = true;
        c.state = stateName(bs.state);
        c.txErrorCount = bs.tec;
        c.rxErrorCount = bs.rec;
        c.busErrors += qFromLittleEndian(bs.bus_errors);
        c.rxOverruns += qFromLittleEndian(bs.rx_dropped);
        c.txRefused += qFromLittleEndian(bs.tx_dropped);
        //the link is shared, so link and queue losses are reported against every bus
        c.linkDrops += hostDrops;
        CANControllerState shown = c;
        shown.linkDrops += mQueueDrops;
        if (bs.flags & CTSC_BST_TERMINATION) shown.note = shown.note.isEmpty() ? QString("Terminated") : shown.note + ", terminated";
        setControllerState(i, shown);
    }
}
