#ifndef IXXATVCI_H
#define IXXATVCI_H

#include <QLibrary>
#include <QTimer>

#include "canconnection.h"

/*
 * IXXAT interfaces through IXXAT's VCI driver (vcinpl).
 *
 * Same deal as the Kvaser driver: the VCI runtime is IXXAT's and can't be shipped with SavvyCAN, so
 * the DLL is opened by name at connect time and nothing is linked at build time.
 *
 * The port name is "device" or "device:channel", both zero based, so "0" is the first channel of the
 * first adapter and "0:1" is its second channel.
 */

#ifdef Q_OS_WIN
#define VCI_CALL __stdcall
#else
#define VCI_CALL
#endif

/* VCI structures. Field order here matches the VCI headers - it is packed but every member lands on
 * its natural boundary anyway, so packed and default alignment come out identical (304 bytes for the
 * device info). Don't reorder anything. */
#pragma pack(push, 1)
struct vci_luid
{
    uint32_t LowPart;
    int32_t HighPart;
};

union vci_id
{
    vci_luid AsLuid;
    int64_t AsInt64;
};

struct vci_guid
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

struct vci_device_info
{
    vci_id VciObjectId;
    vci_guid DeviceClass;
    uint8_t DriverMajorVersion;
    uint8_t DriverMinorVersion;
    uint16_t DriverBuildVersion;
    uint8_t HardwareBranchVersion;
    uint8_t HardwareMajorVersion;
    uint8_t HardwareMinorVersion;
    uint8_t HardwareBuildVersion;
    uint8_t UniqueHardwareId[16];
    char Description[128];
    char Manufacturer[126];
    uint16_t DriverReleaseVersion;
};

/* The real header declares uMsgInfo as a union of bitfields. We keep it as a plain 32 bit word and
 * use masks instead so we don't depend on how the compiler happens to lay bitfields out. */
struct vci_canmsg
{
    uint32_t dwTime;
    uint32_t dwMsgId;
    uint32_t uMsgInfo;
    uint8_t abData[8];
};
#pragma pack(pop)

class IxxatVci : public CANConnection
{
    Q_OBJECT

public:
    IxxatVci(QString portName, int busSpeed);
    virtual ~IxxatVci();

    //pure lookup, public so the unit tests can check the tables
    static bool bitTiming(int speed, uint8_t &btr0, uint8_t &btr1);

    //walk the VCI device list, for the connection dialog's device scan
    static QList<CANDeviceInfo> enumerateDevices();

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
    bool openDevice();
    void closeDevice();
    bool applyBusSettings();
    void sendStatus();

    typedef int32_t (VCI_CALL *fnVciInitialize)(void);
    typedef int32_t (VCI_CALL *fnVciEnumDeviceOpen)(void **hEnum);
    typedef int32_t (VCI_CALL *fnVciEnumDeviceNext)(void *hEnum, vci_device_info *info);
    typedef int32_t (VCI_CALL *fnVciEnumDeviceClose)(void *hEnum);
    typedef int32_t (VCI_CALL *fnVciDeviceOpen)(vci_id *id, void **hDevice);
    typedef int32_t (VCI_CALL *fnVciDeviceClose)(void *hDevice);
    typedef int32_t (VCI_CALL *fnCanChannelOpen)(void *hDevice, uint32_t channel, int32_t exclusive, void **hChannel);
    typedef int32_t (VCI_CALL *fnCanChannelInitialize)(void *hChannel, uint16_t rxFifoSize, uint16_t rxThreshold,
                                                      uint16_t txFifoSize, uint16_t txThreshold);
    typedef int32_t (VCI_CALL *fnCanChannelActivate)(void *hChannel, int32_t enable);
    typedef int32_t (VCI_CALL *fnCanChannelClose)(void *hChannel);
    typedef int32_t (VCI_CALL *fnCanChannelReadMessage)(void *hChannel, uint32_t timeout, vci_canmsg *msg);
    typedef int32_t (VCI_CALL *fnCanChannelPostMessage)(void *hChannel, vci_canmsg *msg);
    typedef int32_t (VCI_CALL *fnCanControlOpen)(void *hDevice, uint32_t channel, void **hControl);
    typedef int32_t (VCI_CALL *fnCanControlInitialize)(void *hControl, uint8_t mode, uint8_t btr0, uint8_t btr1);
    typedef int32_t (VCI_CALL *fnCanControlStart)(void *hControl, int32_t start);
    typedef int32_t (VCI_CALL *fnCanControlReset)(void *hControl);
    typedef int32_t (VCI_CALL *fnCanControlClose)(void *hControl);

    fnVciInitialize vciInitialize;
    fnVciEnumDeviceOpen vciEnumDeviceOpen;
    fnVciEnumDeviceNext vciEnumDeviceNext;
    fnVciEnumDeviceClose vciEnumDeviceClose;
    fnVciDeviceOpen vciDeviceOpen;
    fnVciDeviceClose vciDeviceClose;
    fnCanChannelOpen canChannelOpen;
    fnCanChannelInitialize canChannelInitialize;
    fnCanChannelActivate canChannelActivate;
    fnCanChannelClose canChannelClose;
    fnCanChannelReadMessage canChannelReadMessage;
    fnCanChannelPostMessage canChannelPostMessage;
    fnCanControlOpen canControlOpen;
    fnCanControlInitialize canControlInitialize;
    fnCanControlStart canControlStart;
    fnCanControlReset canControlReset;
    fnCanControlClose canControlClose;

    QLibrary mLib;
    QTimer mTimer;
    void *mDevice;
    void *mChannelHandle;
    void *mControlHandle;
    int mDeviceIdx;
    uint32_t mChannel;
    int mBusSpeed;
    qint64 mTimeBasis;
    bool mHaveTimeBasis;
};

#endif // IXXATVCI_H
