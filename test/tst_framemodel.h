#ifndef TST_FRAMEMODEL_H
#define TST_FRAMEMODEL_H

#include <QObject>

/*
 * Tests for how CANFrameModel tells the view about changes.
 *
 * This matters beyond correctness of the data: the model used to reset itself wholesale on every
 * refresh, and a reset makes QTableView discard everything it knows per row - the heights the user
 * set by expanding a row, and the selection. These tests pin down that ordinary incoming traffic
 * is reported as row insertions (or as changes to existing rows in overwrite mode) and that a
 * reset only happens when something structural really did change.
 */
class TestFrameModel: public QObject
{
    Q_OBJECT

private slots:
    //arriving frames must be announced as inserts, never as a reset
    void bulkRefreshInsertsRatherThanResets();
    //...and the rows must actually be there afterwards
    void bulkRefreshRowCountIsCorrect();
    //nothing new means nothing announced at all
    void bulkRefreshWithNoNewFramesIsSilent();
    //overwrite mode rewrites rows in place, so it reports changes not inserts
    void overwriteModeReportsDataChanged();
    //loading a file appends many rows at once
    void insertFramesAnnouncesAllRows();
    //clearing is structural, a reset is correct there
    void clearFramesResets();
    //several refreshes in a row must keep the view's idea of the row count in step
    void repeatedRefreshesStayConsistent();

    /* Sorting is done by a QSortFilterProxyModel over the top, not by the model itself. These
     * check the proxy orders rows properly and, importantly, that doing so no longer costs the
     * view its selection when frames arrive. */
    void proxySortsOnValuesNotDisplayedText();
    void proxyKeepsSortAsFramesArrive();
    void sourceModelStaysInArrivalOrder();
    void selectionSurvivesFramesArrivingWhileSorted();
    void sortedProxyStillUpdatesTheRightOverwriteRow();
};

#endif // TST_FRAMEMODEL_H
