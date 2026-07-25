#ifndef CANALYSTII_H
#define CANALYSTII_H

#include <libusb.h>
#include <QTimer>

#include "canconnection.h"

/*
 * CANalyst-II (ControlCAN) two channel USB adapter, driven straight over libusb the same way
 * python-can's "canalystii" backend does through the python-canalystii library. No vendor DLL.
 *
 * The device is polled rather than streamed: we ask how many frames are waiting on a channel and
 * then read that many. All of it happens on the connection's own worker thread from a timer, which
 * keeps the USB endpoints single threaded and the frame queue single producer.
 */

#pragma pack(push, 1)
struct canalyst_message
{
    uint32_t can_id;
    uint32_t timestamp; //100us units
    uint8_t time_flag;
    uint8_t send_type;
    uint8_t remote;
    uint8_t extended;
    uint8_t data_len;
    uint8_t data[8];
};

struct canalyst_message_buffer
{
    uint8_t count; //1 to 3
    canalyst_message messages[3];
};

struct canalyst_simple_command
{
    uint32_t command;
    uint32_t padding[15];
};

struct canalyst_init_command
{
    uint32_t command;
    uint32_t acc_code;
    uint32_t acc_mask;
    uint32_t unknown0;
    uint32_t filter;
    uint32_t unknown1;
    uint32_t timing0;
    uint32_t timing1;
    uint32_t mode;
    uint32_t unknown2;
    uint32_t padding[6];
};

struct canalyst_message_status
{
    uint32_t command;
    uint32_t rx_pending;
    uint16_t tx_pending;
    uint16_t unknown;
    uint32_t padding[13];
};
#pragma pack(pop)

class CanalystII : public CANConnection
{
    Q_OBJECT

public:
    CanalystII(QString portName, int busSpeed);
    virtual ~CanalystII();

    //pure lookup, public so the unit tests can check the tables
    static bool bitTiming(int speed, uint32_t &timing0, uint32_t &timing1);

    //list the CANalyst-II adapters plugged in, for the connection dialog's device scan
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
    bool initLibusb();
    bool connectDevice();
    static QString deviceKey(libusb_device *dev, libusb_device_handle *handle);
    void closeDevice();
    bool applyBusSettings(int busIdx);
    bool stopChannel(int busIdx);
    bool sendCommand(int busIdx, const void *packet);
    bool readCommandReply(int busIdx, void *packet);
    void pollChannel(int busIdx);
    void queueFrame(const canalyst_message &msg, int busIdx);
    void sendStatus();

    libusb_context *ctx;
    libusb_device_handle *dev_handle;
    QTimer mTimer;
    int mBusSpeed;
    //device timestamps are a free running counter, anchor them to the wall clock
    qint64 mTimeBasis;
    bool mHaveTimeBasis;
};

#endif // CANALYSTII_H
