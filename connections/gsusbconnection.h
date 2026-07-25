#ifndef GSUSBCONNECTION_H
#define GSUSBCONNECTION_H

#include "canconnection.h"
#include <libusb.h>
#include <QAtomicInt>
#include <QThread>

struct gs_device_config {
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t icount;
    uint32_t sw_version;
    uint32_t hw_version;
};

struct gs_device_mode {
    uint32_t mode;
    uint32_t flags;
};

struct gs_device_state {
    uint32_t state;
    uint32_t rxerr;
    uint32_t txerr;
};

struct gs_device_bittiming {
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
};

struct gs_host_frame {
    uint32_t echo_id;
    uint32_t can_id;
    uint8_t can_dlc;
    uint8_t channel;
    uint8_t flags;
    uint8_t reserved;
    uint8_t data[8];
};

struct gs_device_bittiming_info {
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
};

class GSUSBConnection : public CANConnection
{
    Q_OBJECT
public:
    GSUSBConnection(QString pPortName, int pBusSpeed);
    virtual ~GSUSBConnection();

    /* Look for gs_usb adapters on the USB bus. Static so the connection dialog can offer a list
     * without having to create a connection first. */
    static QList<CANDeviceInfo> enumerateDevices();

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;

private slots:
    /* both of these are invoked from the reading thread but execute in the working
     * thread so that the lock free queue keeps a single producer */
    void handleRxFrame(CANFrame frame);
    void handleDeviceLost();

private:
    bool initLibusb();
    bool connectDevice();
    //identify a device by its serial number if it has one, otherwise by where it sits on the bus
    static bool isGsUsbDevice(uint16_t vid, uint16_t pid);
    static QString deviceKey(libusb_device *dev, libusb_device_handle *handle);
    void readThread();
    /* compute bit timing values for the given bitrate, false if it can't be reached */
    bool calcBitTiming(int speed, gs_device_bittiming& timing);
    /* push bit timing and mode of the given bus down to the device */
    bool applyBusSettings(int busIdx);
    int ctrlOut(uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len);
    int ctrlIn(uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len);
    void sendStatus();

    libusb_context *ctx;
    libusb_device_handle *dev_handle;
    int mBusSpeed;
    QThread* mReadThread;
    QAtomicInt mKeepReading;
};

#endif // GSUSBCONNECTION_H
