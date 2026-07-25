#ifndef SEEEDCAN_H
#define SEEEDCAN_H

#include <QSerialPort>
#include <QTimer>

#include "canconnection.h"

/*
 * Seeed Studio USB-CAN Analyzer.
 *
 * A small serial device that speaks a fixed framing of its own. The protocol implemented here
 * matches python-can's "seeedstudio" backend so logs and traffic line up between the two.
 *
 * Configuration is a single 20 byte packet. Frames themselves are:
 *   0xAA, type, ID (2 bytes for standard / 4 bytes for extended, little endian), data..., 0x55
 * where type is 0xC0 | (extended << 5) | (remote << 4) | length.
 */
class SeeedCAN : public CANConnection
{
    Q_OBJECT

public:
    SeeedCAN(QString portName, int serialSpeed, int busSpeed);
    virtual ~SeeedCAN();

    //pure lookup, public so the unit tests can check the tables
    static uint8_t bitrateCode(int speed);

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
    void processBuffer();
    void sendStatus();

    QSerialPort *serial;
    QByteArray mBuildData;
};

#endif // SEEEDCAN_H
