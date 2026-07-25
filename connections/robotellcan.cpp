#include "robotellcan.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>
#include <QSerialPortInfo>

#define ROBOTELL_FRAME_HEAD 0xAA
#define ROBOTELL_FRAME_END 0x55
#define ROBOTELL_ESCAPE 0xA5

//the unescaped payload is always this long, checksum included
#define ROBOTELL_PAYLOAD_LEN 17

#define ROBOTELL_CHANNEL_CAN 0x00
#define ROBOTELL_CHANNEL_CONFIG 0xFF

#define ROBOTELL_FMT_STANDARD 0
#define ROBOTELL_FMT_EXTENDED 1
#define ROBOTELL_TYPE_DATA 0
#define ROBOTELL_TYPE_REMOTE 1

//config registers, addressed as extended CAN IDs on the config channel
#define ROBOTELL_ID_RESET 0x01FFFEC0
#define ROBOTELL_ID_SERIALBPS 0x01FFFE90
#define ROBOTELL_ID_BAUD 0x01FFFED0

RobotellCAN::RobotellCAN(QString portName, int serialSpeed, int busSpeed) :
    CANConnection(portName, "ROBOTELL", CANCon::ROBOTELL,
                  (serialSpeed > 0) ? serialSpeed : 115200, busSpeed, false, 0, 1, 4000, true),
    serial(nullptr)
{
}

RobotellCAN::~RobotellCAN()
{
    stop();
}

bool RobotellCAN::connectDevice()
{
    serial = new QSerialPort(QSerialPortInfo(getPort()));
    if (!serial)
    {
        qDebug() << "ROBOTELL: can't create serial port" << getPort();
        return false;
    }

    connect(serial, &QSerialPort::readyRead, this, &RobotellCAN::readSerialData);
    connect(serial, &QSerialPort::errorOccurred, this, &RobotellCAN::serialError);

    serial->setBaudRate(mSerialSpeed);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!serial->open(QIODevice::ReadWrite))
    {
        qDebug() << "ROBOTELL: could not open" << getPort() << serial->errorString();
        delete serial;
        serial = nullptr;
        return false;
    }

    return true;
}

void RobotellCAN::disconnectDevice()
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

//build the 17 byte payload, escape it, wrap it and push it out the port
bool RobotellCAN::sendPayload(const uint8_t *payload16)
{
    if (!serial || !serial->isOpen()) return false;

    uint8_t payload[ROBOTELL_PAYLOAD_LEN];
    memcpy(payload, payload16, ROBOTELL_PAYLOAD_LEN - 1);

    uint8_t sum = 0;
    for (int i = 0; i < ROBOTELL_PAYLOAD_LEN - 1; i++) sum += payload[i];
    payload[ROBOTELL_PAYLOAD_LEN - 1] = sum;

    QByteArray out;
    out.append((char)ROBOTELL_FRAME_HEAD);
    out.append((char)ROBOTELL_FRAME_HEAD);

    for (int i = 0; i < ROBOTELL_PAYLOAD_LEN; i++)
    {
        const uint8_t b = payload[i];
        //anything that looks like framing has to be escaped, checksum included
        if (b == ROBOTELL_FRAME_HEAD || b == ROBOTELL_FRAME_END || b == ROBOTELL_ESCAPE)
            out.append((char)ROBOTELL_ESCAPE);
        out.append((char)b);
    }

    out.append((char)ROBOTELL_FRAME_END);
    out.append((char)ROBOTELL_FRAME_END);

    return (serial->write(out) == out.length());
}

void RobotellCAN::writeConfig(uint32_t configId, uint32_t value, int valueSize)
{
    uint8_t payload[ROBOTELL_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));

    payload[0] = configId & 0xFF;
    payload[1] = (configId >> 8) & 0xFF;
    payload[2] = (configId >> 16) & 0xFF;
    payload[3] = (configId >> 24) & 0xFF;

    //value goes in the data field little endian, however many bytes this register takes
    if (valueSize >= 1) payload[4] = value & 0xFF;
    if (valueSize >= 4)
    {
        payload[5] = (value >> 8) & 0xFF;
        payload[6] = (value >> 16) & 0xFF;
        payload[7] = (value >> 24) & 0xFF;
    }

    payload[12] = (uint8_t)valueSize;
    payload[13] = ROBOTELL_CHANNEL_CONFIG;
    payload[14] = ROBOTELL_FMT_EXTENDED;
    payload[15] = ROBOTELL_TYPE_DATA;

    sendPayload(payload);
}

void RobotellCAN::sendConfiguration()
{
    if (!serial || !serial->isOpen()) return;

    writeConfig(ROBOTELL_ID_RESET, 0, 1);

    const int speed = mBusData[0].mBus.getSpeed();
    if (speed > 0) writeConfig(ROBOTELL_ID_BAUD, (uint32_t)speed, 4);
    else qDebug() << "ROBOTELL: no bus speed set, leaving the adapter on whatever it had";
}

void RobotellCAN::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void RobotellCAN::piStarted()
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

void RobotellCAN::piStop()
{
    disconnectDevice();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void RobotellCAN::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    setBusConfig(0, pBus);
    sendConfiguration();
}

bool RobotellCAN::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void RobotellCAN::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool RobotellCAN::piSendFrame(const CANFrame& pFrame)
{
    if (!serial || !serial->isOpen()) return false;
    if (pFrame.bus != 0) return false;

    //error frames are a SavvyCAN concept, the adapter can't put one on the wire
    if (pFrame.frameId() & 0x20000000) return true;

    const QByteArray data = pFrame.payload();
    int len = data.length();
    if (len > 8) len = 8;

    const uint32_t id = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);

    uint8_t payload[ROBOTELL_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));

    payload[0] = id & 0xFF;
    payload[1] = (id >> 8) & 0xFF;
    payload[2] = (id >> 16) & 0xFF;
    payload[3] = (id >> 24) & 0xFF;
    memcpy(payload + 4, data.constData(), len);
    payload[12] = (uint8_t)len;
    payload[13] = ROBOTELL_CHANNEL_CAN;
    payload[14] = pFrame.hasExtendedFrameFormat() ? ROBOTELL_FMT_EXTENDED : ROBOTELL_FMT_STANDARD;
    payload[15] = (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) ? ROBOTELL_TYPE_REMOTE : ROBOTELL_TYPE_DATA;

    return sendPayload(payload);
}

void RobotellCAN::readSerialData()
{
    if (!serial) return;

    mBuildData.append(serial->readAll());
    processBuffer();
}

/*
 * Pull complete packets out of the raw stream. We look for the double head, unescape until the
 * double tail and hand the result off. Junk gets a byte shaved off the front so we resynchronise.
 */
void RobotellCAN::processBuffer()
{
    while (true)
    {
        //find the start of a packet
        while (mBuildData.length() >= 2 &&
               !((uint8_t)mBuildData.at(0) == ROBOTELL_FRAME_HEAD && (uint8_t)mBuildData.at(1) == ROBOTELL_FRAME_HEAD))
        {
            mBuildData.remove(0, 1);
        }
        if (mBuildData.length() < 2) return;

        QByteArray payload;
        bool escaped = false;
        bool complete = false;
        int idx = 2;

        for (; idx < mBuildData.length(); idx++)
        {
            const uint8_t b = (uint8_t)mBuildData.at(idx);

            if (escaped)
            {
                payload.append((char)b);
                escaped = false;
                continue;
            }

            if (b == ROBOTELL_ESCAPE)
            {
                escaped = true;
                continue;
            }

            if (b == ROBOTELL_FRAME_END)
            {
                //needs to be a pair of them to be the end of a packet
                if (idx + 1 >= mBuildData.length()) return; //wait for the second one
                if ((uint8_t)mBuildData.at(idx + 1) == ROBOTELL_FRAME_END)
                {
                    idx += 2;
                    complete = true;
                    break;
                }
                //a lone tail byte means we've lost sync
                break;
            }

            payload.append((char)b);
            if (payload.length() > ROBOTELL_PAYLOAD_LEN) break; //too long, this isn't a real packet
        }

        if (!complete)
        {
            //either the packet isn't all here yet or it's garbage. If we ran out of buffer wait
            //for more, otherwise drop the head and try to find the next one.
            if (idx >= mBuildData.length() && payload.length() <= ROBOTELL_PAYLOAD_LEN) return;
            mBuildData.remove(0, 1);
            continue;
        }

        mBuildData.remove(0, idx);
        handlePayload(payload);
    }
}

void RobotellCAN::handlePayload(const QByteArray &payload)
{
    if (payload.length() != ROBOTELL_PAYLOAD_LEN)
    {
        qDebug() << "ROBOTELL: dropping packet of odd length" << payload.length();
        return;
    }

    uint8_t sum = 0;
    for (int i = 0; i < ROBOTELL_PAYLOAD_LEN - 1; i++) sum += (uint8_t)payload.at(i);
    if (sum != (uint8_t)payload.at(ROBOTELL_PAYLOAD_LEN - 1))
    {
        qDebug() << "ROBOTELL: checksum mismatch, dropping packet";
        return;
    }

    //replies from the config channel aren't bus traffic
    if ((uint8_t)payload.at(13) == ROBOTELL_CHANNEL_CONFIG) return;

    int len = (uint8_t)payload.at(12);
    if (len > 8) len = 8;

    const bool extended = ((uint8_t)payload.at(14) == ROBOTELL_FMT_EXTENDED);
    const bool remote = ((uint8_t)payload.at(15) == ROBOTELL_TYPE_REMOTE);

    uint32_t id = 0;
    for (int i = 0; i < 4; i++) id |= ((uint32_t)(uint8_t)payload.at(i)) << (8 * i);

    if (isCapSuspended()) return;

    CANFrame* frame_p = getQueue().get();
    if (!frame_p)
    {
        qDebug() << "ROBOTELL: can't get a frame, ERROR";
        return;
    }

    frame_p->bus = 0;
    frame_p->isReceived = true;
    frame_p->setFrameId(id & (extended ? 0x1FFFFFFF : 0x7FF));
    frame_p->setExtendedFrameFormat(extended);
    frame_p->setFrameType(remote ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
    frame_p->setPayload(payload.mid(4, len));
    //nothing in the packet carries a device timestamp
    frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));

    checkTargettedFrame(*frame_p);
    getQueue().queue();
}

void RobotellCAN::serialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError) return;

    qDebug() << "ROBOTELL: serial error" << err;
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError ||
        err == QSerialPort::PermissionError)
    {
        disconnectDevice();
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
    }
}
