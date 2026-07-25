#include "pythoncanserial.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>
#include <QSerialPortInfo>

#define PYCAN_START 0xAA
#define PYCAN_END 0xBB

//start + timestamp + length + id + end, data sits between the id and the end byte
#define PYCAN_OVERHEAD 11

PythonCanSerial::PythonCanSerial(QString portName, int serialSpeed) :
    CANConnection(portName, "PYTHONCAN", CANCon::PYCAN_SERIAL,
                  (serialSpeed > 0) ? serialSpeed : 115200, 0, false, 0, 1, 4000, true),
    serial(nullptr), mTimeBasis(0), mHaveTimeBasis(false)
{
}

PythonCanSerial::~PythonCanSerial()
{
    stop();
}

bool PythonCanSerial::connectDevice()
{
    serial = new QSerialPort(QSerialPortInfo(getPort()));
    if (!serial)
    {
        qDebug() << "PYTHONCAN: can't create serial port" << getPort();
        return false;
    }

    connect(serial, &QSerialPort::readyRead, this, &PythonCanSerial::readSerialData);
    connect(serial, &QSerialPort::errorOccurred, this, &PythonCanSerial::serialError);

    serial->setBaudRate(mSerialSpeed);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!serial->open(QIODevice::ReadWrite))
    {
        qDebug() << "PYTHONCAN: could not open" << getPort() << serial->errorString();
        delete serial;
        serial = nullptr;
        return false;
    }

    return true;
}

void PythonCanSerial::disconnectDevice()
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

void PythonCanSerial::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void PythonCanSerial::piStarted()
{
    mBuildData.clear();
    mHaveTimeBasis = false;

    if (!connectDevice())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    //there is no bit rate to set, the other end owns the bus
    mBusData[0].mConfigured = true;
    mBusData[0].mBus.setActive(true);

    setStatus(CANCon::CONNECTED);
    sendStatus();
}

void PythonCanSerial::piStop()
{
    disconnectDevice();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void PythonCanSerial::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx != 0) return;

    //nothing to push to a device here, this transport carries whatever the far end is doing
    setBusConfig(0, pBus);
}

bool PythonCanSerial::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void PythonCanSerial::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool PythonCanSerial::piSendFrame(const CANFrame& pFrame)
{
    if (!serial || !serial->isOpen()) return false;
    if (pFrame.bus != 0) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray data = pFrame.payload();
    int len = data.length();
    if (len > 8) len = 8;

    //python-can truncates its float seconds timestamp to 32 bits of milliseconds, do the same
    const uint32_t ts = (uint32_t)(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    const uint32_t id = pFrame.frameId() & 0x1FFFFFFF;

    QByteArray out;
    out.append((char)PYCAN_START);
    out.append((char)(ts & 0xFF));
    out.append((char)((ts >> 8) & 0xFF));
    out.append((char)((ts >> 16) & 0xFF));
    out.append((char)((ts >> 24) & 0xFF));
    out.append((char)len);
    out.append((char)(id & 0xFF));
    out.append((char)((id >> 8) & 0xFF));
    out.append((char)((id >> 16) & 0xFF));
    out.append((char)((id >> 24) & 0xFF));
    out.append(data.constData(), len);
    out.append((char)PYCAN_END);

    return (serial->write(out) == out.length());
}

void PythonCanSerial::readSerialData()
{
    if (!serial) return;

    mBuildData.append(serial->readAll());
    processBuffer();
}

void PythonCanSerial::processBuffer()
{
    while (mBuildData.length() >= PYCAN_OVERHEAD)
    {
        if ((uint8_t)mBuildData.at(0) != PYCAN_START)
        {
            mBuildData.remove(0, 1);
            continue;
        }

        const int len = (uint8_t)mBuildData.at(5);
        if (len > 8)
        {
            //not a length this protocol can carry, we're out of sync
            mBuildData.remove(0, 1);
            continue;
        }

        const int frameLen = PYCAN_OVERHEAD + len;
        if (mBuildData.length() < frameLen) return; //the rest is still in flight

        if ((uint8_t)mBuildData.at(frameLen - 1) != PYCAN_END)
        {
            mBuildData.remove(0, 1);
            continue;
        }

        uint32_t ts = 0;
        for (int i = 0; i < 4; i++) ts |= ((uint32_t)(uint8_t)mBuildData.at(1 + i)) << (8 * i);

        uint32_t id = 0;
        for (int i = 0; i < 4; i++) id |= ((uint32_t)(uint8_t)mBuildData.at(6 + i)) << (8 * i);

        if (!isCapSuspended())
        {
            CANFrame* frame_p = getQueue().get();
            if (frame_p)
            {
                frame_p->bus = 0;
                frame_p->isReceived = true;
                //the protocol has no flag for it so anything above the 11 bit range must be extended
                frame_p->setExtendedFrameFormat(id > 0x7FF);
                frame_p->setFrameType(QCanBusFrame::DataFrame);
                frame_p->setFrameId(id & 0x1FFFFFFF);
                frame_p->setPayload(mBuildData.mid(10, len));

                if (useSystemTime)
                {
                    frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
                }
                else
                {
                    //anchor the sender's millisecond counter to our clock the first time we see it
                    if (!mHaveTimeBasis)
                    {
                        mTimeBasis = QDateTime::currentMSecsSinceEpoch() - (qint64)ts;
                        mHaveTimeBasis = true;
                    }
                    frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds((mTimeBasis + (qint64)ts) * 1000ull));
                }

                checkTargettedFrame(*frame_p);
                getQueue().queue();
            }
            else qDebug() << "PYTHONCAN: can't get a frame, ERROR";
        }

        mBuildData.remove(0, frameLen);
    }
}

void PythonCanSerial::serialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError) return;

    qDebug() << "PYTHONCAN: serial error" << err;
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError ||
        err == QSerialPort::PermissionError)
    {
        disconnectDevice();
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
    }
}
