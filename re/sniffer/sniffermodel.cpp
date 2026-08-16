#include <QDebug>
#include <Qt>
#include <QApplication>
#include <algorithm>
#include "sniffermodel.h"
#include "snifferwindow.h"
#include "SnifferDelegate.h"

SnifferModel::SnifferModel(QObject *parent)
    : QAbstractItemModel(parent),
      mFilter(false),
      mNeverExpire(false),
      mFadeInactive(false),
      mMuteNotched(false),
      mTimeSequence(0),
      mExpireInterval(5000)
{
    QColor TextColor = QApplication::palette().color(QPalette::Text);
    if (TextColor.red() + TextColor.green() + TextColor.blue() < 200)
    {
        mDarkMode = false;
    }
    else mDarkMode = true;
}

SnifferModel::~SnifferModel()
{
    qDeleteAll(mMap);
    mMap.clear();
    mFilters.clear();
}

void SnifferModel::setExpireInterval(int newVal)
{
    mExpireInterval = newVal;
}

int SnifferModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tc::LAST+1;
}


int SnifferModel::rowCount(const QModelIndex &parent) const
{
    const QMap<quint32, SnifferItem*>& map = mFilter ? mFilters : mMap;
    return parent.isValid() ? 0 : map.size();
}


QVariant SnifferModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    SnifferItem *item = static_cast<SnifferItem*>(index.internalPointer());
    if(!item) QVariant();

    int col = index.column();

    switch(role)
    {
        case Qt::DisplayRole:
        {
            switch(col)
            {
                case tc::DELTA:
                    return QString::number(item->getDelta(), 'f');
                case tc::FREQUENCY:
                    if (item->getDelta() == 0) return QString("0 hz");
                    return QString("%1 hz").arg(qRound(1.00 / item->getDelta()));
                case tc::ID:
                    return "0x" + QString("%1").arg(item->getId(), 5, 16, QLatin1Char('0')).toUpper();
                default:
                    break;
            }
            if(tc::DATA_0<=col && col <=tc::DATA_7)
            {
                int data = item->getData(col-tc::DATA_0);
                if(data >= 0)
                {
                    return QString("%1").arg((uint8_t)data, 2, 16, QLatin1Char('0')).toUpper();
                }
            }
            break;
        }
        case Qt::ForegroundRole:
        {
            if (!mFadeInactive ||  col < 2) return QApplication::palette().brush(QPalette::Text);
            int v = item->getSeqInterval(col - 2) * 10;
            //qDebug() << "mTS: " << mTimeSequence << " gDT(" << (col - 2) << ") " << item->getDataTimestamp(col - 2);
            if (v > 225) v = 225;
            if (v < 0) v = 0;

            if (!mDarkMode) //text defaults to being dark
            {
                return QBrush(QColor(v,v,v,255));
            }
            else //text defaults to being light
            {
                return QBrush(QColor(255-v,255-v,255-v,255));
            }
        }

        case Qt::BackgroundRole:
        {
            if(tc::ID==col)
            {
                if(item->elapsed() > 4000)
                {
                    if (!mDarkMode) return QBrush(Qt::red);
                    return QBrush(QColor(128,0,0));
                }
            }
            else if(tc::DATA_0<=col && col<=tc::DATA_7)
            {
                dc change = item->dataChange(col-tc::DATA_0);
                switch(change)
                {
                    case dc::INC:
                        if (!mDarkMode) return QBrush(Qt::green);
                        return QBrush(QColor(0,128,0));
                    case dc::DEINC:
                        if (!mDarkMode) return QBrush(Qt::red);
                        return QBrush(QColor(128,0,0));
                    default:
                        return QApplication::palette().brush(QPalette::Base);
                }
            }
            break;
        }
    }

    return QVariant();
}


Qt::ItemFlags SnifferModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}


QVariant SnifferModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch(section)
        {
            case tc::DELTA:
                return QString("Delta");
            case tc::FREQUENCY:
                return QString("Frequency");
            case tc::ID:
                return QString("ID");
            default:
                break;
        }
        if(tc::DATA_0<=section && section <=tc::DATA_7)
            return QString::number(section-tc::DATA_0);
    }

    return QVariant();
}


QModelIndex SnifferModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid())
        return QModelIndex();

    const QVector<quint32>& keys = mFilter ? mFilteredKeys : mOrderedKeys;
    const QMap<quint32, SnifferItem*>& map = mFilter ? mFilters : mMap;

    if (column > tc::LAST || row >= keys.size())
        return QModelIndex();

    SnifferItem *item = map.value(keys[row], nullptr);
    if (!item) return QModelIndex();
    return createIndex(row, column, item);
}


QModelIndex SnifferModel::parent(const QModelIndex &) const
{
    return QModelIndex();
}

bool SnifferModel::getNeverExpire()
{
    return mNeverExpire;
}

bool SnifferModel::getFadeInactive()
{
    return mFadeInactive;
}

bool SnifferModel::getMuteNotched()
{
    return mMuteNotched;
}

void SnifferModel::setNeverExpire(bool val)
{
    mNeverExpire = val;
}

void SnifferModel::setFadeInactive(bool val)
{
    mFadeInactive = val;
}

void SnifferModel::setMuteNotched(bool val)
{
    mMuteNotched = val;
}

void SnifferModel::clear()
{
    beginResetModel();
    qDeleteAll(mMap);
    mMap.clear();
    mFilters.clear();
    mOrderedKeys.clear();
    mFilteredKeys.clear();
    mFilter = false;
    endResetModel();
}

void SnifferModel::updateNotchPoint()
{
    QMap<quint32, SnifferItem*>::iterator i;

    /* update markers */
    for (i = mMap.begin(); i != mMap.end(); ++i)
    {
        i.value()->updateMarker();
    }
}

//Called from window with a timer (currently 200ms)
void SnifferModel::refresh()
{
    QMap<quint32, SnifferItem*>::iterator i;
    QVector<quint32> toRemove;
    SnifferItem* item;

    mTimeSequence++;

    for (i = mMap.begin(); i != mMap.end(); ++i)
    {
        //i.value()->updateMarker();
        if(i.value()->elapsed() > (int)mExpireInterval && !mNeverExpire)
            toRemove.append(i.key());
    }

    if(toRemove.size())
    {
        beginResetModel();
        foreach(quint32 id, toRemove)
        {
            item = mMap.take(id);
            mFilters.remove(id);
            delete item;
            auto oit = std::lower_bound(mOrderedKeys.begin(), mOrderedKeys.end(), id);
            if (oit != mOrderedKeys.end() && *oit == id) mOrderedKeys.erase(oit);
            auto fit = std::lower_bound(mFilteredKeys.begin(), mFilteredKeys.end(), id);
            if (fit != mFilteredKeys.end() && *fit == id) mFilteredKeys.erase(fit);
            emit idChange(id, false);
        }
        endResetModel();
    }
    else
        /* refresh data */
        emit dataChanged(createIndex(0, 0),
                    createIndex(rowCount()-1, columnCount()-1), QVector<int>(Qt::DisplayRole));
}


void SnifferModel::filter(fltType pType, int pId)
{
    beginResetModel();
    switch(pType)
    {
        case fltType::NONE:
            mFilter = true;
            mFilters.clear();
            mFilteredKeys.clear();
            break;
        case fltType::ADD:
            mFilter = true;
            mFilters[(quint32)pId] = mMap[(quint32)pId];
            {
                auto it = std::lower_bound(mFilteredKeys.begin(), mFilteredKeys.end(), (quint32)pId);
                if (it == mFilteredKeys.end() || *it != (quint32)pId)
                    mFilteredKeys.insert(it, (quint32)pId);
            }
            break;
        case fltType::REMOVE:
            if(!mFilter)
            {
                mFilters = mMap;
                mFilteredKeys = mOrderedKeys;
            }
            mFilter = true;
            mFilters.remove((quint32)pId);
            {
                auto it = std::lower_bound(mFilteredKeys.begin(), mFilteredKeys.end(), (quint32)pId);
                if (it != mFilteredKeys.end() && *it == (quint32)pId)
                    mFilteredKeys.erase(it);
            }
            break;
        case fltType::ALL:
            mFilter = false;
            mFilters.clear();
            mFilteredKeys.clear();
            break;
    }
    endResetModel();
}


/***********************************************/
/**********         slots       ****************/
/***********************************************/

void SnifferModel::update(CANConnection*, QVector<CANFrame>& pFrames)
{
    foreach(const CANFrame& frame, pFrames)
    {
        quint32 fid = frame.frameId();
        if(!mMap.contains(fid))
        {
            //the announced row has to be computed against whichever containers currently drive
            //rowCount()/index(), which are the filtered ones while a filter is active
            QVector<quint32>& activeKeys = mFilter ? mFilteredKeys : mOrderedKeys;
            auto activeIt = std::lower_bound(activeKeys.begin(), activeKeys.end(), fid);
            int insertRow = static_cast<int>(activeIt - activeKeys.begin());
            beginInsertRows(QModelIndex(), insertRow, insertRow);
            mMap[fid] = new SnifferItem(frame, mTimeSequence);
            mMap[fid]->update(frame, mTimeSequence, mMuteNotched);
            auto it = std::lower_bound(mOrderedKeys.begin(), mOrderedKeys.end(), fid);
            mOrderedKeys.insert(it, fid);
            if (mFilter)
            {
                mFilters[fid] = mMap[fid];
                mFilteredKeys.insert(mFilteredKeys.begin() + insertRow, fid);
            }
            endInsertRows();

            emit idChange(fid, true);
        }
        else
            mMap[fid]->update(frame, mTimeSequence, mMuteNotched);
    }
}

void SnifferModel::notch()
{
    QMap<quint32, SnifferItem*>& map = mFilter ? mFilters : mMap;

    foreach(SnifferItem* item, map)
        item->notch(true);
}

void SnifferModel::unNotch()
{
    QMap<quint32, SnifferItem*>& map = mFilter ? mFilters : mMap;

    foreach(SnifferItem* item, map)
        item->notch(false);
}


