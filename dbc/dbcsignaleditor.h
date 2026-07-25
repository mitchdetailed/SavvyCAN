#ifndef DBCSIGNALEDITOR_H
#define DBCSIGNALEDITOR_H

#include <QDialog>
#include "dbchandler.h"
#include "utility.h"

namespace Ui {
class DBCSignalEditor;
}

class DBCSignalEditor : public QDialog
{
    Q_OBJECT

public:
    explicit DBCSignalEditor(QWidget *parent = 0);
    void setMessageRef(DBC_MESSAGE *msg);
    void setFileIdx(int idx);
    void setSignalRef(DBC_SIGNAL *sig);
    void showEvent(QShowEvent*);
    ~DBCSignalEditor();
    void refreshView();

signals:
    void updatedTreeInfo(DBC_SIGNAL *sig);

private slots:
    void bitfieldDragBegin(int bit);
    void bitfieldDragMove(int bit);
    void bitfieldDragEnd();
    void bitfieldRightClicked(int bit);
    void onValuesCellChanged(int row,int col);
    void onCustomMenuValues(QPoint);
    void deleteCurrentValue();

private:
    Ui::DBCSignalEditor *ui;
    DBCHandler *dbcHandler;
    DBC_MESSAGE *dbcMessage;
    DBC_SIGNAL *currentSignal;
    QList<DBC_SIGNAL> undoBuffer;
    DBCFile *dbcFile;
    bool inhibitCellChanged;
    bool inhibitMsgProc;

    //state for dragging the current signal around the bit grid
    bool bitDragActive;   //did the drag start on a cell that actually belongs to us?
    bool bitDragMoved;    //has the signal actually shifted yet? (controls when we push undo)
    int bitDragAnchorBit; //the bit the drag was started on
    int bitDragStartBit;  //the signal's startBit when the drag began

    void fillSignalForm(DBC_SIGNAL *sig);
    void fillValueTable(DBC_SIGNAL *sig);
    void generateUsedBits();
    void refreshBitGrid();
    bool currentSignalCoversBit(int bit);
    //returns the bits the current signal occupies, in message bit numbering
    QList<int> currentSignalBits();
    //warn in the editor when the signal being edited shares bits with another one
    void checkForOverlap();
    bool signalFitsAtStartBit(int startBit);

    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void readSettings();
    void writeSettings();
    void pushToUndoBuffer();
    void popFromUndoBuffer();
};

#endif // DBCSIGNALEDITOR_H
