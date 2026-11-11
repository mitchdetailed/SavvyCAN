#ifndef CANCONNECTIONMODEL_H
#define CANCONNECTIONMODEL_H


#include <QAbstractTableModel>
#include <QVector>
#include <QHash>

#include "canbus.h"

class CANConnection;

class CANConnectionModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit CANConnectionModel(QObject *parent = 0);
    virtual ~CANConnectionModel();

    // from abstractmodel:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    void add(CANConnection* pConn_p);
    void remove(CANConnection* pConn_p);
    void replace(int idx , CANConnection* pConn_p);
    void swap(int idx1, int idx2);

    CANConnection* getAtIdx(int) const;
    void refresh(int pIndex=-1);

    /* Whether a device's buses are switched on, and the means to switch them. A device counts as
     * active while any one of its buses is.
     *
     * The answer is sampled rather than asked for on demand: getBusSettings() marshals into the
     * connection's own thread and blocks until it answers, which is far too expensive to do from
     * data() every time the table paints. updateActiveStates() re-reads it whenever it could have
     * moved, which is on a toggle here and on any change to the list of connections. */
    bool isConnActive(int pIdx) const;
    void setConnActive(int pIdx, bool pActive);
    void updateActiveStates();

    /* Which buses of a switched off device were on before it was switched off.
     *
     * Without this the button would be destructive on a multi bus device: a GVRET reports its own
     * per bus configuration, so one running CAN0 only would come back from a Deactivate with all
     * three buses enabled, and enabling a controller on a live vehicle bus is not something to do
     * by accident. Empty for a device that is switched on. ConnectionWindow saves and restores it
     * so a restart does not lose the pattern either. */
    QVector<bool> busMask(CANConnection *pConn_p) const;
    void setBusMask(CANConnection *pConn_p, const QVector<bool> &pMask);

private:
    void sampleActiveStates();

    QVector<bool> mActive;
    QHash<CANConnection*, QVector<bool>> mBusMask;
};

#endif // CANCONNECTIONMODEL_H
