#include "canframemodel.h"

#include <QFile>
#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QBrush>
#include <QDateTime>
#include <QSettings>
#include "utility.h"

CANFrameModel::~CANFrameModel()
{
    frames.clear();
    filteredFrames.clear();
    filters.clear();
    busFilters.clear();
}

int CANFrameModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    /* Only the rows the view has been told about. filteredFrames can be ahead of this between
     * bulk refreshes because frames are appended silently and announced later - reporting the raw
     * container size here would surface rows no insert signal was ever sent for, and a sorting
     * proxy would then end up counting those rows twice. */
    return mViewRowCount;
}

int CANFrameModel::totalFrameCount()
{
    int count;
    count = frames.size();
    return count;
}

int CANFrameModel::columnCount(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return (int)Column::NUM_COLUMN;
}

CANFrameModel::CANFrameModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    int maxFramesDefault;
    if (QSysInfo::WordSize > 32)
    {
        qDebug() << "64 bit OS detected. Requesting a large preallocation";
        maxFramesDefault = 10000000;
    }
    else //if compiling for 32 bit you can't ask for gigabytes of preallocation so tone it down.
    {
        qDebug() << "32 bit OS detected. Requesting a much restricted prealloc";
        maxFramesDefault = 2000000;
    }

    QSettings settings;
    preallocSize = settings.value("Main/MaximumFrames", maxFramesDefault).toInt();

    //Each CANFrame object takes up 56 bytes and we're allocating two arrays here so take the
    //# of pre-alloc frames and multiply by 112 to get the RAM usage. This is around 1GiB for the default.

    //the goal is to prevent a reallocation from ever happening
    frames.reserve(preallocSize);
    //this is pretty wasteful. We're storing all frames twice. It may be better for filteredFrames to be a list of pointers.
    //Pointers take up 8 bytes instead of 56 so this is quite a savings for RAM usage. But, then filteredFrames would
    //work differently from frames above so the two could not be used interchangeably. Still think about what can be done.
    filteredFrames.reserve(preallocSize);

    dbcHandler = DBCHandler::getReference();
    interpretFrames = false;
    overwriteDups = false;
    filtersPersistDuringClear = false;
    useHexMode = true;
    useColorsByCanId = false;
    timeStyle = TS_MICROS;
    timeOffset = 0;
    needFilterRefresh = false;
    lastUpdateNumFrames = 0;
    timeFormat =  "MMM-dd HH:mm:ss.zzz";
    bytesPerLine = 8;
    lastFilteredUpdateCount = 0;
    hasAnyDisabledFilter = false;
    hasAnyDisabledBusFilter = false;
}

void CANFrameModel::setBytesPerLine(int bpl)
{
    bytesPerLine = bpl;
}

void CANFrameModel::setHexMode(bool mode)
{
    if (useHexMode != mode)
    {
        this->beginResetModel();
        useHexMode = mode;
        Utility::decimalMode = !useHexMode;
        syncViewRowCount();
        this->endResetModel();
    }
}

void CANFrameModel::setUseColorsByCanId(bool mode)
{
    if (useColorsByCanId != mode)
    {
        this->beginResetModel();
        useColorsByCanId = mode;
        syncViewRowCount();
        this->endResetModel();
    }
}

void CANFrameModel::setTimeStyle(TimeStyle newStyle)
{
    if (timeStyle != newStyle)
    {
        this->beginResetModel();
        timeStyle = newStyle;
        Utility::timeStyle = newStyle;
        syncViewRowCount();
        this->endResetModel();
    }
}

void CANFrameModel::setInterpretMode(bool mode)
{
    //if the state of interpretFrames changes then we need to reset the model
    //so that QT will refresh the view properly
    if (interpretFrames != mode)
    {
        this->beginResetModel();
        interpretFrames = mode;
        syncViewRowCount();
        this->endResetModel();
    }
}

bool CANFrameModel::getInterpretMode()
{
    return interpretFrames;
}

void CANFrameModel::setTimeFormat(QString format)
{
    Utility::timeFormat = format;
    timeFormat = format;
    beginResetModel(); //reset model to show new time format
    syncViewRowCount();
    endResetModel();
}

void CANFrameModel::setIgnoreDBCColors(bool mode)
{
    if(ignoreDBCColors != mode)
    {
        beginResetModel(); //reset model to update the view
        ignoreDBCColors = mode;
        syncViewRowCount();
        endResetModel();
    }
}

/*
 * Scan all frames for the smallest timestamp and offset all timestamps so that smallest one is at 0
*/
void CANFrameModel::normalizeTiming()
{
    mutex.lock();
    if (frames.size() == 0) 
    {
        mutex.unlock();
        return;
    }
    timeOffset = frames[0].timeStamp().microSeconds();
    qint64 prevStamp = 0;

    //find the absolute lowest timestamp in the whole time. Needed because maybe timestamp was reset in the middle.
    for (int j = 0; j < frames.size(); j++)
    {
        if (frames[j].timeStamp().microSeconds() < timeOffset) timeOffset = frames[j].timeStamp().microSeconds();
    }

    for (int i = 0; i < frames.size(); i++)
    {
        qint64 thisStamp = frames[i].timeStamp().microSeconds() - timeOffset;
        if (thisStamp <= prevStamp)
        {
            timeOffset -= prevStamp;
        }
        frames[i].setTimeStamp(QCanBusFrame::TimeStamp(0, thisStamp));
    }

    this->beginResetModel();
    for (int i = 0; i < filteredFrames.size(); i++)
    {
        filteredFrames[i].setTimeStamp(QCanBusFrame::TimeStamp(0, filteredFrames[i].timeStamp().microSeconds() - timeOffset));
    }
    syncViewRowCount();
    this->endResetModel();

    mutex.unlock();
}

void CANFrameModel::setOverwriteMode(bool mode)
{
    beginResetModel();
    overwriteDups = mode;
    recalcOverwrite();
    syncViewRowCount();
    endResetModel();
}

void CANFrameModel::setClearMode(bool mode)
{
    filtersPersistDuringClear = mode;
}

void CANFrameModel::setFilterState(unsigned int ID, bool state)
{
    if (!filters.contains(ID)) return;
    filters[ID] = state;
    if (!state) {
        hasAnyDisabledFilter = true;
    } else {
        hasAnyDisabledFilter = false;
        for (auto const &val : filters) { if (!val) { hasAnyDisabledFilter = true; break; } }
    }
    sendRefresh();
}

void CANFrameModel::setBusFilterState(unsigned int BusID, bool state)
{
    if (!busFilters.contains(BusID)) return;
    busFilters[BusID] = state;
    if (!state) {
        hasAnyDisabledBusFilter = true;
    } else {
        hasAnyDisabledBusFilter = false;
        for (auto const &val : busFilters) { if (!val) { hasAnyDisabledBusFilter = true; break; } }
    }
    sendRefresh();
}

void CANFrameModel::setAllFilters(bool state)
{
    QMap<int, bool>::iterator it;
    for (it = filters.begin(); it != filters.end(); ++it)
    {
        it.value() = state;
    }
    hasAnyDisabledFilter = !state;
    sendRefresh();
}

/*
 * There is probably a more correct way to have done this but below are several functions that collectively implement
 * quicksort on the columns and interpret the columns numerically. But, correct or not, this implementation is quite fast
 * and sorts the columns properly.
*/
uint64_t CANFrameModel::getCANFrameVal(const QVector<CANFrame> *frames, int row, Column col) const
{
    uint64_t temp = 0;
    if (row >= frames->count()) return 0;
    CANFrame frame = frames->at(row);
    switch (col)
    {
    case Column::TimeStamp:
        if (overwriteDups) return frame.timedelta;
        return frame.timeStamp().microSeconds();
    case Column::FrameId:
        return frame.frameId();
    case Column::Extended:
        if (frame.hasExtendedFrameFormat()) return 1;
        return 0;
    case Column::Remote:
        if (overwriteDups) return frame.frameCount;
        if (frame.frameType() == QCanBusFrame::RemoteRequestFrame) return 1;
        return 0;
    case Column::Direction:
        if (frame.isReceived) return 1;
        return 0;
    case Column::Bus:
        return static_cast<uint64_t>(frame.bus);
    case Column::Length:
        return static_cast<uint64_t>(frame.payload().length());
    case Column::ASCII: //sort both the same for now
    case Column::Data:
        for (int i = 0; i < std::min(static_cast<int>(frame.payload().length()), 8); i++) temp += (static_cast<uint64_t>(frame.payload()[i]) << (56 - (8 * i)));
        //qDebug() << temp;
        return temp;
    case Column::NUM_COLUMN:
        return 0;
    }
    return 0;
}

//the one key used everywhere overwrite mode identifies a frame: the full 32 bit ID (flag bits
//included) sits below the bus number, so frames from different buses can never share a key
static uint64_t overwriteKey(const CANFrame &frame)
{
    return (uint64_t)frame.frameId() | ((uint64_t)frame.bus << 32);
}

/*
 * Overwrite mode keeps a lookup from CAN ID to the row showing that ID, so an arriving frame can
 * replace the row it belongs to. Every row that moves invalidates it, so it has to be rebuilt after
 * a sort - otherwise updates land on whatever row happens to now sit at the remembered index,
 * overwriting an unrelated frame.
 */
void CANFrameModel::rebuildOverwriteIndex()
{
    if (!overwriteDups) return;

    overwriteIndex.clear();
    for (int i = 0; i < filteredFrames.size(); i++)
    {
        const uint64_t augID = overwriteKey(filteredFrames[i]);
        overwriteIndex[augID] = i;
    }
}

//End of custom sorting code

void CANFrameModel::recalcOverwrite()
{
    if (!overwriteDups) return; //no need to do a thing if mode is disabled

    qDebug() << "recalcOverwrite called in model";

    mutex.lock();
    beginResetModel();

    //Look at the current list of frames and turn it into just a list of unique IDs
    QHash<uint64_t, CANFrame> overWriteFrames;
    uint64_t idAugmented; //id in lower 32 bits, bus number above them
    foreach(const CANFrame& frame, frames)
    {
        if (frame.frameType() != frame.DataFrame) continue;

        idAugmented = overwriteKey(frame);
        if (filters[frame.frameId()] && busFilters[frame.bus])
        {
            CANFrame mutableFrame = frame; // copy only for frames that will be stored
            if (!overWriteFrames.contains(idAugmented))
            {
                mutableFrame.timedelta = 0;
                mutableFrame.frameCount = 1;
                overWriteFrames.insert(idAugmented, mutableFrame);
            }
            else
            {
                mutableFrame.timedelta = frame.timeStamp().microSeconds() - overWriteFrames[idAugmented].timeStamp().microSeconds();
                mutableFrame.frameCount = overWriteFrames[idAugmented].frameCount + 1;
                overWriteFrames[idAugmented] = mutableFrame;
            }
        }
    }
    //Then replace the old list of frames with just the unique list
    //frames.clear();
    //frames.append(overWriteFrames.values().toVector());
    //frames.reserve(preallocSize);

    filteredFrames.clear();
    filteredFrames.append(overWriteFrames.values().toVector());
    filteredFrames.reserve(preallocSize);
    overwriteIndex.clear();
    for (int i = 0; i < filteredFrames.size(); i++)
    {
        uint64_t augID = overwriteKey(filteredFrames[i]);
        overwriteIndex[augID] = i;
    }

    /*for (int i = 0; i < frames.size(); i++)
    {
        if (filters[frames[i].frameId()] && busFilters[frames[i].bus])
        {
            filteredFrames.append(frames[i]);
        }
    }*/

    syncViewRowCount();
    endResetModel();
    mutex.unlock();
}

QVariant CANFrameModel::data(const QModelIndex &index, int role) const
{
    QString tempString;
    QVariant ts;

    if (!index.isValid())
        return QVariant();

    if (index.row() >= (filteredFrames.size()))
        return QVariant();

    const CANFrame &thisFrame = filteredFrames.at(index.row());

    const unsigned char *data = reinterpret_cast<const unsigned char *>(thisFrame.payload().constData());
    int dataLen = thisFrame.payload().size();

    /* The value a sorting proxy should order this cell by. The displayed text is no good for that -
     * IDs are hex strings, timestamps are formatted - so hand out the underlying number instead and
     * let the proxy compare those. */
    if (role == CANFrameModel::SortRole)
    {
        return QVariant((qulonglong)getCANFrameVal(&filteredFrames, index.row(), Column(index.column())));
    }

    if (role == Qt::BackgroundRole)
    {
        if (dbcHandler != nullptr && interpretFrames && !ignoreDBCColors)
        {
            DBC_MESSAGE *msg = dbcHandler->findMessage(thisFrame);
            if (msg != nullptr)
            {
                return msg->bgColor;
            }
        }

        if (useColorsByCanId)
        {
            QString frameStr = Utility::formatCANID(thisFrame.frameId(), thisFrame.hasExtendedFrameFormat());
            // Color rows by the value in the "category" column
            return QBrush(Utility::colorForString(frameStr));
        }

        return (index.row() % 2) ?
            QApplication::palette().color(QPalette::Base) :
            QApplication::palette().color(QPalette::AlternateBase);
    }

    if (role == Qt::ForegroundRole)
    {
        if (dbcHandler != nullptr && interpretFrames && !ignoreDBCColors)
        {
            DBC_MESSAGE *msg = dbcHandler->findMessage(thisFrame);
            if (msg != nullptr)
            {
                return msg->fgColor;
            }
        }

        return QApplication::palette().color(QPalette::WindowText);
    }

    if (role == Qt::TextAlignmentRole)
    {
        switch(Column(index.column()))
        {
        case Column::TimeStamp:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case Column::FrameId:
        case Column::Direction:
        case Column::Extended:
        case Column::Bus:
        case Column::Remote:
        case Column::Length:
            return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
        default:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role == Qt::DisplayRole) {
        switch (Column(index.column()))
        {
        case Column::TimeStamp:            
            //Reformatting the output a bit with custom code
            if (overwriteDups)
            {
                if (timeStyle == TS_SECONDS) return QString::number(thisFrame.timedelta / 1000000.0, 'f', 5);
                return QString::number(thisFrame.timedelta);
            }
            else ts = Utility::formatTimestamp(thisFrame.timeStamp().microSeconds());
            if (ts.typeId() == QMetaType::Double) return QString::number(ts.toDouble(), 'f', 5);
            if (ts.typeId() == QMetaType::ULongLong) return QString::number(ts.toULongLong());
            if (ts.typeId() == QMetaType::QDateTime) return ts.toDateTime().toString(timeFormat);
            return ts;
        case Column::FrameId:
            return Utility::formatCANID(thisFrame.frameId(), thisFrame.hasExtendedFrameFormat());
        case Column::Extended:
            return QString::number(thisFrame.hasExtendedFrameFormat());
        case Column::Remote:
            if (!overwriteDups) return QString::number(thisFrame.frameType() == QCanBusFrame::RemoteRequestFrame);
            return QString::number(thisFrame.frameCount);
        case Column::Direction:
            if (thisFrame.isReceived) return QString(tr("Rx"));
            return QString(tr("Tx"));
        case Column::Bus:
            return QString::number(thisFrame.bus);
        case Column::Length:
            return QString::number(dataLen);
        case Column::ASCII:
            if (thisFrame.frameId() >= 0x7FFFFFF0ull)
            {
                tempString.append("MARK ");
                tempString.append(QString::number(thisFrame.frameId() & 0x7));
                return tempString;
            }
            if (thisFrame.frameType() == QCanBusFrame::DataFrame) {
                if (dataLen < 0) dataLen = 0;
                //if (dLen > 8) dLen = 8;
                for (int i = 0; i < dataLen; i++)
                {
                    char byt = thisFrame.payload()[i];
                    //0x20 through 0x7E are printable characters. Outside of that range they aren't. So use dots instead
                    if (byt < 0x20) byt = 0x2E; //dot character
                    if (byt > 0x7E) byt = 0x2E;
                    tempString.append(QString::fromUtf8(&byt, 1));
                    if (!((i+1) % bytesPerLine) && (i != (dataLen - 1))) tempString.append("\n");
                }
            }
            //bit 29 is SavvyCAN's error flag, which not every driver pairs with an ErrorFrame type
            if (thisFrame.frameType() == QCanBusFrame::ErrorFrame || (thisFrame.frameId() & 0x20000000))
            {
                 tempString = "ERROR";
            }
            return tempString;
        case Column::Data:
            if (dataLen < 0) dataLen = 0;
            //if (useHexMode) tempString.append("0x ");
            if (thisFrame.frameType() == QCanBusFrame::RemoteRequestFrame) {
                return tempString;
            }
            for (int i = 0; i < dataLen; i++)
            {
                if (useHexMode) tempString.append( QString::number(data[i], 16).toUpper().rightJustified(2, '0'));
                else tempString.append(QString::number(data[i], 10));
                if (!((i+1) % bytesPerLine) && (i != (dataLen - 1))) tempString.append("\n");
                else tempString.append(" ");
            }
            if (thisFrame.frameType() == thisFrame.ErrorFrame)
            {
                if (thisFrame.error() & thisFrame.TransmissionTimeoutError) tempString.append("\nTX Timeout");
                if (thisFrame.error() & thisFrame.LostArbitrationError) tempString.append("\nLost Arbitration");
                if (thisFrame.error() & thisFrame.ControllerError) tempString.append("\nController Error");
                if (thisFrame.error() & thisFrame.ProtocolViolationError) tempString.append("\nProtocol Violation");
                if (thisFrame.error() & thisFrame.TransceiverError) tempString.append("\nTransceiver Error");
                if (thisFrame.error() & thisFrame.MissingAcknowledgmentError) tempString.append("\nMissing ACK");
                if (thisFrame.error() & thisFrame.BusOffError) tempString.append("\nBus OFF");
                if (thisFrame.error() & thisFrame.BusError) tempString.append("\nBus ERR");
                if (thisFrame.error() & thisFrame.ControllerRestartError) tempString.append("\nController restart err");
                if (thisFrame.error() & thisFrame.UnknownError) tempString.append("\nUnknown error type");
            }

            /* Anything with bit 29 set is an error frame in SavvyCAN's convention, whether or not
             * the driver also flagged a frame type. The ID and payload carry the SocketCAN error
             * encoding, which says what actually went wrong and how high the error counters are. */
            if (thisFrame.frameId() & 0x20000000)
            {
                tempString.append("\n");
                tempString.append(Utility::decodeErrorFrame(thisFrame.frameId(), thisFrame.payload()));
            }

            //now, if we're supposed to interpret the data and the DBC handler is loaded then use it
            if ( (dbcHandler != nullptr) && interpretFrames && (thisFrame.frameType() == thisFrame.DataFrame) )
            {
                DBC_MESSAGE *msg = dbcHandler->findMessage(thisFrame);
                if (msg != nullptr)
                {
                    tempString.append("   <" + msg->name + ">\n");
                    if (msg->comment.length() > 1) tempString.append(msg->comment + "\n");
                    for (int j = 0; j < msg->sigHandler->getCount(); j++)
                    {                        
                        QString sigString;
                        DBC_SIGNAL* sig = msg->sigHandler->findSignalByIdx(j);
                        if (!sig) continue; // guard against null return (malformed DBC or index mismatch)

                        if ( (sig->multiplexParent == nullptr) && sig->processAsText(thisFrame, sigString))
                        {
                            tempString.append(sigString);
                            tempString.append("\n");
                            if (sig->isMultiplexor)
                            {
                                qDebug() << "Multiplexor. Diving into the tree";
                                tempString.append(sig->processSignalTree(thisFrame));
                            }
                        }
                        else if (sig->isMultiplexed && overwriteDups) //wasn't in this exact frame but is in the message. Use cached value
                        {
                            bool isInteger = false;
                            if (sig->valType == UNSIGNED_INT || sig->valType == SIGNED_INT) isInteger = true;
                            tempString.append(sig->makePrettyOutput(sig->cachedValue.toDouble(), sig->cachedValue.toLongLong(), true, isInteger));
                            tempString.append("\n");
                        }
                    }
                }
            }
            return tempString;
        default:
            return tempString;
        }
    }

    return QVariant();
}

QVariant CANFrameModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal)
    {
        switch (Column(section))
        {
        case Column::TimeStamp:
            if (overwriteDups) return QString(tr("Time Delta"));
            return QString(tr("Timestamp"));
        case Column::FrameId:
            return QString(tr("ID"));
        case Column::Extended:
            return QString(tr("Ext"));
        case Column::Remote:
            if (!overwriteDups) return QString(tr("RTR"));
            return QString(tr("Cnt"));
        case Column::Direction:
            return QString(tr("Dir"));
        case Column::Bus:
            return QString(tr("Bus"));
        case Column::Length:
            return QString(tr("Len"));
        case Column::ASCII:
            return QString(tr("ASCII"));
        case Column::Data:
            return QString(tr("Data"));
        default:
            return QString("");
        }
    }

    else
        return QString::number(section + 1);

    return QVariant();
}

bool CANFrameModel::any_filters_are_configured(void)
{
    return hasAnyDisabledFilter;
}

bool CANFrameModel::any_busfilters_are_configured(void)
{
    return hasAnyDisabledBusFilter;
}


void CANFrameModel::addFrame(const CANFrame& frame, bool autoRefresh = false)
{
    /*TODO: remove mutex */
    mutex.lock();
    CANFrame tempFrame;
    tempFrame = frame;

    tempFrame.setTimeStamp(QCanBusFrame::TimeStamp(0, tempFrame.timeStamp().microSeconds() - timeOffset));

    lastUpdateNumFrames++;

    //if this ID isn't found in the filters list then add it and show it by default
    if (!filters.contains(tempFrame.frameId()))
    {
        // if there are any filters already configured, leave the new filter disabled
        if (hasAnyDisabledFilter) {
            filters.insert(tempFrame.frameId(), false);
            // hasAnyDisabledFilter stays true
        } else {
            filters.insert(tempFrame.frameId(), true);
        }
        needFilterRefresh = true;
    }

    //if this BusID isn't found in the busFilters list then add it and show it by default
    if (!busFilters.contains(tempFrame.bus))
    {
        // if there are any busFilters already configured, leave the new filter disabled
        if (hasAnyDisabledBusFilter) {
            busFilters.insert(tempFrame.bus, false);
            // hasAnyDisabledBusFilter stays true
        } else {
            busFilters.insert(tempFrame.bus, true);
        }
        needFilterRefresh = true;
    }

    if (!overwriteDups)
    {
        try
        {
            frames.append(tempFrame);

            if (filters[tempFrame.frameId()] && busFilters[tempFrame.bus])
            {
                if (autoRefresh) beginInsertRows(QModelIndex(), filteredFrames.size(), filteredFrames.size());
                tempFrame.frameCount = 1;
                filteredFrames.append(tempFrame);
                lastFilteredUpdateCount++;
                if (autoRefresh)
                {
                    syncViewRowCount();
                    endInsertRows();
                }
            }
        }
        catch (const std::exception& ex)
        {
            qDebug() << "addFrame failed to append. App is probably going to crash. frames.length(): " << frames.length() << " Exception: " << ex.what();
        }
    }
    else //yes, overwrite dups
    {
        const uint64_t augID = overwriteKey(tempFrame);
        if (overwriteIndex.contains(augID))
        {
            int idx = overwriteIndex[augID];
            tempFrame.frameCount = filteredFrames[idx].frameCount + 1;
            tempFrame.timedelta = tempFrame.timeStamp().microSeconds() - filteredFrames[idx].timeStamp().microSeconds();
            filteredFrames.replace(idx, tempFrame);
            /* This row's contents changed rather than a new row appearing. Note it so the next
             * refresh can repaint it - previously the whole model was reset to achieve this. */
            markRowDirty(idx);
            if (autoRefresh)
            {
                const QModelIndex changed = index(idx, 0);
                emit dataChanged(changed, index(idx, columnCount(QModelIndex()) - 1));
            }
        }
        else
        {
            if (filters[tempFrame.frameId()] && busFilters[tempFrame.bus])
            {
                if (autoRefresh) beginInsertRows(QModelIndex(), filteredFrames.size(), filteredFrames.size());
                tempFrame.frameCount = 1;
                tempFrame.timedelta = 0;
                overwriteIndex[augID] = filteredFrames.size();
                filteredFrames.append(tempFrame);
                if (autoRefresh)
                {
                    syncViewRowCount();
                    endInsertRows();
                }
            }
        }
        frames.append(tempFrame);
    }

    mutex.unlock();
}


void CANFrameModel::addFrames(const CANConnection*, const QVector<CANFrame>& pFrames)
{
    if(frames.length() > frames.capacity() * 0.99)
    {
        mutex.lock();
        qDebug() << "Frames count: " << frames.length() << " of " << frames.capacity() << " capacity, removing first " << (int)(frames.capacity() * 0.05) << " frames";
        frames.remove(0, (int)(frames.capacity() * 0.05));
        qDebug() << "Frames removed, new count: " << frames.length();
        mutex.unlock();
    }

    if(filteredFrames.length() > filteredFrames.capacity() * 0.99)
    {
        mutex.lock();
        int toRemove = (int)(filteredFrames.capacity() * 0.05);
        qDebug() << "filteredFrames count: " << filteredFrames.length() << " of " << filteredFrames.capacity() << " capacity, removing first " << toRemove << " frames";
        //the view only knows about mViewRowCount rows - anything appended since the last refresh
        //is still unannounced and must stay that way so the next bulk refresh can report it
        const int announced = qMin(toRemove, mViewRowCount);
        if (announced > 0) beginRemoveRows(QModelIndex(), 0, announced - 1);
        filteredFrames.remove(0, toRemove);
        mViewRowCount -= announced;
        if (announced > 0) endRemoveRows();
        qDebug() << "filteredFrames removed, new count: " << filteredFrames.length();
        lastFilteredUpdateCount = 0;
        //trimming from the front shifts every remaining row, so the ID to row lookup is stale
        rebuildOverwriteIndex();
        mutex.unlock();
    }

    foreach(const CANFrame& frame, pFrames)
    {
        addFrame(frame);
    }
}

void CANFrameModel::sendRefresh()
{
    qDebug() << "Sending mass refresh";    

    if(overwriteDups)
    {
        recalcOverwrite();
    }
    else
    {
        QVector<CANFrame> tempContainer;
        int count = frames.size();
        for (int i = 0; i < count; i++)
        {
            if (filters[frames[i].frameId()] && busFilters[frames[i].bus])
            {
                tempContainer.append(frames[i]);
            }
        }

        mutex.lock();
        beginResetModel();
        filteredFrames.clear();
        filteredFrames.append(tempContainer);
        filteredFrames.reserve(preallocSize);
        lastUpdateNumFrames = 0;
        lastFilteredUpdateCount = 0;
        syncViewRowCount();
        endResetModel();
        mutex.unlock();
    }
}

void CANFrameModel::sendRefresh(int pos)
{
    beginInsertRows(QModelIndex(), pos, pos);
    syncViewRowCount();
    endInsertRows();
}

//issue a refresh for the last num entries in the model.
//used by the serial worker to do batch updates so it doesn't
//have to send thousands of messages per second
void CANFrameModel::markRowDirty(int row)
{
    if (row < 0) return;
    if (mDirtyRowLow < 0 || row < mDirtyRowLow) mDirtyRowLow = row;
    if (row > mDirtyRowHigh) mDirtyRowHigh = row;
}

/*
 * Tell the view what changed since the last time we were called.
 *
 * This used to reset the entire model on every call, which happens several times a second while
 * frames are coming in. A reset tells the view that everything it knows is void, so it throws away
 * per row state - row heights, and the user's selection - and re-lays out the whole table. That is
 * why an expanded row snapped shut again during capture or playback, and why the table got heavy
 * under load.
 *
 * Appends are now reported as inserts and in place changes as dataChanged, both of which leave the
 * rest of the view alone. A full reset is still used when something happened that these cannot
 * describe, which is the safe fallback rather than the normal path.
 */
int CANFrameModel::sendBulkRefresh()
{
    if (lastUpdateNumFrames <= 0) return 0;

    const int newRowCount = filteredFrames.count();
    const int oldRowCount = mViewRowCount;
    const int lastCol = columnCount(QModelIndex()) - 1;

    if (newRowCount < oldRowCount || oldRowCount < 0)
    {
        /* Rows vanished without us announcing it, so we can't describe the difference. Only happens
         * if something bypassed the notifications above; a reset always leaves the view correct. */
        qDebug() << "Bulk refresh sees fewer rows than the view has (" << newRowCount << "vs" << oldRowCount << ") - falling back to a model reset";
        beginResetModel();
        syncViewRowCount();
        endResetModel();
    }
    else
    {
        if (newRowCount > oldRowCount)
        {
            beginInsertRows(QModelIndex(), oldRowCount, newRowCount - 1);
            syncViewRowCount();
            endInsertRows();
        }

        /* Rows that were rewritten in place - overwrite mode replacing a frame with a newer one of
         * the same ID. Report the touched span so those rows repaint. */
        if (mDirtyRowLow >= 0 && lastCol >= 0)
        {
            const int low = qMin(mDirtyRowLow, newRowCount - 1);
            const int high = qMin(mDirtyRowHigh, newRowCount - 1);
            if (low >= 0 && high >= low)
                emit dataChanged(index(low, 0), index(high, lastCol));
        }
    }

    mDirtyRowLow = -1;
    mDirtyRowHigh = -1;

    int num = lastUpdateNumFrames;
    lastUpdateNumFrames = 0;
    lastFilteredUpdateCount = 0;

    return num;
}

void CANFrameModel::clearFrames()
{
    mutex.lock();
    this->beginResetModel();
    frames.clear();
    filteredFrames.clear();
    if(filtersPersistDuringClear == false)
    {
        filters.clear();
        busFilters.clear();
    }
    frames.reserve(preallocSize);
    filteredFrames.reserve(preallocSize);
    syncViewRowCount();
    this->endResetModel();
    lastUpdateNumFrames = 0;
    lastFilteredUpdateCount = 0;
    overwriteIndex.clear();
    if (!filtersPersistDuringClear)
    {
        hasAnyDisabledFilter = false;
        hasAnyDisabledBusFilter = false;
    }
    mutex.unlock();

    emit updatedFiltersList();
}

/*
 * Since the getListReference function returns readonly
 * you can't insert frames with it. Instead this function
 * allows for a mass import of frames into the model
 */
void CANFrameModel::insertFrames(const QVector<CANFrame> &newFrames)
{
    //not resetting the model here because the serial worker automatically does a bulk refresh every 1/4 second
    //and that refresh will cause the view to update. If you do both it usually ends up thinking you have
    //double the number of frames.
    //beginResetModel();
    mutex.lock();
    int insertedFiltered = 0;
    for (int i = 0; i < newFrames.size(); i++)
    {
        frames.append(newFrames[i]);
        if (!filters.contains(newFrames[i].frameId()))
        {
            filters.insert(newFrames[i].frameId(), true);
            needFilterRefresh = true;
        }
        if (!busFilters.contains(newFrames[i].bus))
        {
            // if there are any busFilters already configured, leave the new filter disabled
            if (hasAnyDisabledBusFilter) {
                busFilters.insert(newFrames[i].bus, false);
                // hasAnyDisabledBusFilter stays true
            } else {
                busFilters.insert(newFrames[i].bus, true);
            }
            needFilterRefresh = true;
        }
        if (filters[newFrames[i].frameId()] && busFilters[newFrames[i].bus])
        {
            insertedFiltered++;
            filteredFrames.append(newFrames[i]);
            lastFilteredUpdateCount++;
        }
    }
    lastUpdateNumFrames = newFrames.size();
    mutex.unlock();
    //endResetModel();
    //beginInsertRows(QModelIndex(), filteredFrames.size() + 1, filteredFrames.size() + insertedFiltered);
    //endInsertRows();
    if (needFilterRefresh) emit updatedFiltersList();
}

int CANFrameModel::getIndexFromTimeID(unsigned int ID, double timestamp)
{
    mutex.lock();
    int bestIndex = -1;
    int64_t intTimeStamp = static_cast<int64_t> (timestamp * 1000000l);
    for (int i = 0; i < frames.size(); i++)
    {
        if ((frames[i].frameId() == ID))
        {
            if (frames[i].timeStamp().microSeconds() <= intTimeStamp) bestIndex = i;
            else break; //drop out of loop as soon as we pass the proper timestamp
        }
    }
    mutex.unlock();
    return bestIndex;
}

void CANFrameModel::loadFilterFile(QString filename)
{
    QFile inFile(filename);
    QByteArray line;
    int ID;

    if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    filters.clear();
    busFilters.clear();

    while (!inFile.atEnd()) {
        line = inFile.readLine().simplified();
        if (line.length() > 2)
        {
            QList<QByteArray> tokens = line.split(',');
            if (tokens.count() < 2) continue;
            bool ok = false;
            ID = tokens[0].toInt(&ok, 16);
            if (!ok) continue;
            if (tokens[1].toUpper() == "T") filters.insert(ID, true);
                else filters.insert(ID, false);
        }
    }
    inFile.close();

    hasAnyDisabledFilter = false;
    for (auto const &val : filters) { if (!val) { hasAnyDisabledFilter = true; break; } }
    sendRefresh();

    emit updatedFiltersList();
}

void CANFrameModel::saveFilterFile(QString filename)
{
    QFile outFile(filename);

    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QMap<int, bool>::const_iterator it;
    for (it = filters.begin(); it != filters.end(); ++it)
    {
        outFile.write(QString::number(it.key(), 16).toUtf8());
        outFile.putChar(',');
        if (it.value()) outFile.putChar('T');
            else outFile.putChar('F');
        outFile.write("\n");
    }
    outFile.close();
}

bool CANFrameModel::needsFilterRefresh()
{
    bool temp = needFilterRefresh;
    needFilterRefresh = false;
    return temp;
}

/*
 *This used to not be const correct but it is now. So, there's little harm in
 * allowing external code to peek at our frames. There's just no touching.
 * This ability to get a direct read-only reference speeds up a variety of
 * external code that needs to access frames directly and doesn't care about
 * this model's normal output mechanism.
 */
const QVector<CANFrame>* CANFrameModel::getListReference() const
{
    return &frames;
}

const QVector<CANFrame>* CANFrameModel::getFilteredListReference() const
{
    return &filteredFrames;
}

const QMap<int, bool>* CANFrameModel::getFiltersReference() const
{
    return &filters;
}

const QMap<int, bool>* CANFrameModel::getBusFiltersReference() const
{
    return &busFilters;
}
