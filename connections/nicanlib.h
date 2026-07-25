#ifndef NICANLIB_H
#define NICANLIB_H

#include <QLibrary>
#include <QTimer>

#include "canconnection.h"

/*
 * National Instruments NI-CAN, matching python-can's "nican" backend.
 *
 * NI-CAN is the older of NI's two CAN stacks (NI-XNET is the newer one). The driver library is NI's
 * and isn't linked at build time, it gets opened by name at connect time.
 *
 * The port name is the NI-CAN object name, so "CAN0" is the first interface.
 */

#ifdef Q_OS_WIN
#define NICAN_CALL __stdcall
#else
#define NICAN_CALL
#endif

/* Natural alignment, matching the NI-CAN header. The 64 bit timestamp puts the whole receive struct
 * on an 8 byte boundary, so don't pack these. */
struct nican_rx_message
{
    uint64_t timestamp;
    uint32_t arb_id;
    uint8_t frame_type;
    uint8_t dlc;
    uint8_t data[8];
};

struct nican_tx_message
{
    uint32_t arb_id;
    uint8_t is_remote;
    uint8_t dlc;
    uint8_t data[8];
};

class NicanLib : public CANConnection
{
    Q_OBJECT

public:
    NicanLib(QString portName, int busSpeed);
    virtual ~NicanLib();

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
    bool openObject();
    void closeObject();
    void sendStatus();
    QString statusText(int32_t status);

    typedef int32_t (NICAN_CALL *fnConfig)(const char *name, uint32_t numAttrs,
                                           uint32_t *attrIdList, uint32_t *attrValueList);
    typedef int32_t (NICAN_CALL *fnOpenObject)(const char *name, uint32_t *handle);
    typedef int32_t (NICAN_CALL *fnCloseObject)(uint32_t handle);
    typedef int32_t (NICAN_CALL *fnRead)(uint32_t handle, uint32_t sizeofData, void *data);
    typedef int32_t (NICAN_CALL *fnWrite)(uint32_t handle, uint32_t sizeofData, void *data);
    typedef int32_t (NICAN_CALL *fnAction)(uint32_t handle, uint32_t opcode, uint32_t param);
    typedef int32_t (NICAN_CALL *fnWaitForState)(uint32_t handle, uint32_t desiredState,
                                                 uint32_t timeout, uint32_t *currentState);
    typedef int32_t (NICAN_CALL *fnStatusToString)(int32_t status, uint32_t sizeofString, char *str);

    fnConfig ncConfig;
    fnOpenObject ncOpenObject;
    fnCloseObject ncCloseObject;
    fnRead ncRead;
    fnWrite ncWrite;
    fnAction ncAction;
    fnWaitForState ncWaitForState;
    fnStatusToString ncStatusToString;

    QLibrary mLib;
    QTimer mTimer;
    uint32_t mHandle;
    bool mOpen;
    int mBusSpeed;
    qint64 mTimeBasis;
    bool mHaveTimeBasis;
};

#endif // NICANLIB_H
