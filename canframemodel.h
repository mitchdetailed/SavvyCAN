#ifndef CANFRAMEMODEL_H
#define CANFRAMEMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QVector>
#include <QDebug>
#include <QMutex>
#include "can_structs.h"
#include "dbc/dbchandler.h"
#include "connections/canconnection.h"
#include "utility.h"

enum class Column {
    TimeStamp = 0, ///< The timestamp when the frame was transmitted or received
    FrameId   = 1, ///< The frames CAN identifier (Standard: 11 or Extended: 29 bit)
    Extended  = 2, ///< True if the frames CAN identifier is 29 bit
    Remote    = 3, ///< True if the frames is a remote frame
    Direction = 4, ///< Whether the frame was transmitted or received
    Bus       = 5, ///< The bus where the frame was transmitted or received
    Length    = 6, ///< The frames payload data length
    ASCII     = 7, ///< The payload interpreted as ASCII characters
    Data      = 8, ///< The frames payload data
    NUM_COLUMN
};

class CANFrameModel: public QAbstractTableModel
{
    Q_OBJECT

public:
    /* Role a sorting proxy should ask for. Returns the raw number behind a cell rather than the
     * text shown in it, because the display text sorts wrongly - IDs are hex, timestamps are
     * formatted, lengths are strings. The model itself never sorts; ordering belongs to the view
     * layer so that the model can stay append only. */
    static const int SortRole = Qt::UserRole + 1;

    CANFrameModel(QObject *parent = 0);
    virtual ~CANFrameModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const;
    int columnCount(const QModelIndex &) const;
    int totalFrameCount();

    void sendRefresh();
    void sendRefresh(int);
    int  sendBulkRefresh();
    void clearFrames();
    void setInterpretMode(bool);
    bool getInterpretMode();
    void setOverwriteMode(bool);
    void setHexMode(bool);
    void setUseColorsByCanId(bool);
    void setClearMode(bool mode);
    void setTimeStyle(TimeStyle newStyle);
    void setIgnoreDBCColors(bool mode);
    void setFilterState(unsigned int ID, bool state);
    void setBusFilterState(unsigned int BusID, bool state);
    void setAllFilters(bool state);
    void setTimeFormat(QString);
    void setBytesPerLine(int bpl);
    void loadFilterFile(QString filename);
    void saveFilterFile(QString filename);
    void normalizeTiming();
    void recalcOverwrite();
    bool needsFilterRefresh();
    void insertFrames(const QVector<CANFrame> &newFrames);
    int getIndexFromTimeID(unsigned int ID, double timestamp);
    const QVector<CANFrame> *getListReference() const; //thou shalt not modify these frames externally!
    const QVector<CANFrame> *getFilteredListReference() const; //Thus saith the Lord, NO.
    const QMap<int, bool> *getFiltersReference() const; //this neither
    const QMap<int, bool> *getBusFiltersReference() const; //this neither

public slots:
    void addFrame(const CANFrame&, bool);
    void addFrames(const CANConnection*, const QVector<CANFrame>&);

signals:
    void updatedFiltersList();

private:
    uint64_t getCANFrameVal(const QVector<CANFrame> *frames, int row, Column col) const;
    bool any_filters_are_configured(void);
    bool any_busfilters_are_configured(void);

    QVector<CANFrame> frames;
    QVector<CANFrame> filteredFrames;
    QMap<int, bool> filters;
    QMap<int, bool> busFilters;
    DBCHandler *dbcHandler;
    QMutex mutex;
    bool interpretFrames; //should we use the dbcHandler?
    bool overwriteDups; //should we display all frames or only the newest for each ID?
    bool filtersPersistDuringClear;
    QString timeFormat;
    TimeStyle timeStyle;
    bool useHexMode;
    bool useColorsByCanId;
    bool needFilterRefresh;
    bool ignoreDBCColors;
    int64_t timeOffset;
    int lastUpdateNumFrames;
    uint32_t preallocSize;
    int bytesPerLine;
    QHash<uint64_t, int> overwriteIndex;
    int lastFilteredUpdateCount;
    bool hasAnyDisabledFilter;
    bool hasAnyDisabledBusFilter;

    /* How many rows the view currently believes exist. Kept in step with every notification we
     * send it (reset, insert, remove) so that sendBulkRefresh can work out what actually changed
     * and describe it precisely instead of resetting the whole model. */
    int mViewRowCount = 0;
    /* In overwrite mode rows are replaced in place rather than appended. These bracket the rows
     * touched since the last refresh so we can repaint just those. -1 means nothing was touched. */
    int mDirtyRowLow = -1;
    int mDirtyRowHigh = -1;

    /* Bring mViewRowCount up to the container size. Must run inside the begin/end bracket of
     * whatever notification announces the change, because views re-query rowCount() while
     * handling the end call and rowCount() reports mViewRowCount. */
    void syncViewRowCount() { mViewRowCount = filteredFrames.count(); }
    //remember that a row's contents changed, for the next refresh to report
    void markRowDirty(int row);

    //rebuild the ID to row lookup, which any row movement invalidates
    void rebuildOverwriteIndex();
};


#endif // CANFRAMEMODEL_H

