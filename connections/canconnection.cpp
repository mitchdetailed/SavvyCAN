#include <QSettings>
#include <QThread>
#include "canconnection.h"

CANConnection::CANConnection(QString pPort,
                             QString pDriver,
                             CANCon::type pType,
                             int pSerialSpeed,
                             int pBusSpeed,
                             bool pCanFd,
                             int pDataRate,
                             int pNumBuses,
                             int pQueueLen,
                             bool pUseThread) :
    mNumBuses(pNumBuses),
    mSerialSpeed(pSerialSpeed),
    mQueue(),
    mPort(pPort),
    mDriver(pDriver),
    mType(pType),
    mIsCapSuspended(false),
    mStatus(CANCon::NOT_CONNECTED),
    mStarted(false),
    mThread_p(nullptr)
{
    /* register types */
    qRegisterMetaType<CANBus>("CANBus");
    qRegisterMetaType<CANFrame>("CANFrame");
    qRegisterMetaType<CANConStatus>("CANConStatus");
    qRegisterMetaType<CANFltObserver>("CANFlt");

    /* set queue size */
    mQueue.setSize(pQueueLen); /*TODO add check on returned value */

    /* allocate buses */
    /* TODO: change those tables for a vector */
    mBusData.resize(mNumBuses);
    for(int i=0 ; i<mNumBuses ; i++) {
        mBusData[i].mConfigured  = false;
    }

    if (pBusSpeed > 0) mBusData[0].mBus.setSpeed(pBusSpeed);
    mBusData[0].mBus.setCanFD(pCanFd);
    if (pDataRate > 0) {
      mBusData[0].mBus.setDataRate(pDataRate);
      if (pCanFd) {
	mBusData[0].mBus.setCanFD(pCanFd);
      }
    }
 
    /* if needed, create a thread and move ourself into it */
    if(pUseThread) {
        mThread_p = new QThread();
    }
}


CANConnection::~CANConnection()
{
    /* stop and delete thread */
    if(mThread_p) {
        mThread_p->quit();
        mThread_p->wait();
        delete mThread_p;
        mThread_p = nullptr;
    }

    mBusData.clear();
}


void CANConnection::start()
{
    if( mThread_p && (mThread_p != QThread::currentThread()) )
    {
        /* move ourself to the thread */
        moveToThread(mThread_p); /*TODO handle errors */
        /* connect started() */
        connect(mThread_p, SIGNAL(started()), this, SLOT(start()));
        /* start the thread */
        mThread_p->start(QThread::HighPriority);
        return;
    }

    /* set started flag */
    mStarted = true;

    QSettings settings;

    if (settings.value("Main/TimeClock", false).toBool())
    {
        useSystemTime = true;
    }
    else useSystemTime = false;

    /* in multithread case, this will be called before entering thread event loop */
    return piStarted();
}


void CANConnection::suspend(bool pSuspend)
{
    /* execute in mThread_p context. If the thread is no longer running a blocking queued call
     * would never return, so in that case just run directly - the device is torn down anyway. */
    if( mThread_p && mThread_p->isRunning() && (mThread_p != QThread::currentThread()) ) {
        QMetaObject::invokeMethod(this, "suspend",
                                  Qt::BlockingQueuedConnection,
                                  Q_ARG(bool, pSuspend));
        return;
    }

    return piSuspend(pSuspend);
}


void CANConnection::stop()
{
    /* 1) execute in mThread_p context */
    if( mThread_p && mStarted && (mThread_p != QThread::currentThread()) )
    {
        /* if thread is finished, it means we call this function for the second time so we can leave */
        if( !mThread_p->isFinished() )
        {
            /* we need to call piStop() */
            QMetaObject::invokeMethod(this, "stop",
                                      Qt::BlockingQueuedConnection);
            /* 3) stop thread */
            mThread_p->quit();
            if(!mThread_p->wait()) {
                qDebug() << "can't stop thread";
            }
        }
        return;
    }

    /* 2) call piStop in mThread context */
    return piStop();
}


bool CANConnection::getBusSettings(int pBusIdx, CANBus& pBus)
{
    /* make sure we execute in mThread context - unless the thread is gone, in which case a
     * blocking queued call would hang forever and reading the stored config directly is safe */
    if( mThread_p && mThread_p->isRunning() && (mThread_p != QThread::currentThread()) ) {
        bool ret;
        QMetaObject::invokeMethod(this, "getBusSettings",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(int , pBusIdx),
                                  Q_ARG(CANBus& , pBus));
        return ret;
    }

    return piGetBusSettings(pBusIdx, pBus);
}


void CANConnection::setBusSettings(int pBusIdx, CANBus pBus)
{
    /* make sure we execute in mThread context - unless the thread is gone, in which case a
     * blocking queued call would hang forever. Every driver tolerates being poked while stopped. */
    if( mThread_p && mThread_p->isRunning() && (mThread_p != QThread::currentThread()) ) {
        QMetaObject::invokeMethod(this, "setBusSettings",
                                  Qt::BlockingQueuedConnection,
                                  Q_ARG(int, pBusIdx),
                                  Q_ARG(CANBus, pBus));
        return;
    }

    return piSetBusSettings(pBusIdx, pBus);
}


bool CANConnection::sendFrame(const CANFrame& pFrame)
{
    /* make sure we execute in mThread context - unless the thread is gone, in which case a
     * blocking queued call would hang the sender forever. Drivers check their handles anyway. */
    if( mThread_p && mThread_p->isRunning() && (mThread_p != QThread::currentThread()) )
    {
        bool ret;
        QMetaObject::invokeMethod(this, "sendFrame",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(const CANFrame&, pFrame));
        return ret;
    }

    CANFrame *txFrame;
    txFrame = getQueue().get();
    if (txFrame)
    {
        *txFrame = pFrame;
        /* only advance the write index when we actually got a slot. Advancing on a full queue
         * pushes the write index onto the read index which corrupts the whole ring. */
        getQueue().queue();
    }

    return piSendFrame(pFrame);
}


bool CANConnection::sendFrames(const QList<CANFrame>& pFrames)
{
    /* make sure we execute in mThread context - see sendFrame for why isRunning matters */
    if( mThread_p && mThread_p->isRunning() && (mThread_p != QThread::currentThread()) )
    {
        bool ret;
        QMetaObject::invokeMethod(this, "sendFrames",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(const QList<CANFrame>&, pFrames));
        return ret;
    }

    return piSendFrames(pFrames);
}


int CANConnection::getNumBuses() const{
    return mNumBuses;
}

int CANConnection::getSerialSpeed() const{
  return mSerialSpeed;
}


bool CANConnection::isConfigured(int pBusId) {
    if( pBusId < 0 || pBusId >= getNumBuses())
        return false;
    return mBusData[pBusId].mConfigured;
}

void CANConnection::setConfigured(int pBusId, bool pConfigured) {
    if( pBusId < 0 || pBusId >= getNumBuses())
        return;
    mBusData[pBusId].mConfigured = pConfigured;
}


bool CANConnection::getBusConfig(int pBusId, CANBus& pBus) {
    if( pBusId < 0 || pBusId >= getNumBuses() || !isConfigured(pBusId))
        return false;
    qDebug() << "getBusConfig id: " << pBusId;
    pBus = mBusData[pBusId].mBus;
    return true;
}


void CANConnection::setBusConfig(int pBusId, CANBus& pBus) {
    if( pBusId < 0 || pBusId >= getNumBuses())
        return;

    mBusData[pBusId].mConfigured = true;
    mBusData[pBusId].mBus = pBus;
}


QString CANConnection::getPort() {
    return mPort;
}

QString CANConnection::getDriver()
{
    return mDriver;
}

LFQueue<CANFrame>& CANConnection::getQueue() {
    return mQueue;
}


CANCon::type CANConnection::getType() {
    return mType;
}


CANCon::status CANConnection::getStatus() {
    return (CANCon::status) mStatus.loadRelaxed();
}

void CANConnection::setStatus(CANCon::status pStatus) {
    mStatus.storeRelaxed(pStatus);
}

bool CANConnection::isCapSuspended() {
    return mIsCapSuspended;
}

void CANConnection::setCapSuspended(bool pIsSuspended) {
    mIsCapSuspended = pIsSuspended;
}

void CANConnection::debugInput(QByteArray bytes) {
    Q_UNUSED(bytes)
}

bool CANConnection::addTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver)
{
/*
    if( mThread_p && (mThread_p != QThread::currentThread()) ) {
        bool ret;
        QMetaObject::invokeMethod(this, "addTargettedFrame",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(int, pBusId),
                                  Q_ARG(uint32_t , ID),
                                  Q_ARG(uint32_t , mask),
                                  Q_ARG(QObject *, receiver));
        return ret;
    }
*/
    /* sanity checks */
    if(pBusId < -1 || pBusId >=  getNumBuses())
        return false;

    qDebug() << "Connection is registering a new targetted frame filter, local bus " << pBusId;
    CANFltObserver target;
    target.id = ID;
    target.mask = mask;
    target.observer = receiver;
    if (pBusId > -1)
        mBusData[pBusId].mTargettedFrames.append(target);
    else
    {
        for (int i = 0; i < mBusData.size(); i++) mBusData[i].mTargettedFrames.append(target);
    }

    return true;
}

bool CANConnection::removeTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver)
{
/*
    if( mThread_p && (mThread_p != QThread::currentThread()) ) {
        bool ret;
        QMetaObject::invokeMethod(this, "removeTargettedFrame",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(int, pBusId),
                                  Q_ARG(uint32_t , ID),
                                  Q_ARG(uint32_t , mask),
                                  Q_ARG(QObject *, receiver));
        return ret;
    }
*/
    /* sanity checks */
    if(pBusId < -1 || pBusId >= getNumBuses())
        return false;

    CANFltObserver target;
    target.id = ID;
    target.mask = mask;
    target.observer = receiver;
    mBusData[pBusId].mTargettedFrames.removeAll(target);

    return true;
}

bool CANConnection::removeAllTargettedFrames(QObject *receiver)
{
    for (int i = 0; i < getNumBuses(); i++) {
        foreach (const CANFltObserver filt, mBusData[i].mTargettedFrames)
        {
            if (filt.observer == receiver) mBusData[i].mTargettedFrames.removeOne(filt);
        }
    }

    return true;
}

void CANConnection::checkTargettedFrame(CANFrame &frame)
{
    unsigned int maskedID;
    //qDebug() << "Got frame with ID " << frame.ID << " on bus " << frame.bus;
    if (mBusData.size() == 0) return;

    int bus = frame.bus;
    if (bus < 0) bus = 0;
    if (bus > (mBusData.length() - 1)) bus = mBusData.length() - 1;

    if (mBusData[bus].mTargettedFrames.length() == 0) return;
    foreach (const CANFltObserver filt, mBusData[bus].mTargettedFrames)
    {
        //qDebug() << "Checking filter with id " << filt.id << " mask " << filt.mask;
        maskedID = frame.frameId() & filt.mask;
        if (maskedID == filt.id) {
            qDebug() << "In connection object I got a targetted frame. Forwarding it.";
            QMetaObject::invokeMethod(filt.observer, "gotTargettedFrame",Qt::QueuedConnection, Q_ARG(CANFrame, frame));
        }
    }
}

bool CANConnection::piSendFrames(const QList<CANFrame>& pFrames)
{
    foreach(const CANFrame& frame, pFrames)
    {
        if(!piSendFrame(frame))
            return false;
    }

    return true;
}
