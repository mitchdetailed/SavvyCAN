#include "gsusbconnection.h"
#include <QDateTime>
#include <QDebug>
#include <QCanBusFrame>

#define GS_USB_BREQ_HOST_FORMAT 0
#define GS_USB_BREQ_BITTIMING 1
#define GS_USB_BREQ_MODE 2
#define GS_USB_BREQ_BT_CONST 3
#define GS_USB_BREQ_DEVICE_CONFIG 4
#define GS_USB_BREQ_TIMESTAMP 5
#define GS_USB_BREQ_IDENTIFY 6

#define GS_CAN_MODE_RESET 0
#define GS_CAN_MODE_START 1

#define GS_CAN_MODE_NORMAL 0
#define GS_CAN_MODE_LISTEN_ONLY (1<<0)
#define GS_CAN_MODE_LOOP_BACK (1<<1)

#define GS_USB_ENDPOINT_IN 0x81
#define GS_USB_ENDPOINT_OUT 0x02

//flags carried in the can_id field of a host frame
#define GS_CAN_ID_EFF 0x80000000u
#define GS_CAN_ID_RTR 0x40000000u
#define GS_CAN_ID_ERR 0x20000000u
#define GS_CAN_ID_MASK 0x1FFFFFFFu

//an echo id of all ones marks a frame that really came off of the bus, anything
//else is the device echoing back a frame we asked it to transmit
#define GS_USB_RX_ECHO_ID 0xFFFFFFFFu

//candlelight and the other gs_usb firmwares all clock the CAN core at 48MHz
#define GS_USB_CAN_CLOCK 48000000u
//1 (sync) + 1 (prop) + 12 (phase1) + 2 (phase2)
#define GS_USB_TQ_PER_BIT 16u
//brp is 10 bits wide on the supported controllers
#define GS_USB_MAX_BRP 1024u

//used whenever we are handed a bus speed of zero (resetting a connection does that)
#define GS_USB_DEFAULT_SPEED 500000

GSUSBConnection::GSUSBConnection(QString pPortName, int pBusSpeed)
    : CANConnection(pPortName, "GSUSB", CANCon::GSUSB, 0, pBusSpeed, false, 0, 1, 4000, true),
      ctx(nullptr), dev_handle(nullptr), mBusSpeed(pBusSpeed > 0 ? pBusSpeed : GS_USB_DEFAULT_SPEED),
      mReadThread(nullptr), mKeepReading(0)
{
    if (pBusSpeed <= 0) qDebug() << "GSUSB: no bus speed given, defaulting to" << mBusSpeed;
}

GSUSBConnection::~GSUSBConnection()
{
    stop();
}

int GSUSBConnection::ctrlOut(uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len)
{
    return libusb_control_transfer(dev_handle,
                                   LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
                                   request, value, index, (unsigned char*)data, len, 1000);
}

int GSUSBConnection::ctrlIn(uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len)
{
    return libusb_control_transfer(dev_handle,
                                   LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
                                   request, value, index, (unsigned char*)data, len, 1000);
}

bool GSUSBConnection::initLibusb()
{
    if (ctx) return true;
    int r = libusb_init(&ctx);
    if (r < 0) {
        qDebug() << "GSUSB: Init Error" << r;
        ctx = nullptr;
        return false;
    }
    return true;
}

/*
 * The gs_usb firmware is used by a lot of different adapters. This is the same ID list the Linux
 * gs_usb driver matches on, so anything the kernel calls a gs_usb device we will too.
 */
bool GSUSBConnection::isGsUsbDevice(uint16_t vid, uint16_t pid)
{
    if (vid == 0x1d50 && pid == 0x606f) return true; //Geschwister Schneider / candleLight / CANable
    if (vid == 0x1209 && pid == 0x2323) return true; //candleLight with the openmoko ID
    if (vid == 0x1cd2 && pid == 0x606f) return true; //CES CANext FD
    if (vid == 0x16d0 && pid == 0x10b8) return true; //ABE CAN Debugger FD
    return false;
}

/*
 * How we name a particular adapter. A serial number is the only thing that stays with the device
 * across a replug, so use it when there is one and fall back to its position on the bus.
 */
QString GSUSBConnection::deviceKey(libusb_device *dev, libusb_device_handle *handle)
{
    if (handle)
    {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) == 0 && desc.iSerialNumber)
        {
            unsigned char serial[128];
            memset(serial, 0, sizeof(serial));
            if (libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, serial, sizeof(serial) - 1) > 0)
            {
                const QString str = QString::fromLatin1((const char*)serial).trimmed();
                if (!str.isEmpty()) return str;
            }
        }
    }

    return QString("%1:%2").arg(libusb_get_bus_number(dev)).arg(libusb_get_device_address(dev));
}

QList<CANDeviceInfo> GSUSBConnection::enumerateDevices()
{
    QList<CANDeviceInfo> found;

    libusb_context *scanCtx = nullptr;
    if (libusb_init(&scanCtx) < 0)
    {
        qDebug() << "GSUSB: could not start libusb for a device scan";
        return found;
    }

    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(scanCtx, &list);

    for (ssize_t i = 0; i < count; i++)
    {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (!isGsUsbDevice(desc.idVendor, desc.idProduct)) continue;

        //opening is only so we can read the string descriptors, it's fine if it fails
        libusb_device_handle *handle = nullptr;
        libusb_open(list[i], &handle);

        QString product;
        if (handle && desc.iProduct)
        {
            unsigned char buf[128];
            memset(buf, 0, sizeof(buf));
            if (libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf) - 1) > 0)
                product = QString::fromLatin1((const char*)buf).trimmed();
        }
        if (product.isEmpty()) product = "gs_usb device";

        const QString key = deviceKey(list[i], handle);
        if (handle) libusb_close(handle);

        found.append(CANDeviceInfo(key, QString("%1 [%2] (%3:%4)")
                                            .arg(product).arg(key)
                                            .arg(desc.idVendor, 4, 16, QChar('0'))
                                            .arg(desc.idProduct, 4, 16, QChar('0'))));
    }

    if (list) libusb_free_device_list(list, 1);
    libusb_exit(scanCtx);

    return found;
}

bool GSUSBConnection::connectDevice()
{
    if (!ctx) return false;

    /* An empty port name (or the old fixed "GSUSB" one) means "whichever adapter you find", which
     * keeps connections saved before this had a device list working. */
    const QString wanted = getPort().trimmed();
    const bool takeAny = wanted.isEmpty() || (wanted.compare("GSUSB", Qt::CaseInsensitive) == 0);

    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(ctx, &list);

    for (ssize_t i = 0; i < count && !dev_handle; i++)
    {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (!isGsUsbDevice(desc.idVendor, desc.idProduct)) continue;

        libusb_device_handle *handle = nullptr;
        if (libusb_open(list[i], &handle) != 0 || !handle)
        {
            qDebug() << "GSUSB: found an adapter but could not open it (driver or permissions?)";
            continue;
        }

        if (takeAny || deviceKey(list[i], handle) == wanted) dev_handle = handle;
        else libusb_close(handle);
    }

    if (list) libusb_free_device_list(list, 1);

    if (!dev_handle) {
        qDebug() << "GSUSB: Cannot open device" << (takeAny ? QString("(no gs_usb adapter found)") : QString("'%1'").arg(wanted));
        emit debugOutput(takeAny ? QString("GSUSB: no gs_usb adapter found")
                                 : QString("GSUSB: no adapter matching '%1' was found").arg(wanted));
        return false;
    }

    if (libusb_kernel_driver_active(dev_handle, 0) == 1) {
        if (libusb_detach_kernel_driver(dev_handle, 0) == 0) {
            qDebug() << "GSUSB: Kernel driver detached";
        }
    }

    int r = libusb_claim_interface(dev_handle, 0);
    if (r < 0) {
        qDebug() << "GSUSB: Cannot claim interface" << r;
        libusb_close(dev_handle);
        dev_handle = nullptr;
        return false;
    }

    uint32_t host_format = 0x0000BEEF;
    r = ctrlOut(GS_USB_BREQ_HOST_FORMAT, 1, 0, &host_format, sizeof(host_format));
    if (r < 0) qDebug() << "GSUSB: could not set host format" << r;

    gs_device_config config;
    memset(&config, 0, sizeof(config));
    r = ctrlIn(GS_USB_BREQ_DEVICE_CONFIG, 0, 0, &config, sizeof(config));
    if (r != (int)sizeof(config))
    {
        //the device did not tell us how many channels it has so assume the common single channel case
        qDebug() << "GSUSB: could not read device config" << r << "- assuming one channel";
        mNumBuses = 1;
    }
    else
    {
        mNumBuses = (int)config.icount + 1;
        //icount is a byte off of the wire, don't trust it blindly
        if (mNumBuses < 1 || mNumBuses > 16)
        {
            qDebug() << "GSUSB: device reported an implausible channel count" << mNumBuses << "- assuming one channel";
            mNumBuses = 1;
        }
    }

    mBusData.resize(mNumBuses);
    for(int i=0; i<mNumBuses; i++) {
        mBusData[i].mConfigured = true;
        mBusData[i].mBus.setSpeed(mBusSpeed);
        mBusData[i].mBus.setCanFD(false);
        mBusData[i].mBus.setActive(true);
    }
    return true;
}

bool GSUSBConnection::calcBitTiming(int speed, gs_device_bittiming& timing)
{
    if (speed <= 0) return false;

    uint32_t brp = GS_USB_CAN_CLOCK / (GS_USB_TQ_PER_BIT * (uint32_t)speed);
    if (brp < 1 || brp > GS_USB_MAX_BRP) return false;

    // 16 total time quanta: 1 (sync) + 1 (prop) + 12 (phase1) + 2 (phase2) = 16.
    // 500k bitrate gives BRP = 6. Formula: BRP = 48000000 / (16 * speed)
    timing.prop_seg = 1;
    timing.phase_seg1 = 12;
    timing.phase_seg2 = 2;
    timing.sjw = 1;
    timing.brp = brp;
    return true;
}

bool GSUSBConnection::applyBusSettings(int busIdx)
{
    if (!dev_handle) return false;
    if (busIdx < 0 || busIdx >= mBusData.count()) return false;

    const CANBus& bus = mBusData[busIdx].mBus;
    int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    gs_device_bittiming timing;
    memset(&timing, 0, sizeof(timing));
    if (!calcBitTiming(speed, timing))
    {
        qDebug() << "GSUSB: cannot reach bus speed" << speed << "- leaving bus" << busIdx << "alone";
        return false;
    }

    //the channel has to be stopped before it will accept new bit timing
    gs_device_mode mode;
    memset(&mode, 0, sizeof(mode));
    mode.mode = GS_CAN_MODE_RESET;
    mode.flags = 0;
    int r = ctrlOut(GS_USB_BREQ_MODE, (uint16_t)busIdx, 0, &mode, sizeof(mode));
    if (r < 0)
    {
        qDebug() << "GSUSB: could not reset bus" << busIdx << r;
        return false;
    }

    r = ctrlOut(GS_USB_BREQ_BITTIMING, (uint16_t)busIdx, 0, &timing, sizeof(timing));
    if (r < 0)
    {
        qDebug() << "GSUSB: could not set bit timing on bus" << busIdx << r;
        return false;
    }

    if (!bus.isActive()) return true;

    mode.mode = GS_CAN_MODE_START;
    mode.flags = bus.isListenOnly() ? GS_CAN_MODE_LISTEN_ONLY : GS_CAN_MODE_NORMAL;
    r = ctrlOut(GS_USB_BREQ_MODE, (uint16_t)busIdx, 0, &mode, sizeof(mode));
    if (r < 0)
    {
        qDebug() << "GSUSB: could not start bus" << busIdx << r;
        return false;
    }

    return true;
}

void GSUSBConnection::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void GSUSBConnection::piStarted()
{
    if (!initLibusb() || !connectDevice())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    setStatus(CANCon::CONNECTED);

    for (int i = 0; i < mNumBuses; i++) applyBusSettings(i);

    mKeepReading.storeRelease(1);
    // spawn read thread
    mReadThread = QThread::create([this]{ readThread(); });
    mReadThread->start();

    sendStatus();
}

void GSUSBConnection::piStop()
{
    mKeepReading.storeRelease(0);
    if (mReadThread) {
        mReadThread->wait();
        delete mReadThread;
        mReadThread = nullptr;
    }

    if (dev_handle) {
        gs_device_mode mode;
        memset(&mode, 0, sizeof(mode));
        mode.mode = GS_CAN_MODE_RESET;
        mode.flags = 0;
        for (int i = 0; i < mNumBuses; i++) {
            ctrlOut(GS_USB_BREQ_MODE, (uint16_t)i, 0, &mode, sizeof(mode));
        }

        libusb_release_interface(dev_handle, 0);
        libusb_close(dev_handle);
        dev_handle = nullptr;
    }

    if (ctx) {
        libusb_exit(ctx);
        ctx = nullptr;
    }
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void GSUSBConnection::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx < 0 || pBusIdx >= mBusData.count()) return;

    setBusConfig(pBusIdx, pBus);
    applyBusSettings(pBusIdx);
}

bool GSUSBConnection::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void GSUSBConnection::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);

    /* flush queue if we are suspended */
    if(isCapSuspended())
        getQueue().flush();
}

bool GSUSBConnection::piSendFrame(const CANFrame& pFrame)
{
    if (!dev_handle) return false;
    if (pFrame.bus < 0 || pFrame.bus >= mNumBuses) return false;

    gs_host_frame tx_frame;
    memset(&tx_frame, 0, sizeof(tx_frame));

    tx_frame.echo_id = 0;
    tx_frame.can_id = pFrame.frameId() & GS_CAN_ID_MASK;
    if (pFrame.hasExtendedFrameFormat()) {
        tx_frame.can_id |= GS_CAN_ID_EFF;
    }
    if (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) {
        tx_frame.can_id |= GS_CAN_ID_RTR;
    }

    //this is classic CAN only, the frame the device expects has room for 8 bytes
    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    tx_frame.can_dlc = (uint8_t)len;
    tx_frame.channel = (uint8_t)pFrame.bus;
    tx_frame.flags = 0;

    memcpy(tx_frame.data, payload.constData(), len);

    int transferred = 0;
    int r = libusb_bulk_transfer(dev_handle, GS_USB_ENDPOINT_OUT, (unsigned char*)&tx_frame, sizeof(tx_frame), &transferred, 1000);
    return (r == 0);
}

void GSUSBConnection::readThread()
{
    gs_host_frame rx_frame;
    int transferred = 0;
    while (mKeepReading.loadAcquire()) {
        int r = libusb_bulk_transfer(dev_handle, GS_USB_ENDPOINT_IN, (unsigned char*)&rx_frame, sizeof(rx_frame), &transferred, 100);

        if (r == LIBUSB_ERROR_TIMEOUT) continue;

        if (r != LIBUSB_SUCCESS)
        {
            qDebug() << "GSUSB: read error" << r;
            if (r == LIBUSB_ERROR_NO_DEVICE || r == LIBUSB_ERROR_NOT_FOUND || r == LIBUSB_ERROR_IO)
            {
                //the device went away, stop hammering libusb and let the UI know
                mKeepReading.storeRelease(0);
                QMetaObject::invokeMethod(this, "handleDeviceLost", Qt::QueuedConnection);
            }
            continue;
        }

        if (transferred < (int)sizeof(rx_frame)) continue;

        //frames we transmitted come back to us here. They were already put in the queue
        //by CANConnection::sendFrame so drop them instead of showing them twice
        if (rx_frame.echo_id != GS_USB_RX_ECHO_ID) continue;

        CANFrame frame;
        frame.bus = rx_frame.channel;
        frame.isReceived = true;
        frame.setExtendedFrameFormat((rx_frame.can_id & GS_CAN_ID_EFF) != 0);
        frame.setFrameType((rx_frame.can_id & GS_CAN_ID_RTR) ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
        frame.setFrameId(rx_frame.can_id & GS_CAN_ID_MASK);

        //DLC codes above 8 have no meaning for classic CAN and there are only 8 data bytes to read
        int len = rx_frame.can_dlc & 0x0F;
        if (len > 8) len = 8;
        frame.setPayload(QByteArray((char*)rx_frame.data, len));

        //the frames this firmware hands us carry no timestamp of their own
        frame.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));

        //the queue only supports one producer so it has to be filled from the working thread
        QMetaObject::invokeMethod(this, "handleRxFrame", Qt::QueuedConnection, Q_ARG(CANFrame, frame));
    }
}

void GSUSBConnection::handleRxFrame(CANFrame frame)
{
    if (isCapSuspended()) return;

    if (frame.bus < 0 || frame.bus >= mBusData.count())
    {
        qDebug() << "GSUSB: dropping frame from unknown channel" << frame.bus;
        return;
    }

    CANFrame* frame_p = getQueue().get();
    if (!frame_p)
    {
        qDebug() << "GSUSB: can't get a frame, ERROR";
        return;
    }

    *frame_p = frame;
    checkTargettedFrame(*frame_p);
    getQueue().queue();
}

void GSUSBConnection::handleDeviceLost()
{
    if (getStatus() == CANCon::NOT_CONNECTED) return;

    qDebug() << "GSUSB: lost connection to device";
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}
