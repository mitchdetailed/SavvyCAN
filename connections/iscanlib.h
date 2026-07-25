#ifndef ISCANLIB_H
#define ISCANLIB_H

#include <QLibrary>
#include <QTimer>

#include "canconnection.h"

/*
 * Thorsis / ifak isCAN adapters through iscandrv, matching python-can's "iscan" backend.
 *
 * The driver library is the vendor's and isn't linked at build time - it gets opened by name when
 * the connection starts.
 *
 * The port name is the isCAN channel number, so "0" is the first channel.
 */

#ifdef Q_OS_WIN
#define ISCAN_CALL __stdcall
#else
#define ISCAN_CALL
#endif

/* Natural alignment, same as the vendor header (the 4 byte ID is followed by three single bytes and
 * then the data, so nothing needs padding until the tail). */
struct iscan_message
{
    uint32_t message_id;
    uint8_t is_extended;
    uint8_t remote_req;
    uint8_t data_len;
    uint8_t data[8];
};

class IscanLib : public CANConnection
{
    Q_OBJECT

public:
    IscanLib(QString portName, int busSpeed);
    virtual ~IscanLib();

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;

private slots:
    void handleTick();

private:
    bool loadLibrary();
    void unloadLibrary();
    bool openChannel();
    void closeChannel();
    void sendStatus();
    static bool baudrateCode(int speed, uint8_t &code);

    typedef uint8_t (ISCAN_CALL *fnDeviceInitEx)(uint8_t channel, uint8_t baudrate);
    typedef uint8_t (ISCAN_CALL *fnReceiveMessageEx)(uint8_t channel, iscan_message *msg);
    typedef uint8_t (ISCAN_CALL *fnTransmitMessageEx)(uint8_t channel, iscan_message *msg);
    typedef uint8_t (ISCAN_CALL *fnCloseDevice)(uint8_t channel);

    fnDeviceInitEx isCAN_DeviceInitEx;
    fnReceiveMessageEx isCAN_ReceiveMessageEx;
    fnTransmitMessageEx isCAN_TransmitMessageEx;
    fnCloseDevice isCAN_CloseDevice;

    QLibrary mLib;
    QTimer mTimer;
    uint8_t mChannel;
    bool mOpen;
    int mBusSpeed;
};

#endif // ISCANLIB_H
