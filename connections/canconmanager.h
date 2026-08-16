#ifndef CANCONMANAGER_H
#define CANCONMANAGER_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>

#include "canconnection.h"

class CANConManager : public QObject
{
    Q_OBJECT

public:
    static CANConManager* getInstance();
    virtual ~CANConManager();

    void add(CANConnection* pConn_p);
    void remove(CANConnection* pConn_p);
    void replace(int idx, CANConnection* pConn_p);
    void swap(int idx1, int idx2);
    QList<CANConnection*>& getConnections();
    void stopAllConnections();

    CANConnection* getByName(const QString& pName) const;

    uint64_t getTimeBasis();
    void resetTimeBasis();

    int getNumBuses();
    int getBusBase(CANConnection *);

    /**
     * @brief Health figures for a global bus number, as shown in the connection window.
     * @param busNum - global bus number, the same numbering frames carry
     * @param stats - filled in on success
     * @return false if that bus doesn't exist
     */
    bool getBusStats(int busNum, CANBusStats &stats);

    //start the counters over, e.g. after reconnecting
    void resetBusStats();

    /**
     * @brief sendFrame sends a single frame out the desired bus
     * @param pFrame - reference to a CANFrame struct that has been filled out for sending
     * @return bool specifying whether the send succeeded or not
     * @note Finds which CANConnection object is responsible for this bus and automatically converts bus number to pass properly to CANConnection
     */
    bool sendFrame(const CANFrame& pFrame);

    //just the multi-frame version of above function.
    bool sendFrames(const QList<CANFrame>& pFrames);

    /**
     * @brief Add a new filter for the targetted frames. If a frame matches it will immediately be sent via the targettedFrameReceived signal
     * @param pBusId - Which bus to bond to. -1 for any, otherwise a bitfield of buses (but 0 = first bus, etc)
     * @param ID - 11 or 29 bit ID to match against
     * @param mask - 11 or 29 bit mask used for filter
     * @param receiver - Pointer to a QObject that wants to receive notification when filter is matched
     * @return true if filter was able to be added, false otherwise.
     */
    bool addTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver);

    /**
     * @brief Try to find a matching filter in the list and remove it, no longer targetting those frames
     * @param pBusId - Which bus to bond to. Doesn't have to match the call to addTargettedFrame exactly. You could disconnect just one bus for instance.
     * @param ID - 11 or 29 bit ID to match against
     * @param mask - 11 or 29 bit mask used for filter
     * @param receiver - Pointer to a QObject that wants to receive notification when filter is matched
     * @return true if filter was found and deleted, false otherwise.
     */
    bool removeTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver);

    bool removeAllTargettedFrames(QObject *receiver);

signals:
    void framesReceived(CANConnection* pConn_p, QVector<CANFrame>& pFrames);
    void connectionStatusUpdated(int conns);

private slots:
    void refreshCanList();

private:
    explicit CANConManager(QObject *parent = 0);
    void refreshConnection(CANConnection* pConn_p);

    static CANConManager*  mInstance;
    QList<CANConnection*>  mConns;
    QTimer                 mTimer;
    QElapsedTimer          mElapsedTimer;
    uint64_t               mTimestampBasis;
    uint32_t               mNumActiveBuses;
    bool                   useSystemTime;
    QMutex                 buslessMutex;
    /* guards mConns against the frame sender thread. The GUI thread mutates the list
     * (add/remove/replace) while FrameSenderObject calls sendFrame from its own thread - both
     * sides take this lock. Pure GUI thread readers (getConnections & friends) don't. */
    QMutex                 mConnsMutex;
    QVector<CANFrame>      buslessFrames;
    QVector<CANFrame>      tempFrames;
    //indexed by global bus number, grown as buses appear
    QVector<CANBusStats>   mBusStats;
    QMutex                 mStatsMutex;
    QElapsedTimer          mStatsTimer;
    //accumulate frames into the stats and, once a second, turn them into rates
    void accumulateStats(const QVector<CANFrame> &frames);
    void updateStatsRates();
};

#endif // CANCONNECTIONMODEL_H

