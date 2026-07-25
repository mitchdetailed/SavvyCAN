#ifndef ROBOTELLCAN_H
#define ROBOTELLCAN_H

#include <QSerialPort>

#include "canconnection.h"

/*
 * Robotell CAN-USB adapter.
 *
 * Matches python-can's "robotell" backend. Every packet on the wire is a fixed 17 byte structure
 * wrapped in framing bytes with escaping:
 *
 *   0xAA 0xAA <escaped payload> 0x55 0x55
 *
 * and the payload is:
 *   [0..3]   CAN ID, little endian
 *   [4..11]  data, zero padded
 *   [12]     length
 *   [13]     channel - 0x00 for real CAN traffic, 0xFF to poke a config register
 *   [14]     0 = standard ID, 1 = extended ID
 *   [15]     0 = data frame, 1 = remote frame
 *   [16]     checksum, the low byte of the sum of the previous 16
 *
 * Device settings are written by sending a frame to channel 0xFF with a magic ID.
 */
class RobotellCAN : public CANConnection
{
    Q_OBJECT

public:
    RobotellCAN(QString portName, int serialSpeed, int busSpeed);
    virtual ~RobotellCAN();

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;

private slots:
    void readSerialData();
    void serialError(QSerialPort::SerialPortError err);

private:
    bool connectDevice();
    void disconnectDevice();
    void sendConfiguration();
    void writeConfig(uint32_t configId, uint32_t value, int valueSize);
    bool sendPayload(const uint8_t *payload16);
    void processBuffer();
    void handlePayload(const QByteArray &payload);
    void sendStatus();

    QSerialPort *serial;
    QByteArray mBuildData; //raw bytes off the port, still escaped and framed
};

#endif // ROBOTELLCAN_H
