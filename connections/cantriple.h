#ifndef CANTRIPLE_H
#define CANTRIPLE_H

#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>
#include <QByteArray>

#include "canconnection.h"
#include "cantriple_protocol.h"

/*
 * CAN Triple V1 / V2 running the dedicated SavvyCAN firmware.
 *
 * Both models look the same from here: a serial port (the V1's on-board ST-LINK virtual COM port,
 * or the V2's own USB CDC port) carrying the CTSC protocol in cantriple_protocol.h. Which model is
 * on the other end, and so whether CAN FD is available, comes from the device's INFO reply rather
 * than from anything the user picks.
 *
 * Three buses, each with its own speed, listen only, CAN FD (V2 only) and termination.
 */
class CANTriple : public CANConnection
{
    Q_OBJECT

public:
    CANTriple(QString portName, int busSpeed, bool canFd, int dataRate);
    virtual ~CANTriple();

    /* serial ports that look like a CAN Triple - an ST-LINK VCP or the V2's own CDC port */
    static QList<CANDeviceInfo> enumerateDevices();

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;
    virtual bool piSendFrames(const QList<CANFrame>& pFrames) override;

    /* The byte transport. A QSerialPort normally; the unit tests swap in an in-memory device
     * emulator by overriding these three and pushing the emulator's replies through feedBytes. */
    virtual bool transportOpen();
    virtual void transportClose();
    virtual void transportWrite(const QByteArray &bytes);
    void feedBytes(const QByteArray &bytes);

private slots:
    void readSerialData();
    void serialError(QSerialPort::SerialPortError err);
    void handleTick();

private:
    enum class Link
    {
        Closed,     //port not open, retrying now and then
        Hello,      //port open, waiting for INFO
        Starting,   //INFO in, bus configs and START sent, waiting for START's ACK
        Streaming   //frames flowing
    };

    bool openPort();
    void closePort();
    void sendHello();
    void beginSession();
    void sendPacket(uint8_t type, const QByteArray &payload);
    void sendBusConfig(int bus);
    //append one tx record for frame to out, false if the frame can't go out on this device
    bool buildTxRecord(const CANFrame &frame, QByteArray &out);

    void processPacket(const uint8_t *pkt, size_t len);
    void handleInfo(const uint8_t *p, size_t len);
    void handleAck(const uint8_t *p, size_t len);
    void handleFrames(const uint8_t *p, size_t len);
    void handleStatus(const uint8_t *p, size_t len);

    //turn the device's wrapping 32 bit microsecond clock into a continuous one
    qint64 extendDeviceTime(uint32_t t);
    void sendDebug(const QString &text);

    QSerialPort    *mSerial;
    bool            mPortOpen;
    QTimer         *mTick;
    Link            mLink;
    QByteArray      mRxEncoded;          //bytes since the last 0x00 delimiter
    bool            mRxOverflow;         //current packet ran past CTSC_MAX_PACKET, skip to next 0x00
    QElapsedTimer   mSinceRx;            //since the last valid packet from the device
    QElapsedTimer   mSinceStep;          //since the link last changed state or retried
    QElapsedTimer   mSincePing;
    int             mRetries;

    ctsc_info_t     mInfo;
    bool            mHaveInfo;

    bool            mHaveDeviceTime;
    uint32_t        mLastDeviceTime;
    qint64          mDeviceTimeExt;      //extended device clock, microseconds
    qint64          mTimeBasis;          //host epoch microseconds at device time zero

    quint64         mQueueDrops;         //frames we had no queue slot for
    CANControllerState mCtl[CTSC_NUM_BUSES];
};

#endif // CANTRIPLE_H
