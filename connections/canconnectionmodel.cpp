#include "canconnectionmodel.h"
#include "connections/canconnection.h"
#include "connections/canconmanager.h"

CANConnectionModel::CANConnectionModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

CANConnectionModel::~CANConnectionModel()
{
}

enum class Column {
    Type       = 0, ///< The CAN driver/backend type, e.g. GVRET, peakcan, or socketcan
    Subtype    = 1, ///< Mostly used by SerialBus devices to pick the sub type
    Port       = 2, ///< The CAN hardware port, e.g. can0 for socketcan
    NumBuses   = 3, ///< Number of buses exposed by this device. Usually non-GVRET devices will just have one
    Status     = 4, ///< The bus status as text message
    Active     = 5  ///< Carries the label for the Activate / Deactivate button drawn on each row
};

QVariant CANConnectionModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal)
    {
        switch (Column(section))
        {
        case Column::Type:
            return QString(tr("Type"));
        case Column::Subtype:
            return QString(tr("Subtype"));
        case Column::Port:
            return QString(tr("Port"));
        case Column::NumBuses:
            return QString(tr("Buses"));
        case Column::Status:
            return QString(tr("Status"));
        case Column::Active:
            return QString(tr("Active"));
        }
    }

    else
        return QString::number(section + 1);

    return QVariant();
}

int CANConnectionModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 6;
}


int CANConnectionModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    return conns.size();
}

QVariant CANConnectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    //qDebug() << "Row: " << index.row();

    CANConnection *conn_p = getAtIdx(index.row());

    if (!conn_p) return QVariant();

    //bool isSocketCAN = (conn_p->getType() == CANCon::SERIALBUS) ? true: false;

    if (role == Qt::DisplayRole) {

        switch (Column(index.column()))
        {
            case Column::Type:
                if (conn_p)
                    switch (conn_p->getType()) {
                        case CANCon::KVASER: return "KVASER";
                        case CANCon::SERIALBUS: return "SerialBus";
                        case CANCon::GVRET_SERIAL: return "GVRET";
                        case CANCon::KAYAK: return "socketcand";
                        case CANCon::LAWICEL: return "LAWICEL";
                        case CANCon::CANSERVER: return "CANserver";
                        case CANCon::CANLOGSERVER: return "CanLogServer";
                        case CANCon::GSUSB: return "GSUSB";
                        case CANCon::SEEEDSTUDIO: return "Seeed USB-CAN";
                        case CANCon::ROBOTELL: return "Robotell";
                        case CANCon::PYCAN_SERIAL: return "python-can serial";
                        case CANCon::UDP_MULTICAST: return "UDP multicast";
                        case CANCon::CANALYSTII: return "CANalyst-II";
                        case CANCon::IXXAT: return "IXXAT";
                        case CANCon::USB2CAN: return "USB2CAN";
                        case CANCon::ISCAN: return "isCAN";
                        case CANCon::NICAN: return "NI-CAN";
                        case CANCon::NEOUSYS: return "Neousys";
                        case CANCon::CANTRIPLE: return "CAN Triple";
                        default: {}
                    }
                else qDebug() << "Tried to show connection type but connection was nullptr";
                break;
            case Column::Port:
                if (conn_p) return conn_p->getPort();
                else qDebug() << "Tried to show connection port but connection was nullptr";
                break;
            case Column::Subtype:
                return conn_p->getDriver();
                break;
            case Column::NumBuses:
                return conn_p->getNumBuses();
                break;
            case Column::Status:
                 return (conn_p->getStatus()==CANCon::CONNECTED) ? "Connected" : "Not Connected";
            case Column::Active:
                /* This is the caption of a button, so it says what pressing it will do rather than
                 * what the state currently is. The Status column already reports the state. */
                return mActive.value(index.row(), false) ? QString(tr("Deactivate")) : QString(tr("Activate"));
        }
    }
    return QVariant();
}

bool CANConnectionModel::isConnActive(int pIdx) const
{
    return mActive.value(pIdx, false);
}

QVector<bool> CANConnectionModel::busMask(CANConnection *pConn_p) const
{
    /* Only answer for a device that is still in the list. The record is keyed by pointer and the
     * connection window's destructor deletes connections without going through the model, so a
     * stale key could otherwise be matched by a later connection that landed on the same address
     * and hand it a dead device's bus pattern. */
    if (!CANConManager::getInstance()->getConnections().contains(pConn_p)) return QVector<bool>();

    return mBusMask.value(pConn_p);
}

void CANConnectionModel::setBusMask(CANConnection *pConn_p, const QVector<bool> &pMask)
{
    if (!pConn_p) return;

    /* No repaint needed: the button's caption comes from whether the buses are on, not from this.
     * The record only says what to put back when they are turned on again. */
    if (pMask.contains(true)) mBusMask.insert(pConn_p, pMask);
    else mBusMask.remove(pConn_p);
}

/*
 * Switch a whole device on or off.
 *
 * Switching off is easy - every bus goes inactive. Switching back on is where the care is needed,
 * because "which buses were on" is not recoverable from a device whose buses are all off. A GVRET
 * hands us its own per bus configuration and plenty of them run CAN0 only, so simply turning every
 * bus on would put a controller on a bus the user never enabled. What was on gets remembered on
 * the way down and put back on the way up; only a device we have no record for comes up with
 * everything enabled.
 */
void CANConnectionModel::setConnActive(int pIdx, bool pActive)
{
    CANConnection *conn_p = getAtIdx(pIdx);
    if (!conn_p) return;

    const int numBuses = conn_p->getNumBuses();

    if (!pActive)
    {
        QVector<bool> mask;
        mask.reserve(numBuses);
        for (int i = 0; i < numBuses; i++)
        {
            CANBus bus;
            mask.append(conn_p->getBusSettings(i, bus) && bus.isActive());
        }

        //a device that was already off in every bus tells us nothing worth keeping
        if (mask.contains(true)) mBusMask.insert(conn_p, mask);
    }

    const QVector<bool> mask = pActive ? mBusMask.value(conn_p) : QVector<bool>();

    for (int i = 0; i < numBuses; i++)
    {
        CANBus bus;
        if (!conn_p->getBusSettings(i, bus)) continue;

        /* No record at all means switch everything on. A record we do have is taken at its word,
         * including for buses it does not mention - a device's bus count moves under us, GVRET
         * reports its own and drops to 2 on every connect before saying otherwise, so the record
         * and the device can genuinely disagree on how many buses there are. Assuming "on" for the
         * difference is the one answer that can put a controller on a bus nobody enabled, so the
         * mask is read entry by entry and anything it is silent about stays off. */
        bus.setActive(pActive && (mask.isEmpty() || mask.value(i, false)));
        conn_p->setBusSettings(i, bus);
    }

    //the record only describes a device that is currently off
    if (pActive) mBusMask.remove(conn_p);

    updateActiveStates();
}

//re-read every device's on/off state into the cache the Active column paints from
void CANConnectionModel::sampleActiveStates()
{
    const int rows = rowCount();
    mActive.resize(rows);

    for (int row = 0; row < rows; row++)
    {
        bool active = false;
        CANConnection *conn_p = getAtIdx(row);

        if (conn_p)
        {
            for (int i = 0; i < conn_p->getNumBuses(); i++)
            {
                CANBus bus;
                if (conn_p->getBusSettings(i, bus) && bus.isActive())
                {
                    active = true;
                    break;
                }
            }
        }

        mActive[row] = active;
    }
}

void CANConnectionModel::updateActiveStates()
{
    const QVector<bool> before = mActive;
    sampleActiveStates();

    /* A different number of rows means a device was added or removed, and whoever did that is
     * already inside a model reset - repainting single cells here would be wrong. */
    if (before.size() != mActive.size()) return;

    for (int row = 0; row < mActive.size(); row++)
    {
        if (before[row] == mActive[row]) continue;
        const QModelIndex idx = index(row, int(Column::Active));
        emit dataChanged(idx, idx, {Qt::DisplayRole});
    }
}


void CANConnectionModel::add(CANConnection* pConn_p)
{
    CANConManager* manager = CANConManager::getInstance();

    beginResetModel();
    manager->add(pConn_p);
    sampleActiveStates();
    endResetModel();
}


void CANConnectionModel::remove(CANConnection* pConn_p)
{
    CANConManager* manager = CANConManager::getInstance();

    //the device is going away, so anything remembered about its buses goes with it
    mBusMask.remove(pConn_p);

    beginResetModel();
    manager->remove(pConn_p);
    sampleActiveStates();
    endResetModel();
}

void CANConnectionModel::replace(int idx , CANConnection* pConn_p)
{
    CANConManager* manager = CANConManager::getInstance();

    //the old object is about to be discarded. A caller that wants to carry its bus record over
    //to the replacement reads it first and puts it back with setBusMask().
    mBusMask.remove(getAtIdx(idx));

    beginResetModel();
    manager->replace(idx, pConn_p);
    sampleActiveStates();
    endResetModel();
}

void CANConnectionModel::swap(int idx1, int idx2)
{
    CANConManager* manager = CANConManager::getInstance();

    beginResetModel();
    manager->swap(idx1, idx2);
    sampleActiveStates();
    endResetModel();
}

CANConnection* CANConnectionModel::getAtIdx(int pIdx) const
{
    if (pIdx < 0)
        return nullptr;

    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    //the list can shrink between a view giving us a row and us being asked about it
    if (pIdx >= conns.count())
        return nullptr;

    return conns.at(pIdx);
}

void CANConnectionModel::refresh(int pIndex)
{
    Q_UNUSED(pIndex)

    beginResetModel();
    sampleActiveStates();
    endResetModel();
    /*
    QModelIndex begin;
    QModelIndex end;

    if(pIndex>=0) {
        begin   = createIndex(pIndex, 0);
        end     = createIndex(pIndex, columnCount()-1);
    }
    else {
        begin   = createIndex(0, 0);
        end     = createIndex(rowCount()-1, columnCount()-1);
    }
    dataChanged(begin, end, QVector<int>(Qt::DisplayRole)); */
}
