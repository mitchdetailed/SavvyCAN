#ifndef PYTHONCANSERIAL_H
#define PYTHONCANSERIAL_H

#include <QSerialPort>

#include "canconnection.h"

/*
 * python-can's "serial" backend (can.interfaces.serial).
 *
 * This isn't a particular piece of hardware, it's the trivial framing python-can uses to push CAN
 * traffic down a serial port or pty. Supporting it lets a python-can script and SavvyCAN feed each
 * other over a real port or a virtual pair.
 *
 * Every frame is:
 *   0xAA, timestamp (4 bytes LE, milliseconds), length (1), ID (4 bytes LE), data..., 0xBB
 */
class PythonCanSerial : public CANConnection
{
    Q_OBJECT

public:
    PythonCanSerial(QString portName, int serialSpeed);
    virtual ~PythonCanSerial();

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
    void processBuffer();
    void sendStatus();

    QSerialPort *serial;
    QByteArray mBuildData;
    //the wire timestamp is a 32 bit millisecond counter so it can't carry absolute time. We anchor
    //the first one we see to the wall clock and report everything relative to that.
    qint64 mTimeBasis;
    bool mHaveTimeBasis;
};

#endif // PYTHONCANSERIAL_H
