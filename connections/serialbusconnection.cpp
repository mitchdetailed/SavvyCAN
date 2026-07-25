#include "serialbusconnection.h"

#include "canconmanager.h"

#include <QCanBus>
#include <QCanBusFrame>
#include <QDateTime>
#include <QDebug>

/***********************************/
/****    class definition       ****/
/***********************************/

SerialBusConnection::SerialBusConnection(QString portName, QString driverName, int pBusSpeed, int pDataRate, bool pCanFd) :
    CANConnection(portName, driverName, CANCon::SERIALBUS,0 ,pBusSpeed, pCanFd, pDataRate ,1, 4000, true),
    mTimer(this) /*NB: set connection as parent of timer to manage it from working thread */
{
}


SerialBusConnection::~SerialBusConnection()
{
    stop();
}


void SerialBusConnection::piStarted()
{
    qDebug() << "piStarted()";
    /* create device */
    QString errorString;
    qDebug() << "Creating device instance";
    mDev_p = QCanBus::instance()->createDevice(getDriver(), getPort(), &errorString);
    if (!mDev_p) {
        disconnectDevice();
        qDebug() << "Error: createDevice(" << getType() << getDriver() << getPort() << "):" << errorString;
        return;
    }

    /* connect slots */
    connect(mDev_p, &QCanBusDevice::errorOccurred, this, &SerialBusConnection::errorReceived);
    connect(mDev_p, &QCanBusDevice::framesWritten, this, &SerialBusConnection::framesWritten);
    connect(mDev_p, &QCanBusDevice::framesReceived, this, &SerialBusConnection::framesReceived);

    connect(&mTimer, SIGNAL(timeout()), this, SLOT(testConnection()));
    mTimer.setInterval(1000);
    mTimer.setSingleShot(false); //keep ticking
    mTimer.start();
    mBusData[0].mBus.setActive(true);
    mBusData[0].mBus.setCanFD(false);
    mBusData[0].mConfigured = true;
}


void SerialBusConnection::piSuspend(bool pSuspend)
{
    /* update capSuspended */
    setCapSuspended(pSuspend);

    /* flush queue if we are suspended */
    if(isCapSuspended())
        getQueue().flush();
}


void SerialBusConnection::piStop() {
    qDebug() << "piStop()";
    mTimer.stop();
    disconnectDevice();
}


bool SerialBusConnection::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}


void SerialBusConnection::piSetBusSettings(int pBusIdx, CANBus bus)
{
    quint32 sbusconfig = 0;

    //CANConStatus stats;
    /* sanity checks */
    if(0 != pBusIdx)
        return;

    if (!mDev_p) return;

    /* disconnect device if we have one connected */
    disconnectDevice();

    /* copy bus config */
    setBusConfig(0, bus);

    /* if bus is not active we are done */
    if(!bus.isActive())
    {
        /* Say so straight away rather than leaving the window showing Connected until the watchdog
         * next runs. Re-enabling the bus brings this back to Connected below. */
        setStatus(CANCon::NOT_CONNECTED);
        sendStatus();
        return;
    }

    /* set configuration */
    /*if (p.useConfigurationEnabled) {
     foreach (const SettingsDialog::ConfigurationItem &item, p.configurations)
         mDev->setConfigurationParameter(item.first, item.second);
    }*/

    //You cannot set the speed of a socketcan interface, it has to be set with console commands.
    //But, you can probabaly set the speed of many of the other serialbus devices so go ahead and try
    mDev_p->setConfigurationParameter(QCanBusDevice::BitRateKey, bus.getSpeed());
    mDev_p->setConfigurationParameter(QCanBusDevice::CanFdKey, bus.isCanFD());

    if(bus.isListenOnly())
        sbusconfig |= EN_SILENT_MODE;
    mDev_p->setConfigurationParameter(QCanBusDevice::UserKey, sbusconfig);

    /* connect device */
    if (!mDev_p->connectDevice()) {
        disconnectDevice();
        qDebug() << "can't connect device";
        setStatus(CANCon::NOT_CONNECTED);
    }
    else
    {
        //the bus is enabled and the device took it, so we are on the bus again
        setStatus(CANCon::CONNECTED);
    }
    sendStatus();
}


bool SerialBusConnection::piSendFrame(const CANFrame& pFrame)
{
    /* sanity checks */
    if(0 != pFrame.bus /*|| pFrame.len>8*/)
        return false;
    if (!mDev_p) return false;

    return mDev_p->writeFrame(pFrame);
}


/***********************************/
/****   private methods         ****/
/***********************************/


/* disconnect device */
void SerialBusConnection::disconnectDevice() {
    if(mDev_p) {
        mDev_p->disconnectDevice();
    }
}

void SerialBusConnection::sendStatus() {
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = mNumBuses;
    emit status(stats);
}


void SerialBusConnection::errorReceived(QCanBusDevice::CanBusError error) const
{
    switch (error) {
        case QCanBusDevice::ReadError:
        case QCanBusDevice::WriteError:
        case QCanBusDevice::ConnectionError:
        case QCanBusDevice::ConfigurationError:
        case QCanBusDevice::UnknownError:
        qWarning() << mDev_p->errorString();
        break;
    default:
        break;
    }
}

void SerialBusConnection::framesWritten(qint64 count)
{
    Q_UNUSED(count);
    //qDebug() << "Number of frames written:" << count;
}

void SerialBusConnection::framesReceived()
{
    uint64_t timeBasis = CANConManager::getInstance()->getTimeBasis();

    /* sanity checks */
    if(!mDev_p)
        return;

    /* read frame */
    while(true)
    {
        const QCanBusFrame recFrame = mDev_p->readFrame();

        /* exit case */
        if(!recFrame.isValid())
            break;

        /* drop frame if capture is suspended */
        if(isCapSuspended())
            continue;

        /* check frame */
        //if (recFrame.payload().length() <= 8) {
        if (true) {
            CANFrame* frame_p = getQueue().get();
            if(frame_p) {
                frame_p->setPayload(recFrame.payload());
                frame_p->bus = 0;
                if (recFrame.frameType() == recFrame.ErrorFrame)
                {
                    frame_p->setExtendedFrameFormat(recFrame.hasExtendedFrameFormat());
                    frame_p->setFrameId(recFrame.frameId() + 0x20000000ull);
	            frame_p->isReceived = true;
                }
                else
                {
                    frame_p->setExtendedFrameFormat(recFrame.hasExtendedFrameFormat());
                    frame_p->setFrameId(recFrame.frameId());
                }
                frame_p->setTimeStamp(recFrame.timeStamp());
                frame_p->setFrameType(recFrame.frameType());
                frame_p->setError(recFrame.error());
                /* If recorded frame has a local echo, it is a Tx message, and thus should not be marked as Rx */
                frame_p->isReceived = !recFrame.hasLocalEcho();

                if (useSystemTime) {
                    frame_p->setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ul));
                }
                else frame_p->setTimeStamp(QCanBusFrame::TimeStamp(0, (recFrame.timeStamp().seconds() * 1000000ul + recFrame.timeStamp().microSeconds()) - timeBasis));

                checkTargettedFrame(*frame_p);

                /* enqueue frame */
                getQueue().queue();
            }
            else
                qDebug() << "can't get a frame, ERROR";
        }
    }
}


/*
 * Watchdog, once a second. It keeps the reported status in step with what the device is actually
 * doing and brings the bus back if it drops out on its own.
 *
 * Two things this must NOT do, both of which it used to. It must not stop its own timer when the
 * device goes down: that left the connection permanently stuck reporting Not Connected, because the
 * only code that can report Connected again is this function. And it must not force the bus active
 * when reconnecting: a bus the user deliberately turned off is supposed to stay off, otherwise
 * unticking "Enable Bus" is undone a second later.
 */
void SerialBusConnection::testConnection() {
    //anything other than Unconnected counts as up, so a device still connecting isn't torn down
    const bool deviceDown = (!mDev_p || mDev_p->state() == QCanBusDevice::UnconnectedState);

    CANBus bus;
    const bool haveConfig = getBusConfig(0, bus);
    //did the user ask for this bus to be on?
    const bool busWanted = haveConfig && bus.isActive();

    switch(getStatus())
    {
        case CANCon::CONNECTED:
            if (deviceDown) {
                /* Either we lost the device or the user just turned the bus off. Report it either
                 * way, but keep this timer running so we can notice it coming back. */
                setStatus(CANCon::NOT_CONNECTED);
                sendStatus();
            }
            break;

        case CANCon::NOT_CONNECTED:
            if (!deviceDown) {
                //the device came up, most likely because the bus was just re-enabled
                setStatus(CANCon::CONNECTED);
                sendStatus();
            }
            else if (busWanted && mDev_p) {
                /* The user wants this bus up but the device isn't. Try to bring it back - the
                 * settings path reports the resulting status itself, so nothing to do here. This
                 * is also how the very first connection comes up after piStarted. */
                setBusSettings(0, bus);
            }
            break;

        default: {}
    }
}
