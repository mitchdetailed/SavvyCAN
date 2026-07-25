#ifndef USB2CANLIB_H
#define USB2CANLIB_H

#include <QLibrary>
#include <QTimer>

#include "canconnection.h"

/*
 * 8devices USB2CAN through the CANAL interface (usb2can.dll), matching python-can's "usb2can"
 * backend. On Linux these adapters are plain SocketCAN devices so use the SerialBus connection type
 * there instead; this exists for Windows where the CANAL DLL is the only way in.
 *
 * The DLL ships with the adapter's Windows driver and can't be redistributed, so it is opened by
 * name at connect time rather than linked.
 *
 * The port name is the adapter's serial number, which is what CANAL wants in its config string.
 */

#ifdef Q_OS_WIN
#define CANAL_CALL __stdcall
#else
#define CANAL_CALL
#endif

/* CANAL structures. These use the C header's natural alignment (the trailing timestamp lands on a
 * 4 byte boundary after the 8 data bytes), so do NOT pack them. */
struct canal_msg
{
    uint32_t flags;
    uint32_t obid;
    uint32_t id;
    uint8_t sizeData;
    uint8_t data[8];
    uint32_t timestamp;
};

struct canal_status
{
    uint32_t channel_status;
    uint32_t lasterrorcode;
    uint32_t lasterrorsubcode;
    char lasterrorstr[80];
};

class Usb2CanLib : public CANConnection
{
    Q_OBJECT

public:
    Usb2CanLib(QString portName, int busSpeed);
    virtual ~Usb2CanLib();

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
    bool openAdapter();
    void closeAdapter();
    void sendStatus();

    typedef long (CANAL_CALL *fnCanalOpen)(const char *configStr, uint32_t flags);
    typedef int  (CANAL_CALL *fnCanalClose)(long handle);
    typedef int  (CANAL_CALL *fnCanalSend)(long handle, canal_msg *msg);
    typedef int  (CANAL_CALL *fnCanalReceive)(long handle, canal_msg *msg);
    typedef int  (CANAL_CALL *fnCanalGetStatus)(long handle, canal_status *status);

    fnCanalOpen CanalOpen;
    fnCanalClose CanalClose;
    fnCanalSend CanalSend;
    fnCanalReceive CanalReceive;
    fnCanalGetStatus CanalGetStatus;

    QLibrary mLib;
    QTimer mTimer;
    long mHandle;
    bool mOpen;
    int mBusSpeed;
    qint64 mTimeBasis;
    bool mHaveTimeBasis;
};

#endif // USB2CANLIB_H
