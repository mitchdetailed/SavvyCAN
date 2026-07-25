#include "seeedcan.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>
#include <QSerialPortInfo>

//every packet the device sends or accepts is bracketed by these
#define SEEED_PACKET_START 0xAA
#define SEEED_PACKET_END 0x55
//the second byte of the configuration packet and its command id
#define SEEED_CFG_START2 0x55
#define SEEED_CFG_COMMAND 0x12
//data frames always have the top two bits of the type byte set
#define SEEED_TYPE_DATA 0xC0
#define SEEED_TYPE_EXT 0x20
#define SEEED_TYPE_RTR 0x10
#define SEEED_TYPE_LEN_MASK 0x0F

#define SEEED_MODE_NORMAL 0x00
#define SEEED_MODE_LOOPBACK 0x01
#define SEEED_MODE_SILENT 0x02

//the filter is set wide open so the frame type here only picks which ID width the filter uses
#define SEEED_FRAME_TYPE_STD 0x01

#define SEEED_CONFIG_LEN 20

SeeedCAN::SeeedCAN(QString portName, int serialSpeed, int busSpeed) :
    CANConnection(portName, "SEEED", CANCon::SEEEDSTUDIO,
                  (serialSpeed > 0) ? serialSpeed : 2000000, busSpeed, false, 0, 1, 4000, true),
    serial(nullptr)
{
}

SeeedCAN::~SeeedCAN()
{
    stop();
}

//the device takes a code rather than a speed. Anything it can't do falls back to 500k.
uint8_t SeeedCAN::bitrateCode(int speed)
{
    switch (speed)
    {
    case 1000000: return 0x01;
    case 800000:  return 0x02;
    case 500000:  return 0x03;
    case 400000:  return 0x04;
    case 250000:  return 0x05;
    case 200000:  return 0x06;
    case 125000:  return 0x07;
    case 100000:  return 0x08;
    case 50000:   return 0x09;
    case 20000:   return 0x0A;
    case 10000:   return 0x0B;
    case 5000:    return 0x0C;
    default:
        qDebug() << "SEEED: unsupported bus speed" << speed << "- using 500k";
        return 0x03;
    }
}

bool SeeedCAN::connectDevice()
{
    serial = new QSerialPort(QSerialPortInfo(getPort()));
    if (!serial)
    {
        qDebug() << "SEEED: can't create serial port" << getPort();
        return false;
    }

    connect(serial, &QSerialPort::readyRead, this, &SeeedCAN::readSerialData);
    connect(serial, &QSerialPort::errorOccurred, this, &SeeedCAN::serialError);

    serial->setBaudRate(mSerialSpeed);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!serial->open(QIODevice::ReadWrite))
    {
        qDebug() << "SEEED: could not open" << getPort() << serial->errorString();
        delete serial;
        serial = nullptr;
        return false;
    }

    return true;
}

void SeeedCAN::disconnectDevice()
{
    if (serial)
    {
        if (serial->isOpen()) serial->close();
        serial->disconnect();
        /* reachable from the port's own error signal, so deleting the sender here directly
         * would be use-after-free once the emission unwinds */
        serial->deleteLater();
        serial = nullptr;
    }
}

void SeeedCAN::sendConfiguration()
{
    if (!serial || !serial->isOpen()) return;

    const CANBus& bus = mBusData[0].mBus;
    int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : 500000;

    uint8_t cfg[SEEED_CONFIG_LEN];
    memset(cfg, 0, sizeof(cfg));

    cfg[0] = SEEED_PACKET_START;
    cfg[1] = SEEED_CFG_START2;
    cfg[2] = SEEED_CFG_COMMAND;
    cfg[3] = bitrateCode(speed);
    cfg[4] = SEEED_FRAME_TYPE_STD;
    //bytes 5-8 are the acceptance filter and 9-12 the mask. All zero lets everything through.
    cfg[13] = bus.isListenOnly() ? SEEED_MODE_SILENT : SEEED_MODE_NORMAL;
    cfg[14] = 0x01;
    //bytes 15-18 are a manual bit timing override which we don't use

    //checksum covers everything after the two start bytes
    uint8_t sum = 0;
    for (int i = 2; i < SEEED_CONFIG_LEN - 1; i++) sum += cfg[i];
    cfg[SEEED_CONFIG_LEN - 1] = sum;

    serial->write((const char*)cfg, SEEED_CONFIG_LEN);
}

void SeeedCAN::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void SeeedCAN::piStarted()
{
    mBuildData.clear();

    if (!connectDevice())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setActive(true);
    sendConfiguration();

    setStatus(CANCon::CONNECTED);
    sendStatus();
}

void SeeedCAN::piStop()
{
    disconnectDevice();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void SeeedCAN::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    setBusConfig(0, pBus);
    sendConfiguration();
}

bool SeeedCAN::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void SeeedCAN::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool SeeedCAN::piSendFrame(const CANFrame& pFrame)
{
    if (!serial || !serial->isOpen()) return false;
    if (pFrame.bus != 0) return false;

    //an error frame is something we made up locally, the device has no way to send one
    if (pFrame.frameId() & 0x20000000) return true;

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    const bool extended = pFrame.hasExtendedFrameFormat();
    const bool remote = (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame);

    QByteArray out;
    out.append((char)SEEED_PACKET_START);
    out.append((char)(SEEED_TYPE_DATA | (extended ? SEEED_TYPE_EXT : 0) | (remote ? SEEED_TYPE_RTR : 0) | len));

    const uint32_t id = pFrame.frameId() & (extended ? 0x1FFFFFFF : 0x7FF);
    out.append((char)(id & 0xFF));
    out.append((char)((id >> 8) & 0xFF));
    if (extended)
    {
        out.append((char)((id >> 16) & 0xFF));
        out.append((char)((id >> 24) & 0xFF));
    }

    out.append(payload.constData(), len);
    out.append((char)SEEED_PACKET_END);

    return (serial->write(out) == out.length());
}

void SeeedCAN::readSerialData()
{
    if (!serial) return;

    mBuildData.append(serial->readAll());
    processBuffer();
}

/*
 * Frames are variable length so we can only tell where one ends by decoding it. Anything that
 * doesn't decode gets a single byte shaved off the front and we try again, which resynchronises
 * on the next real frame.
 */
void SeeedCAN::processBuffer()
{
    while (mBuildData.length() >= 2)
    {
        if ((uint8_t)mBuildData.at(0) != SEEED_PACKET_START)
        {
            mBuildData.remove(0, 1);
            continue;
        }

        const uint8_t type = (uint8_t)mBuildData.at(1);
        if ((type & SEEED_TYPE_DATA) != SEEED_TYPE_DATA)
        {
            //not a data frame header, could be the tail of a config echo. Skip the start byte.
            mBuildData.remove(0, 1);
            continue;
        }

        const bool extended = (type & SEEED_TYPE_EXT) != 0;
        const bool remote = (type & SEEED_TYPE_RTR) != 0;
        int len = type & SEEED_TYPE_LEN_MASK;
        if (len > 8)
        {
            mBuildData.remove(0, 1);
            continue;
        }

        const int idLen = extended ? 4 : 2;
        const int frameLen = 2 + idLen + len + 1; //header + id + data + end byte
        if (mBuildData.length() < frameLen) return; //rest of it hasn't turned up yet

        if ((uint8_t)mBuildData.at(frameLen - 1) != SEEED_PACKET_END)
        {
            mBuildData.remove(0, 1);
            continue;
        }

        uint32_t id = 0;
        for (int i = 0; i < idLen; i++) id |= ((uint32_t)(uint8_t)mBuildData.at(2 + i)) << (8 * i);

        if (!isCapSuspended())
        {
            CANFrame* frame_p = getQueue().get();
            if (frame_p)
            {
                frame_p->bus = 0;
                frame_p->isReceived = true;
                frame_p->setFrameId(id);
                frame_p->setExtendedFrameFormat(extended);
                frame_p->setFrameType(remote ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
                frame_p->setPayload(mBuildData.mid(2 + idLen, len));
                //the device doesn't timestamp anything for us
                frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));

                checkTargettedFrame(*frame_p);
                getQueue().queue();
            }
            else qDebug() << "SEEED: can't get a frame, ERROR";
        }

        mBuildData.remove(0, frameLen);
    }
}

void SeeedCAN::serialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError) return;

    qDebug() << "SEEED: serial error" << err;
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError ||
        err == QSerialPort::PermissionError)
    {
        //the adapter is gone, there's no point pretending we're still connected
        disconnectDevice();
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
    }
}
