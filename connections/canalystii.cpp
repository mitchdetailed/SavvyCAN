#include "canalystii.h"

#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

#define CANALYST_VID 0x04D8
#define CANALYST_PID 0x0053

#define CANALYST_COMMAND_INIT 0x01
#define CANALYST_COMMAND_START 0x02
#define CANALYST_COMMAND_STOP 0x03
#define CANALYST_COMMAND_CLEAR_RX 0x05
#define CANALYST_COMMAND_MESSAGE_STATUS 0x0A
#define CANALYST_COMMAND_CAN_STATUS 0x0B

#define CANALYST_MODE_NORMAL 0
#define CANALYST_MODE_LISTEN_ONLY 1

#define CANALYST_PACKET_LEN 64
#define CANALYST_USB_TIMEOUT 100

//how many frames to pull off one channel in a single tick before letting the event loop breathe
#define CANALYST_MAX_PER_TICK 60

//channel 0 uses endpoints 2 (commands) and 1 (data), channel 1 uses 4 and 3
static const uint8_t CANALYST_COMMAND_EP[2] = {0x02, 0x04};
static const uint8_t CANALYST_MESSAGE_EP[2] = {0x01, 0x03};

CanalystII::CanalystII(QString portName, int busSpeed) :
    CANConnection(portName, "CANALYSTII", CANCon::CANALYSTII, 0, busSpeed, false, 0, 2, 4000, true),
    ctx(nullptr), dev_handle(nullptr), mTimer(this),
    mBusSpeed(busSpeed > 0 ? busSpeed : 500000), mTimeBasis(0), mHaveTimeBasis(false)
{
}

CanalystII::~CanalystII()
{
    stop();
}

/*
 * The controller is an SJA1000 clone clocked at 16MHz, so these are the classic BTR0/BTR1 pairs
 * that every ControlCAN based tool uses.
 */
bool CanalystII::bitTiming(int speed, uint32_t &timing0, uint32_t &timing1)
{
    switch (speed)
    {
    case 1000000: timing0 = 0x00; timing1 = 0x14; return true;
    case 800000:  timing0 = 0x00; timing1 = 0x16; return true;
    case 500000:  timing0 = 0x00; timing1 = 0x1C; return true;
    case 250000:  timing0 = 0x01; timing1 = 0x1C; return true;
    case 125000:  timing0 = 0x03; timing1 = 0x1C; return true;
    case 100000:  timing0 = 0x04; timing1 = 0x1C; return true;
    case 50000:   timing0 = 0x09; timing1 = 0x1C; return true;
    case 20000:   timing0 = 0x18; timing1 = 0x1C; return true;
    case 10000:   timing0 = 0x31; timing1 = 0x1C; return true;
    case 5000:    timing0 = 0x63; timing1 = 0x1C; return true;
    default: return false;
    }
}

bool CanalystII::initLibusb()
{
    if (ctx) return true;

    int r = libusb_init(&ctx);
    if (r < 0)
    {
        qDebug() << "CANALYSTII: libusb init error" << r;
        ctx = nullptr;
        return false;
    }
    return true;
}

//serial number if the adapter reports one, otherwise where it sits on the USB bus
QString CanalystII::deviceKey(libusb_device *dev, libusb_device_handle *handle)
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

QList<CANDeviceInfo> CanalystII::enumerateDevices()
{
    QList<CANDeviceInfo> found;

    libusb_context *scanCtx = nullptr;
    if (libusb_init(&scanCtx) < 0)
    {
        qDebug() << "CANALYSTII: could not start libusb for a device scan";
        return found;
    }

    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(scanCtx, &list);

    for (ssize_t i = 0; i < count; i++)
    {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != CANALYST_VID || desc.idProduct != CANALYST_PID) continue;

        libusb_device_handle *handle = nullptr;
        libusb_open(list[i], &handle);

        const QString key = deviceKey(list[i], handle);
        if (handle) libusb_close(handle);

        found.append(CANDeviceInfo(key, QString("CANalyst-II [%1]").arg(key)));
    }

    if (list) libusb_free_device_list(list, 1);
    libusb_exit(scanCtx);

    return found;
}

bool CanalystII::connectDevice()
{
    if (!ctx) return false;

    /* An empty port name, or the "0" the dialog offers by default, means take the first adapter we
     * find. Anything else has to match a key from a device scan. */
    const QString wanted = getPort().trimmed();
    const bool takeAny = wanted.isEmpty() || wanted == "0";

    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(ctx, &list);

    for (ssize_t i = 0; i < count && !dev_handle; i++)
    {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != CANALYST_VID || desc.idProduct != CANALYST_PID) continue;

        libusb_device_handle *handle = nullptr;
        if (libusb_open(list[i], &handle) != 0 || !handle)
        {
            qDebug() << "CANALYSTII: found an adapter but could not open it (driver or permissions?)";
            continue;
        }

        if (takeAny || deviceKey(list[i], handle) == wanted) dev_handle = handle;
        else libusb_close(handle);
    }

    if (list) libusb_free_device_list(list, 1);

    if (!dev_handle)
    {
        qDebug() << "CANALYSTII: cannot open device";
        emit debugOutput(takeAny ? QString("CANALYSTII: no adapter found")
                                 : QString("CANALYSTII: no adapter matching '%1' was found").arg(wanted));
        return false;
    }

    if (libusb_kernel_driver_active(dev_handle, 0) == 1)
    {
        if (libusb_detach_kernel_driver(dev_handle, 0) == 0)
            qDebug() << "CANALYSTII: kernel driver detached";
    }

    int r = libusb_claim_interface(dev_handle, 0);
    if (r < 0)
    {
        qDebug() << "CANALYSTII: cannot claim interface" << r;
        libusb_close(dev_handle);
        dev_handle = nullptr;
        return false;
    }

    return true;
}

void CanalystII::closeDevice()
{
    if (dev_handle)
    {
        libusb_release_interface(dev_handle, 0);
        libusb_close(dev_handle);
        dev_handle = nullptr;
    }

    if (ctx)
    {
        libusb_exit(ctx);
        ctx = nullptr;
    }
}

bool CanalystII::sendCommand(int busIdx, const void *packet)
{
    if (!dev_handle) return false;
    if (busIdx < 0 || busIdx > 1) return false;

    int transferred = 0;
    int r = libusb_bulk_transfer(dev_handle, CANALYST_COMMAND_EP[busIdx], (unsigned char*)packet,
                                 CANALYST_PACKET_LEN, &transferred, CANALYST_USB_TIMEOUT);
    if (r != LIBUSB_SUCCESS)
    {
        qDebug() << "CANALYSTII: command write failed" << r;
        return false;
    }
    return (transferred == CANALYST_PACKET_LEN);
}

bool CanalystII::readCommandReply(int busIdx, void *packet)
{
    if (!dev_handle) return false;
    if (busIdx < 0 || busIdx > 1) return false;

    int transferred = 0;
    int r = libusb_bulk_transfer(dev_handle, CANALYST_COMMAND_EP[busIdx] | LIBUSB_ENDPOINT_IN,
                                 (unsigned char*)packet, CANALYST_PACKET_LEN, &transferred,
                                 CANALYST_USB_TIMEOUT);
    if (r != LIBUSB_SUCCESS) return false;
    return (transferred == CANALYST_PACKET_LEN);
}

bool CanalystII::applyBusSettings(int busIdx)
{
    if (!dev_handle) return false;
    if (busIdx < 0 || busIdx >= mBusData.count()) return false;

    const CANBus& bus = mBusData[busIdx].mBus;
    const int speed = (bus.getSpeed() > 0) ? bus.getSpeed() : mBusSpeed;

    uint32_t timing0 = 0, timing1 = 0;
    if (!bitTiming(speed, timing0, timing1))
    {
        qDebug() << "CANALYSTII: unsupported bus speed" << speed << "- leaving channel" << busIdx << "alone";
        return false;
    }

    //stop it first so a reconfigure while running behaves
    stopChannel(busIdx);

    canalyst_init_command init;
    memset(&init, 0, sizeof(init));
    init.command = CANALYST_COMMAND_INIT;
    init.acc_code = 0;
    init.acc_mask = 0xFFFFFFFF; //a wide open mask accepts every ID
    init.filter = 0;
    init.timing0 = timing0;
    init.timing1 = timing1;
    init.mode = bus.isListenOnly() ? CANALYST_MODE_LISTEN_ONLY : CANALYST_MODE_NORMAL;

    if (!sendCommand(busIdx, &init)) return false;

    if (!bus.isActive()) return true;

    canalyst_simple_command start;
    memset(&start, 0, sizeof(start));
    start.command = CANALYST_COMMAND_START;
    return sendCommand(busIdx, &start);
}

bool CanalystII::stopChannel(int busIdx)
{
    canalyst_simple_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CANALYST_COMMAND_STOP;
    return sendCommand(busIdx, &cmd);
}

void CanalystII::sendStatus()
{
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}

void CanalystII::piStarted()
{
    mHaveTimeBasis = false;

    if (!initLibusb() || !connectDevice())
    {
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    for (int i = 0; i < mNumBuses; i++)
    {
        mBusData[i].mConfigured = true;
        mBusData[i].mBus.setSpeed(mBusSpeed);
        mBusData[i].mBus.setActive(true);
        applyBusSettings(i);
    }

    setStatus(CANCon::CONNECTED);

    //UniqueConnection so a stop/start cycle cannot stack a second delivery per tick
    connect(&mTimer, &QTimer::timeout, this, &CanalystII::handleTick, Qt::UniqueConnection);
    mTimer.setInterval(10);
    mTimer.setSingleShot(false);
    mTimer.start();

    sendStatus();
}

void CanalystII::piStop()
{
    mTimer.stop();

    if (dev_handle)
    {
        for (int i = 0; i < mNumBuses; i++) stopChannel(i);
    }

    closeDevice();
    setStatus(CANCon::NOT_CONNECTED);
    sendStatus();
}

void CanalystII::piSetBusSettings(int pBusIdx, CANBus pBus)
{
    if (pBusIdx < 0 || pBusIdx >= mBusData.count()) return;

    setBusConfig(pBusIdx, pBus);
    applyBusSettings(pBusIdx);
}

bool CanalystII::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void CanalystII::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool CanalystII::piSendFrame(const CANFrame& pFrame)
{
    if (!dev_handle) return false;
    if (pFrame.bus < 0 || pFrame.bus >= mNumBuses) return false;
    if (pFrame.frameId() & 0x20000000) return true; //locally generated error frame

    const QByteArray payload = pFrame.payload();
    int len = payload.length();
    if (len > 8) len = 8;

    canalyst_message_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.count = 1;

    canalyst_message &msg = buffer.messages[0];
    msg.can_id = pFrame.frameId() & (pFrame.hasExtendedFrameFormat() ? 0x1FFFFFFF : 0x7FF);
    msg.timestamp = 0;
    msg.time_flag = 0;
    msg.send_type = 0;
    msg.remote = (pFrame.frameType() == QCanBusFrame::RemoteRequestFrame) ? 1 : 0;
    msg.extended = pFrame.hasExtendedFrameFormat() ? 1 : 0;
    msg.data_len = (uint8_t)len;
    memcpy(msg.data, payload.constData(), len);

    int transferred = 0;
    int r = libusb_bulk_transfer(dev_handle, CANALYST_MESSAGE_EP[pFrame.bus], (unsigned char*)&buffer,
                                 sizeof(buffer), &transferred, CANALYST_USB_TIMEOUT);
    return (r == LIBUSB_SUCCESS);
}

void CanalystII::handleTick()
{
    if (!dev_handle) return;

    for (int i = 0; i < mNumBuses; i++) pollChannel(i);
}

void CanalystII::pollChannel(int busIdx)
{
    //ask how much is waiting for us
    canalyst_simple_command statusCmd;
    memset(&statusCmd, 0, sizeof(statusCmd));
    statusCmd.command = CANALYST_COMMAND_MESSAGE_STATUS;
    if (!sendCommand(busIdx, &statusCmd)) return;

    canalyst_message_status reply;
    memset(&reply, 0, sizeof(reply));
    if (!readCommandReply(busIdx, &reply)) return;

    uint32_t pending = reply.rx_pending;
    if (pending == 0) return;
    if (pending > CANALYST_MAX_PER_TICK) pending = CANALYST_MAX_PER_TICK;

    //frames arrive in the same count prefixed 64 byte buffers we transmit with, up to 3 per buffer
    const uint32_t numBuffers = (pending + 2) / 3;

    QByteArray raw;
    raw.resize((int)(numBuffers * sizeof(canalyst_message_buffer)));

    int transferred = 0;
    int r = libusb_bulk_transfer(dev_handle, CANALYST_MESSAGE_EP[busIdx] | LIBUSB_ENDPOINT_IN,
                                 (unsigned char*)raw.data(), raw.length(), &transferred,
                                 CANALYST_USB_TIMEOUT);
    if (r != LIBUSB_SUCCESS)
    {
        qDebug() << "CANALYSTII: message read failed" << r;
        return;
    }

    const int bufCount = transferred / (int)sizeof(canalyst_message_buffer);
    for (int i = 0; i < bufCount; i++)
    {
        canalyst_message_buffer buffer;
        memcpy(&buffer, raw.constData() + (i * sizeof(canalyst_message_buffer)), sizeof(buffer));
        int count = buffer.count;
        if (count > 3) count = 3;
        for (int j = 0; j < count; j++) queueFrame(buffer.messages[j], busIdx);
    }
}

void CanalystII::queueFrame(const canalyst_message &msg, int busIdx)
{
    if (isCapSuspended()) return;

    CANFrame* frame_p = getQueue().get();
    if (!frame_p)
    {
        qDebug() << "CANALYSTII: can't get a frame, ERROR";
        return;
    }

    int len = msg.data_len;
    if (len > 8) len = 8;

    frame_p->bus = busIdx;
    frame_p->isReceived = true;
    frame_p->setExtendedFrameFormat(msg.extended != 0);
    frame_p->setFrameType(msg.remote ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
    frame_p->setFrameId(msg.can_id & (msg.extended ? 0x1FFFFFFF : 0x7FF));
    frame_p->setPayload(QByteArray((const char*)msg.data, len));

    if (useSystemTime)
    {
        frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ull));
    }
    else
    {
        //the device counts in 100us ticks from whenever it started, anchor the first one we see
        const qint64 deviceUs = (qint64)msg.timestamp * 100;
        if (!mHaveTimeBasis)
        {
            mTimeBasis = (QDateTime::currentMSecsSinceEpoch() * 1000) - deviceUs;
            mHaveTimeBasis = true;
        }
        frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds((quint64)(mTimeBasis + deviceUs)));
    }

    checkTargettedFrame(*frame_p);
    getQueue().queue();
}
