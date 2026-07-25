#include <QtTest>
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>

#include "tst_framemodel.h"
#include "canframemodel.h"

namespace {

//builds a frame that will pass the model's default filters
CANFrame makeFrame(uint32_t id, int bus = 0, int len = 4)
{
    CANFrame f;
    f.setFrameId(id);
    f.setExtendedFrameFormat(id > 0x7FF);
    f.setFrameType(QCanBusFrame::DataFrame);
    f.bus = bus;
    f.isReceived = true;
    f.setTimeStamp(QCanBusFrame::TimeStamp(0, 1000));
    f.setPayload(QByteArray(len, (char)0xA5));
    return f;
}

/* Watches everything the model tells the view. The distinction the tests care about is whether a
 * change arrived as a reset (which costs the view all its per row state) or as a targeted insert
 * or data change. */
class ModelWatcher
{
public:
    explicit ModelWatcher(CANFrameModel *model)
        : resets(model, &QAbstractItemModel::modelReset),
          inserts(model, &QAbstractItemModel::rowsInserted),
          removes(model, &QAbstractItemModel::rowsRemoved),
          changes(model, &QAbstractItemModel::dataChanged) {}

    void clear() { resets.clear(); inserts.clear(); removes.clear(); changes.clear(); }

    QSignalSpy resets;
    QSignalSpy inserts;
    QSignalSpy removes;
    QSignalSpy changes;
};

} //namespace

void TestFrameModel::bulkRefreshInsertsRatherThanResets()
{
    CANFrameModel model;
    //false means "don't notify per frame", which is how live capture feeds the model
    for (int i = 0; i < 5; i++) model.addFrame(makeFrame(0x100 + i), false);

    ModelWatcher watch(&model);
    const int reported = model.sendBulkRefresh();

    QCOMPARE(reported, 5);
    QCOMPARE(watch.resets.count(), 0);   //the whole point: no reset
    QCOMPARE(watch.inserts.count(), 1);  //one contiguous run of new rows

    //first/last row of the inserted range, as rowsInserted(parent, first, last)
    const QList<QVariant> args = watch.inserts.at(0);
    QCOMPARE(args.at(1).toInt(), 0);
    QCOMPARE(args.at(2).toInt(), 4);
}

void TestFrameModel::bulkRefreshRowCountIsCorrect()
{
    CANFrameModel model;
    for (int i = 0; i < 12; i++) model.addFrame(makeFrame(0x200 + i), false);
    model.sendBulkRefresh();
    QCOMPARE(model.rowCount(QModelIndex()), 12);

    //a second batch has to land after the first, not replace it
    ModelWatcher watch(&model);
    for (int i = 0; i < 3; i++) model.addFrame(makeFrame(0x300 + i), false);
    model.sendBulkRefresh();

    QCOMPARE(model.rowCount(QModelIndex()), 15);
    QCOMPARE(watch.resets.count(), 0);
    QCOMPARE(watch.inserts.count(), 1);
    const QList<QVariant> args = watch.inserts.at(0);
    QCOMPARE(args.at(1).toInt(), 12);   //picks up exactly where the last batch ended
    QCOMPARE(args.at(2).toInt(), 14);
}

void TestFrameModel::bulkRefreshWithNoNewFramesIsSilent()
{
    CANFrameModel model;
    model.addFrame(makeFrame(0x123), false);
    model.sendBulkRefresh();

    ModelWatcher watch(&model);
    const int reported = model.sendBulkRefresh();

    QCOMPARE(reported, 0);
    QCOMPARE(watch.resets.count(), 0);
    QCOMPARE(watch.inserts.count(), 0);
    QCOMPARE(watch.changes.count(), 0);
}

void TestFrameModel::overwriteModeReportsDataChanged()
{
    CANFrameModel model;
    model.setOverwriteMode(true);

    //one frame per ID first, so there are rows to overwrite
    model.addFrame(makeFrame(0x111), false);
    model.addFrame(makeFrame(0x222), false);
    model.sendBulkRefresh();
    QCOMPARE(model.rowCount(QModelIndex()), 2);

    ModelWatcher watch(&model);

    //same IDs again - these replace the existing rows rather than adding new ones
    model.addFrame(makeFrame(0x111), false);
    model.addFrame(makeFrame(0x222), false);
    const int reported = model.sendBulkRefresh();

    QCOMPARE(reported, 2);
    QCOMPARE(model.rowCount(QModelIndex()), 2);   //still two rows
    QCOMPARE(watch.resets.count(), 0);            //and no reset to achieve it
    QCOMPARE(watch.inserts.count(), 0);           //nothing was added
    QVERIFY(watch.changes.count() > 0);           //the rows were repainted instead
}

void TestFrameModel::insertFramesAnnouncesAllRows()
{
    //this is the path a file load takes
    CANFrameModel model;
    QVector<CANFrame> loaded;
    for (int i = 0; i < 40; i++) loaded.append(makeFrame(0x400 + i));

    model.insertFrames(loaded);

    ModelWatcher watch(&model);
    model.sendBulkRefresh();

    QCOMPARE(model.rowCount(QModelIndex()), 40);
    QCOMPARE(watch.resets.count(), 0);
    QCOMPARE(watch.inserts.count(), 1);
    const QList<QVariant> args = watch.inserts.at(0);
    QCOMPARE(args.at(1).toInt(), 0);
    QCOMPARE(args.at(2).toInt(), 39);
}

void TestFrameModel::clearFramesResets()
{
    CANFrameModel model;
    for (int i = 0; i < 6; i++) model.addFrame(makeFrame(0x500 + i), false);
    model.sendBulkRefresh();

    ModelWatcher watch(&model);
    model.clearFrames();

    //throwing everything away really is structural, so a reset is the right answer here
    QCOMPARE(watch.resets.count(), 1);
    QCOMPARE(model.rowCount(QModelIndex()), 0);

    /* And the model must not then think the view still has the old rows - adding more frames
     * has to report them starting from zero again. */
    watch.clear();
    for (int i = 0; i < 2; i++) model.addFrame(makeFrame(0x600 + i), false);
    model.sendBulkRefresh();

    QCOMPARE(model.rowCount(QModelIndex()), 2);
    QCOMPARE(watch.resets.count(), 0);
    QCOMPARE(watch.inserts.count(), 1);
    const QList<QVariant> args = watch.inserts.at(0);
    QCOMPARE(args.at(1).toInt(), 0);
    QCOMPARE(args.at(2).toInt(), 1);
}

void TestFrameModel::repeatedRefreshesStayConsistent()
{
    /* The row count the model reports and the number of rows it has announced must never drift
     * apart. QAbstractItemModelTester checks the announcements are internally consistent as they
     * happen and will fail the test if an insert doesn't line up with the row count. */
    CANFrameModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Warning);

    ModelWatcher watch(&model);

    int expected = 0;
    for (int batch = 0; batch < 20; batch++)
    {
        const int count = 1 + (batch % 7);
        for (int i = 0; i < count; i++) model.addFrame(makeFrame(0x700 + i, 0, 1 + (i % 8)), false);
        model.sendBulkRefresh();
        expected += count;
        QCOMPARE(model.rowCount(QModelIndex()), expected);
    }

    //twenty batches of ordinary traffic, and not one reset among them
    QCOMPARE(watch.resets.count(), 0);
    QCOMPARE(watch.inserts.count(), 20);
}

/***************************************************************************************************
 * sorting, which is done by a proxy over the model rather than by the model
 **************************************************************************************************/

namespace {

//sets up the proxy the same way MainWindow does
void configureProxy(QSortFilterProxyModel &proxy, CANFrameModel &model)
{
    proxy.setSourceModel(&model);
    proxy.setSortRole(CANFrameModel::SortRole);
    proxy.setDynamicSortFilter(true);
}

//the IDs as the view would show them, in row order
QList<uint32_t> shownIds(QAbstractItemModel &m)
{
    QList<uint32_t> ids;
    for (int r = 0; r < m.rowCount(); r++)
        ids << m.data(m.index(r, 1), Qt::DisplayRole).toString().toUInt(nullptr, 16);
    return ids;
}

} //namespace

void TestFrameModel::proxySortsOnValuesNotDisplayedText()
{
    /* The IDs here are chosen so that sorting the displayed text would give the wrong answer:
     * as strings "0x100" sorts before "0x80", but 0x80 is the smaller number. This is what the
     * dedicated sort role is for. */
    CANFrameModel model;
    QSortFilterProxyModel proxy;
    configureProxy(proxy, model);

    const uint32_t ids[] = {0x100, 0x80, 0x7FF, 0x9};
    for (uint32_t id : ids) model.addFrame(makeFrame(id), false);
    model.sendBulkRefresh();

    proxy.sort(1, Qt::AscendingOrder);
    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x9, 0x80, 0x100, 0x7FF}));

    proxy.sort(1, Qt::DescendingOrder);
    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x7FF, 0x100, 0x80, 0x9}));
}

void TestFrameModel::proxyKeepsSortAsFramesArrive()
{
    //frames arriving must land in their sorted position, not get appended at the bottom
    CANFrameModel model;
    QSortFilterProxyModel proxy;
    configureProxy(proxy, model);

    const uint32_t ids[] = {0x300, 0x100, 0x200};
    for (uint32_t id : ids) model.addFrame(makeFrame(id), false);
    model.sendBulkRefresh();

    proxy.sort(1, Qt::AscendingOrder);
    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x100, 0x200, 0x300}));

    model.addFrame(makeFrame(0x250), false);   //belongs in the middle
    model.addFrame(makeFrame(0x050), false);   //belongs at the front
    model.sendBulkRefresh();

    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x050, 0x100, 0x200, 0x250, 0x300}));
}

void TestFrameModel::sourceModelStaysInArrivalOrder()
{
    /* The architectural point of the proxy: however the user sorts the view, the model underneath
     * keeps frames in the order they arrived, so it can go on reporting new ones as simple
     * appends. If this ever fails, sorting has leaked back down into the model. */
    CANFrameModel model;
    QSortFilterProxyModel proxy;
    configureProxy(proxy, model);

    const uint32_t ids[] = {0x300, 0x100, 0x200};
    for (uint32_t id : ids) model.addFrame(makeFrame(id), false);
    model.sendBulkRefresh();

    proxy.sort(1, Qt::AscendingOrder);
    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x100, 0x200, 0x300}));
    //...while the model is untouched
    QCOMPARE(shownIds(model), (QList<uint32_t>{0x300, 0x100, 0x200}));

    //and new frames are still plain appends as far as the model is concerned
    ModelWatcher watch(&model);
    model.addFrame(makeFrame(0x050), false);
    model.sendBulkRefresh();

    QCOMPARE(watch.resets.count(), 0);
    QCOMPARE(watch.inserts.count(), 1);
    QCOMPARE(shownIds(model), (QList<uint32_t>{0x300, 0x100, 0x200, 0x050}));
}

void TestFrameModel::selectionSurvivesFramesArrivingWhileSorted()
{
    /* This is what the whole refactor was for. Selecting a row and then having traffic arrive used
     * to lose the selection, because the model reset itself on every refresh. Row insertions and
     * the proxy's re-ordering both keep persistent indexes alive instead. */
    CANFrameModel model;
    QSortFilterProxyModel proxy;
    configureProxy(proxy, model);

    const uint32_t ids[] = {0x300, 0x100, 0x200};
    for (uint32_t id : ids) model.addFrame(makeFrame(id), false);
    model.sendBulkRefresh();
    proxy.sort(1, Qt::AscendingOrder);

    QItemSelectionModel selection(&proxy);
    //select the middle row, 0x200
    const QModelIndex chosen = proxy.index(1, 1);
    QCOMPARE(chosen.data(Qt::DisplayRole).toString().toUInt(nullptr, 16), (uint)0x200);
    selection.select(chosen, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(selection.selectedRows().count(), 1);

    //traffic arrives, including a frame that sorts ahead of the selected one and shifts it down
    model.addFrame(makeFrame(0x050), false);
    model.addFrame(makeFrame(0x400), false);
    model.sendBulkRefresh();

    //still exactly one row selected, and still the same frame
    QCOMPARE(selection.selectedRows().count(), 1);
    const QModelIndex stillSelected = selection.selectedRows().first();
    QCOMPARE(proxy.data(proxy.index(stillSelected.row(), 1), Qt::DisplayRole).toString().toUInt(nullptr, 16),
             (uint)0x200);
}

void TestFrameModel::sortedProxyStillUpdatesTheRightOverwriteRow()
{
    /* Overwrite mode maps each ID to a row of the model. Because the model no longer reorders
     * itself, that mapping stays valid no matter how the view is sorted - the bug where sorting
     * made updates land on an unrelated row cannot happen through this path. */
    CANFrameModel model;
    model.setOverwriteMode(true);
    QSortFilterProxyModel proxy;
    configureProxy(proxy, model);

    const uint32_t ids[] = {0x300, 0x100, 0x200};
    for (uint32_t id : ids) model.addFrame(makeFrame(id), false);
    model.sendBulkRefresh();

    proxy.sort(1, Qt::AscendingOrder);
    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x100, 0x200, 0x300}));

    CANFrame update = makeFrame(0x300);
    update.setPayload(QByteArray::fromHex("EEEEEEEE"));
    model.addFrame(update, false);
    model.sendBulkRefresh();

    //no row lost, no duplicate created
    QCOMPARE(shownIds(proxy), (QList<uint32_t>{0x100, 0x200, 0x300}));
    //the payload landed on 0x300's row (last in ascending order) and nowhere else
    QVERIFY(proxy.data(proxy.index(2, 8), Qt::DisplayRole).toString().toUpper().contains("EE"));
    QVERIFY(!proxy.data(proxy.index(0, 8), Qt::DisplayRole).toString().toUpper().contains("EE"));
}
