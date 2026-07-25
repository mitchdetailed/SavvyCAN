#include <QDateTime>
#include <QSettings>
#include <QCoreApplication>

#include "canconmanager.h"
#include "canconfactory.h"

CANConManager* CANConManager::mInstance = nullptr;

CANConManager* CANConManager::getInstance()
{
    if(!mInstance)
        mInstance = new CANConManager();

    return mInstance;
}

CANConManager::CANConManager(QObject *parent): QObject(parent)
{
    connect(&mTimer, SIGNAL(timeout()), this, SLOT(refreshCanList()));
    mTimer.setInterval(20); /*Tick 50 times per second to allow for good resolution in reception where needed. GUI updates *MUCH* more slowly*/
    mTimer.setSingleShot(false);
    mTimer.start();

    mNumActiveBuses = 0;

    resetTimeBasis();

    QSettings settings;

    if (settings.value("Main/TimeClock", false).toBool())
    {
        useSystemTime = true;
    }
    else useSystemTime = false;
}

void CANConManager::resetTimeBasis()
{
    mTimestampBasis = QDateTime::currentMSecsSinceEpoch() * 1000;
    mElapsedTimer.restart();
}

CANConManager::~CANConManager()
{
    mTimer.stop();
    mInstance = nullptr;
}

void CANConManager::stopAllConnections()
{
    foreach (CANConnection *conn, mConns)
    {
        conn->stop();
    }
}

void CANConManager::add(CANConnection* pConn_p)
{
    QMutexLocker locker(&mConnsMutex);
    mConns.append(pConn_p);
}


void CANConManager::remove(CANConnection* pConn_p)
{
    //disconnect(pConn_p, 0, this, 0);
    QMutexLocker locker(&mConnsMutex);
    mConns.removeOne(pConn_p);
}

void CANConManager::replace(int idx, CANConnection* pConn_p)
{
    QMutexLocker locker(&mConnsMutex);
    if (idx < 0 || idx >= mConns.size()) return;
    CANConnection *original = mConns[idx];
    mConns.replace(idx, pConn_p);
    original->deleteLater(); original = NULL;
}

//Get total number of buses currently registered with the program
int CANConManager::getNumBuses()
{
    int buses = 0;
    foreach(CANConnection* conn_p, mConns)
    {
        buses += conn_p->getNumBuses();
    }
    return buses;
}

int CANConManager::getBusBase(CANConnection *which)
{
    int buses = 0;
    foreach(CANConnection* conn_p, mConns)
    {
        if (conn_p != which) buses += conn_p->getNumBuses();
        else return buses;
    }
    return -1;
}

void CANConManager::refreshCanList()
{
    QObject* sender_p = QObject::sender();

    if (mConns.size() == 0)
    {
        tempFrames.clear();
        buslessMutex.lock();
        if(buslessFrames.size()) {            
            tempFrames = buslessFrames; //make a copy and pass that copy
            buslessFrames.clear(); //delete all frames from the original
        }
        buslessMutex.unlock();
        if(tempFrames.size()) {
            emit framesReceived(nullptr, tempFrames);
        }
        return;
    }

    if( sender_p != &mTimer)
    {
        /* if we are not the sender, the signal is coming from a connection */
        /* refresh only the given connection */
        if(mConns.contains((CANConnection*) sender_p))
            refreshConnection((CANConnection*)sender_p);
    }
    else
    {
        foreach (CANConnection* conn_p, mConns)
            refreshConnection((CANConnection*)conn_p);
    }

    /* Rates have to be recalculated on the clock, not on frame arrival. Doing it inside
     * refreshConnection meant an idle bus never got here at all (that function returns early when
     * its queue is empty), so a quiet bus showed nothing and a bus that went quiet kept displaying
     * whatever rate it last had. */
    updateStatsRates();
}

uint64_t CANConManager::getTimeBasis()
{
    return mTimestampBasis;
}

QList<CANConnection*>& CANConManager::getConnections()
{
    return mConns;
}


CANConnection* CANConManager::getByName(const QString& pName) const
{
    foreach(CANConnection* conn_p, mConns)
    {
        if(conn_p->getPort() == pName)
            return conn_p;
    }

    return nullptr;
}


void CANConManager::refreshConnection(CANConnection* pConn_p)
{
    unsigned int buses = 0;
    foreach(CANConnection* conn_p, mConns)
    {
        if (conn_p->getStatus() == CANCon::CONNECTED) buses += conn_p->getNumBuses();
    }
    if (buses != mNumActiveBuses)
    {
        mNumActiveBuses = buses;
        emit connectionStatusUpdated(buses);
    }

    if (pConn_p->getQueue().peek() == nullptr) return;

    CANFrame* frame_p = nullptr;
    QVector<CANFrame> frames;

    //Each connection only knows about its own bus numbers
    //so this variable is used to fix that up to turn local bus numbers
    //into system global bus numbers for display.
    int busBase = 0;

    foreach (CANConnection* conn, mConns)
    {
        if (conn != pConn_p) busBase += conn->getNumBuses();
        else break;
    }

    //qDebug() << "Bus fixup number: " << busBase;

    while( (frame_p = pConn_p->getQueue().peek() ) ) {
        frame_p->bus += busBase;
        //qDebug() << "Rx of frame from bus: " << frame_p->bus;
        frames.append(*frame_p);
        pConn_p->getQueue().dequeue();
    }

    if(frames.size())
    {
        accumulateStats(frames);
        emit framesReceived(pConn_p, frames);
    }
}

/*
 * Count frames and bits per bus. Bus load is worked out from the frame's own bit count rather than
 * asked of the hardware, which means it works for every connection type - including socketcand,
 * MQTT and the python-can transports where no controller is reachable to ask.
 */
void CANConManager::accumulateStats(const QVector<CANFrame> &frames)
{
    QMutexLocker locker(&mStatsMutex);

    foreach (const CANFrame &frame, frames)
    {
        const int bus = frame.bus;
        if (bus < 0 || bus > 255) continue; //nonsense bus number, don't grow the vector for it
        if (bus >= mBusStats.size()) mBusStats.resize(bus + 1);

        CANBusStats &stats = mBusStats[bus];

        if (frame.isReceived) stats.framesReceived++;
        else stats.framesSent++;
        stats.framesThisPeriod++;

        //SavvyCAN marks an error frame by setting bit 29 of the ID
        if (frame.frameId() & 0x20000000) stats.errorFrames++;

        /* Frame length on the wire: 44 bits of overhead for a standard frame, 64 for extended,
         * plus the payload. Bit stuffing adds up to a fifth on top in the worst case, so apply the
         * usual 1.2 factor - this is an estimate and is presented as one. */
        const int payloadBits = frame.payload().length() * 8;
        const int overhead = frame.hasExtendedFrameFormat() ? 64 : 44;
        stats.bitsThisPeriod += (uint64_t)((overhead + payloadBits) * 1.2);
    }
}

//once a second turn the accumulated counts into a frame rate and a bus load percentage
void CANConManager::updateStatsRates()
{
    /* Give every bus that exists an entry, whether or not it has ever carried a frame. Without
     * this a connected but silent bus has no statistics at all and the health panel has nothing
     * to show - which reads as "broken" rather than the "quiet" it actually is. */
    {
        QMutexLocker locker(&mStatsMutex);
        const int totalBuses = getNumBuses();
        if (mBusStats.size() < totalBuses) mBusStats.resize(totalBuses);
    }

    if (!mStatsTimer.isValid())
    {
        mStatsTimer.start();
        return;
    }

    const qint64 elapsed = mStatsTimer.elapsed();
    if (elapsed < 1000) return;

    /* Collect the bus speeds first, before taking the stats lock. getBusSettings marshals into the
     * connection's worker thread with a blocking call, and holding a mutex across that would tie
     * the statistics to whatever a driver happens to be doing. */
    QVector<int> busSpeeds;
    int busBase = 0;
    foreach (CANConnection *conn, mConns)
    {
        for (int i = 0; i < conn->getNumBuses(); i++)
        {
            CANBus bus;
            const int speed = conn->getBusSettings(i, bus) ? bus.getSpeed() : 0;
            if (busBase + i >= busSpeeds.size()) busSpeeds.resize(busBase + i + 1);
            busSpeeds[busBase + i] = speed;
        }
        busBase += conn->getNumBuses();
    }

    QMutexLocker locker(&mStatsMutex);

    const double seconds = (double)elapsed / 1000.0;
    for (int i = 0; i < mBusStats.size(); i++)
    {
        CANBusStats &stats = mBusStats[i];
        const int speed = (i < busSpeeds.size()) ? busSpeeds[i] : 0;

        stats.frameRate = (int)((double)stats.framesThisPeriod / seconds);

        if (speed > 0)
            stats.busLoadPercent = qMin(100.0, ((double)stats.bitsThisPeriod / seconds) / (double)speed * 100.0);
        else
            stats.busLoadPercent = 0.0; //without a configured speed a percentage is meaningless

        stats.framesThisPeriod = 0;
        stats.bitsThisPeriod = 0;
    }

    mStatsTimer.restart();
}

bool CANConManager::getBusStats(int busNum, CANBusStats &stats)
{
    QMutexLocker locker(&mStatsMutex);

    if (busNum < 0 || busNum >= mBusStats.size()) return false;
    stats = mBusStats[busNum];
    return true;
}

void CANConManager::resetBusStats()
{
    QMutexLocker locker(&mStatsMutex);
    for (int i = 0; i < mBusStats.size(); i++) mBusStats[i].reset();
    mStatsTimer.restart();
}

/*
 * Uses the requested bus to look up which CANConnection object handles this bus based on the order of
 * the objects and how many buses they implement. For instance, if the request is to send on bus 2
 * and there is a GVRET object first then a socketcan object it'll send on the socketcan object as
 * gvret will have claimed buses 0 and 1 and socketcan bus 2. But, each actual CANConnection expects
 * its own bus numbers to start at zero so the frame bus number has to be offset accordingly.
 * Also keep in mind that the CANConnection "sendFrame" function uses a blocking queued connection
 * and so will force the frame to be delivered before it keeps going. This allows on the stack variables
 * to be used but is slow. This function uses an on the stack copy of the frame so the way it works
 * is a good thing but performance will suffer. TODO: Investigate a way to use non-blocking calls.
*/
bool CANConManager::sendFrame(const CANFrame& pFrame)
{
    int busBase = 0;
    CANFrame workingFrame = pFrame;

    /* This runs on the frame sender's thread while the GUI thread adds and removes connections,
     * so the walk over the list has to be locked or a removal mid-send is a use-after-free. */
    QMutexLocker locker(&mConnsMutex);

    if (mConns.size() == 0)
    {
        buslessMutex.lock();
        buslessFrames.append(pFrame);
        buslessMutex.unlock();
        return true;
    }

    foreach (CANConnection* conn, mConns)
    {
        //check if this CAN connection is supposed to handle the requested bus
        if (pFrame.bus < (busBase + conn->getNumBuses()))
        {
            workingFrame.bus -= busBase;
            workingFrame.isReceived = false;
            if (useSystemTime)
            {
                workingFrame.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ul));
            }
            else
            {
                workingFrame.setTimeStamp(QCanBusFrame::TimeStamp(0, mElapsedTimer.nsecsElapsed() / 1000));
                //workingFrame.timestamp -= mTimestampBasis;
            }

            return conn->sendFrame(workingFrame);
        }
        busBase += conn->getNumBuses();
    }
    return false;
}

bool CANConManager::sendFrames(const QList<CANFrame>& pFrames)
{
    foreach(const CANFrame& frame, pFrames)
    {
        if(!sendFrame(frame))
            return false;
    }

    return true;
}

//For each device associated with buses go through and see if that device has a bus
//that the filter should apply to. If so forward the data on but fudge
//the bus numbers if bus wasn't -1 so that they're local to the device
bool CANConManager::addTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver)
{
    //int tempBusVal;
    int busBase = 0;

    foreach (CANConnection* conn, mConns)
    {
        if (pBusId == -1) conn->addTargettedFrame(pBusId, ID, mask, receiver);
        else if (pBusId < (busBase + conn->getNumBuses()))
        {
            qDebug() << "Forwarding targetted frame setting to a connection object";
            conn->addTargettedFrame(pBusId - busBase, ID, mask, receiver);

        }
        busBase += conn->getNumBuses();
    }
    return true;
}

bool CANConManager::removeTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver)
{
    //int tempBusVal;
    int busBase = 0;

    foreach (CANConnection* conn, mConns)
    {
        if (pBusId == -1) conn->removeTargettedFrame(pBusId, ID, mask, receiver);
        else if (pBusId < (busBase + conn->getNumBuses()))
        {
            qDebug() << "Forwarding targetted frame setting to a connection object";
            conn->removeTargettedFrame(pBusId - busBase, ID, mask, receiver);

        }
        busBase += conn->getNumBuses();
    }
    return true;
}

bool CANConManager::removeAllTargettedFrames(QObject *receiver)
{
    foreach (CANConnection* conn, mConns)
    {
        conn->removeAllTargettedFrames(receiver);
    }

    return true;
}
