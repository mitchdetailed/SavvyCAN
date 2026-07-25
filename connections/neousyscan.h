#ifndef NEOUSYSCAN_H
#define NEOUSYSCAN_H

#include <QLibrary>

#include "canconnection.h"

/*
 * Neousys embedded CAN ports through the WDT_DIO library, matching python-can's "neousys" backend.
 * These are the CAN ports built into Neousys industrial PCs.
 *
 * Unlike the other vendor drivers this one is push rather than poll: we hand the library a callback
 * and it calls us from its own thread. That thread must not touch the frame queue, so received
 * frames are posted over to the connection's worker thread first.
 *
 * The port name is the CAN port number, so "0" is the first one.
 */

#ifdef Q_OS_WIN
//WDT_DIO's callbacks are plain cdecl, which is what CFUNCTYPE gives python-can
#define NEOUSYS_CALL __cdecl
#else
#define NEOUSYS_CALL
#endif

struct neousys_can_msg
{
    uint32_t id;
    uint16_t flags;
    uint8_t extra;
    uint8_t len;
    uint8_t data[8];
};

struct neousys_can_setup
{
    uint32_t bitRate;
    uint32_t recvConfig;
    uint32_t recvId;
    uint32_t recvMask;
};

class NeousysCan : public CANConnection
{
    Q_OBJECT

public:
    NeousysCan(QString portName, int busSpeed);
    virtual ~NeousysCan();

    //called from the driver's own thread by the trampoline below
    void receivedFromDriver(const neousys_can_msg *msgs, unsigned int count);

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;

private slots:
    //runs in the worker thread so the queue keeps a single producer
    void handleRxFrame(CANFrame frame);

private:
    bool loadLibrary();
    void unloadLibrary();
    bool startPort();
    void stopPort();
    void sendStatus();

    typedef void (NEOUSYS_CALL *neousysRxCallback)(neousys_can_msg *msgs, unsigned int count);
    typedef void (NEOUSYS_CALL *neousysStatusCallback)(unsigned int status);

    typedef int (*fnCanSetup)(unsigned int port, neousys_can_setup *setup, unsigned int size);
    typedef int (*fnCanStart)(unsigned int port);
    typedef int (*fnCanStop)(unsigned int port);
    typedef int (*fnCanSend)(unsigned int port, neousys_can_msg *msg, unsigned int size);
    typedef int (*fnCanRegisterReceived)(unsigned int port, neousysRxCallback cb);
    typedef int (*fnCanRegisterStatus)(unsigned int port, neousysStatusCallback cb);

    fnCanSetup CAN_Setup;
    fnCanStart CAN_Start;
    fnCanStop CAN_Stop;
    fnCanSend CAN_Send;
    fnCanRegisterReceived CAN_RegisterReceived;
    fnCanRegisterStatus CAN_RegisterStatus;

    QLibrary mLib;
    unsigned int mPort;
    bool mStarted;
    int mBusSpeed;
};

#endif // NEOUSYSCAN_H
