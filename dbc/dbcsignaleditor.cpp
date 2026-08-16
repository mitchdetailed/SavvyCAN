#include "dbcsignaleditor.h"
#include "ui_dbcsignaleditor.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QSettings>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QRegularExpression>
#include <qevent.h>
#include "helpwindow.h"

// For Motorola signals, DBC startBit is the MSB position. These helpers convert between
// DBC startBit (MSB) and the true LSB position using the standard Motorola bit traversal
// (decrement within byte, +15 jump across byte boundary).
static int motorolaLSBfromStartBit(int startBit, int signalSize)
{
    int bit = startBit;
    for (int i = 0; i < signalSize - 1; i++)
    {
        if (bit % 8 == 0) bit += 15;
        else bit--;
    }
    return bit;
}

static int motorolaStartBitFromLSB(int lsb, int signalSize)
{
    int bit = lsb;
    for (int i = 0; i < signalSize - 1; i++)
    {
        if (bit % 8 == 7) bit -= 15;
        else bit++;
    }
    return bit;
}

DBCSignalEditor::DBCSignalEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DBCSignalEditor)
{
    ui->setupUi(this);

    readSettings();

    dbcHandler = DBCHandler::getReference();
    dbcMessage = nullptr;
    currentSignal = nullptr;
    inhibitMsgProc = false;
    bitDragActive = false;
    bitDragMoved = false;
    bitDragAnchorBit = -1;
    bitDragStartBit = 0;

    QStringList headers2;
    headers2 << "Value" << "Text";
    ui->valuesTable->setColumnCount(2);
    ui->valuesTable->setColumnWidth(0, 200);
    ui->valuesTable->setColumnWidth(1, 440);
    ui->valuesTable->setHorizontalHeaderLabels(headers2);
    ui->valuesTable->horizontalHeader()->setStretchLastSection(true);

    ui->comboType->addItem("UNSIGNED INTEGER");
    ui->comboType->addItem("SIGNED INTEGER");
    ui->comboType->addItem("SINGLE PRECISION");
    ui->comboType->addItem("DOUBLE PRECISION");
    ui->comboType->addItem("STRING");

    ui->bitfield->setMode(GridMode::SIGNAL_VIEW);
    //the left button drags the signal being edited around rather than teleporting it to the click
    ui->bitfield->setDragEnabled(true);
    ui->bitfield->setToolTip(tr("Drag the highlighted signal to move it. Right click another signal to edit it instead."));

    connect(ui->bitfield, SIGNAL(gridDragBegin(int)), this, SLOT(bitfieldDragBegin(int)));
    connect(ui->bitfield, SIGNAL(gridDragMove(int)), this, SLOT(bitfieldDragMove(int)));
    connect(ui->bitfield, SIGNAL(gridDragEnd()), this, SLOT(bitfieldDragEnd()));
    connect(ui->bitfield, SIGNAL(gridRightClicked(int)), this, SLOT(bitfieldRightClicked(int)));

    connect(ui->valuesTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onCustomMenuValues(QPoint)));
    ui->valuesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->valuesTable, SIGNAL(cellChanged(int,int)), this, SLOT(onValuesCellChanged(int,int)));

    //now with 100% more lambda expressions just to make it interesting (and shorter, and easier...)
    connect(ui->cbIntelFormat, &QCheckBox::toggled,
            [=]()
            {
                if (currentSignal == nullptr) return;
                if (currentSignal->intelByteOrder != ui->cbIntelFormat->isChecked())
                {
                    dbcFile->setDirtyFlag();
                    pushToUndoBuffer();
                    currentSignal->intelByteOrder = ui->cbIntelFormat->isChecked();
                    //fillSignalForm(currentSignal);
                    refreshBitGrid();
                }
            });

    connect(ui->comboReceiver, &QComboBox::currentTextChanged,
            [=]()
            {
                if (currentSignal == nullptr) return;
                if (inhibitMsgProc) return;

                DBC_NODE *node = dbcFile->findNodeByName(ui->comboReceiver->currentText());
                if (currentSignal->receiver != node)
                {
                    dbcFile->setDirtyFlag();
                    pushToUndoBuffer();
                    currentSignal->receiver = node;
                }
            });
    connect(ui->comboType, &QComboBox::currentTextChanged,
            [=]()
            {
                if (currentSignal == nullptr) return;
                switch (ui->comboType->currentIndex())
                {
                case 0:
                    if (currentSignal->valType != UNSIGNED_INT)
                    {
                        pushToUndoBuffer();
                        currentSignal->valType = UNSIGNED_INT;
                        dbcFile->setDirtyFlag();
                        fillSignalForm(currentSignal);
                    }
                    break;
                case 1:
                    if (currentSignal->valType != SIGNED_INT)
                    {
                        pushToUndoBuffer();
                        currentSignal->valType = SIGNED_INT;
                        dbcFile->setDirtyFlag();
                        fillSignalForm(currentSignal);
                    }
                    break;
                case 2:
                    if (currentSignal->valType != SP_FLOAT)
                    {
                        pushToUndoBuffer();
                        currentSignal->valType = SP_FLOAT;
                        dbcFile->setDirtyFlag();
                        if (dbcMessage) //if we have a good msg reference we can use it to get the # of bytes expected.
                        {
                            int maxBit = ((dbcMessage->len * 8) - 32 + 7);
                            if (maxBit < 0) maxBit = 0;
                            if (currentSignal->startBit > maxBit) currentSignal->startBit = maxBit;
                        }
                        else if (currentSignal->startBit > 39) currentSignal->startBit = 39;
                        currentSignal->signalSize = 32;
                        fillSignalForm(currentSignal);
                    }
                    break;
                case 3:
                    if (currentSignal->valType != DP_FLOAT)
                    {
                        pushToUndoBuffer();
                        currentSignal->valType = DP_FLOAT;
                        dbcFile->setDirtyFlag();
                        if (dbcMessage)
                        {
                            int maxBit = ((dbcMessage->len * 8) - 64 + 7);
                            if (currentSignal->startBit > maxBit) currentSignal->startBit = maxBit;
                        }
                        else currentSignal->startBit = 7; //has to be!
                        currentSignal->signalSize = 64;
                        fillSignalForm(currentSignal);
                    }
                    break;
                case 4:
                    if (currentSignal->valType != STRING)
                    {
                        pushToUndoBuffer();
                        currentSignal->valType = STRING;
                        dbcFile->setDirtyFlag();
                        fillSignalForm(currentSignal);
                    }
                    break;
                }
            });
    connect(ui->txtBias, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtBias->text().toDouble(&result);
                if (result)
                {
                    if (currentSignal->bias != temp)
                    {
                        pushToUndoBuffer();
                        dbcFile->setDirtyFlag();
                        currentSignal->bias = temp;
                    }
                }
            });

    connect(ui->txtMaxVal, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtMaxVal->text().toDouble(&result);
                if (result)
                {
                    if (currentSignal->max != temp)
                    {
                        pushToUndoBuffer();
                        dbcFile->setDirtyFlag();
                        currentSignal->max = temp;
                    }
                }
            });

    connect(ui->txtMinVal, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtMinVal->text().toDouble(&result);
                if (result)
                {
                    if (currentSignal->min != temp)
                    {
                        pushToUndoBuffer();
                        dbcFile->setDirtyFlag();
                        currentSignal->min = temp;
                    }
                }
            });

    connect(ui->txtScale, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtScale->text().toDouble(&result);
                if (result)
                {
                    if (currentSignal->factor != temp)
                    {
                        pushToUndoBuffer();
                        dbcFile->setDirtyFlag();
                        currentSignal->factor = temp;
                    }
                }
            });

    connect(ui->txtComment, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                if (currentSignal->comment != ui->txtComment->text())
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->comment = ui->txtComment->text();
                    emit updatedTreeInfo(currentSignal);
                }
            });

    connect(ui->txtUnitName, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                if (currentSignal->unitName != ui->txtUnitName->text())
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->unitName = ui->txtUnitName->text();
                }
            });

    connect(ui->txtBitLength, &QLineEdit::textChanged,
            [=]()
            {
                if (currentSignal == nullptr) return;
                int temp;
                temp = Utility::ParseStringToNum(ui->txtBitLength->text());
                if (temp < 1) return;
                if (dbcMessage)
                {
                    if (temp > (int)(dbcMessage->len * 8)) return;
                }
                else if (temp > 64) return;

                if (currentSignal->valType == SP_FLOAT) temp = 32;
                if (currentSignal->valType == DP_FLOAT) temp = 64;

                if (currentSignal->signalSize != temp)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->signalSize = temp;
                    //fillSignalForm(currentSignal);
                    refreshBitGrid();
                }
            });

    connect(ui->txtStartBit, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                bool ok;
                int lsb = ui->txtStartBit->text().toInt(&ok);
                int maxBit = dbcMessage ? (int)(dbcMessage->len * 8) - 1 : 511;

                auto revert = [&]() {
                    if (currentSignal->intelByteOrder)
                        ui->txtStartBit->setText(QString::number(currentSignal->startBit));
                    else
                        ui->txtStartBit->setText(QString::number(motorolaLSBfromStartBit(currentSignal->startBit, currentSignal->signalSize)));
                };

                if (!ok || lsb < 0 || lsb > maxBit) { revert(); return; }

                int newDBCStartBit;
                if (currentSignal->intelByteOrder)
                {
                    // LSB == DBC startBit for Intel; MSB end must still fit in message
                    if (lsb + currentSignal->signalSize - 1 > maxBit) { revert(); return; }
                    newDBCStartBit = lsb;
                }
                else
                {
                    // Convert LSB back to DBC startBit (MSB position for Motorola)
                    newDBCStartBit = motorolaStartBitFromLSB(lsb, currentSignal->signalSize);
                    if (newDBCStartBit < 0 || newDBCStartBit > maxBit) { revert(); return; }
                }

                if (currentSignal->startBit != newDBCStartBit)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->startBit = newDBCStartBit;
                    refreshBitGrid();
                }
            });

    connect(ui->txtName, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                QString tempNameStr = ui->txtName->text().simplified().replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
                if (tempNameStr.length() == 0) return; //can't do that!
                if (currentSignal->name != tempNameStr)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->name = tempNameStr;
                    refreshBitGrid();
                    //need to update the tree too.
                    emit updatedTreeInfo(currentSignal);
                }
            });

    connect(ui->txtMultiplexValues, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                if (currentSignal->multiplexDbcString(DBC_SIGNAL::MuxStringFormat_UI) != ui->txtMultiplexValues->text())
                {
                    //TODO: could look up the multiplexor and ensure that the value is within a range that the multiplexor could return
                    QString errorString;
                    if (!currentSignal->parseDbcMultiplexUiString(ui->txtMultiplexValues->text(), errorString)) {
                        QMessageBox::critical(this, tr("Error"), tr("The multiplex values field contains errors:\n%1").arg(errorString));
                    } else {
                        pushToUndoBuffer();
                        dbcFile->setDirtyFlag();
                    }
                }
            });

    connect(ui->rbExtended, &QRadioButton::toggled,
            [=](bool state)
            {
                if (!currentSignal) return;
                if (!state) return; //we only need to handle the case where it is true

                //only do anything if this is different from the current state. It should be because we're in a toggle event but let's be sure
                if (!currentSignal->isMultiplexed || !currentSignal->isMultiplexor)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->isMultiplexed = true;
                    currentSignal->isMultiplexor = true;
                    //an extended multi signal cannot be the root multiplexor for a message so make sure to remove it if it was.
                    if (dbcMessage->multiplexorSignal == currentSignal) dbcMessage->multiplexorSignal = nullptr;
                    ui->txtMultiplexValues->setEnabled(currentSignal->isMultiplexed);
                    ui->cbMultiplexParent->setEnabled(currentSignal->isMultiplexed);
                    fillSignalForm(currentSignal);
                }
            });

    connect(ui->rbMultiplexed, &QRadioButton::toggled,
            [=](bool state)
            {
                if (!currentSignal) return;
                if (!state) return; //we only need to handle the case where it is true

                //only do anything if this is different from the current state. It should be because we're in a toggle event but let's be sure
                if (!currentSignal->isMultiplexed || currentSignal->isMultiplexor)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->isMultiplexed = true;
                    currentSignal->isMultiplexor = false;
                    //if the set multiplexor for the message was this signal then clear it
                    if (dbcMessage->multiplexorSignal == currentSignal) dbcMessage->multiplexorSignal = nullptr;
                    ui->txtMultiplexValues->setEnabled(currentSignal->isMultiplexed);
                    ui->cbMultiplexParent->setEnabled(currentSignal->isMultiplexed);
                    fillSignalForm(currentSignal);
                }
            });

    connect(ui->rbMultiplexor, &QRadioButton::toggled,
            [=](bool state)
            {
                if (!currentSignal) return;
                if (!state) return; //we only need to handle the case where it is true

                if (currentSignal->isMultiplexed || !currentSignal->isMultiplexor)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->isMultiplexed = false;
                    currentSignal->isMultiplexor = true;
                    //we just set that this is the multiplexor so update the message to show that as well.
                    dbcMessage->multiplexorSignal = currentSignal;
                    ui->txtMultiplexValues->setEnabled(currentSignal->isMultiplexed);
                    ui->cbMultiplexParent->setEnabled(currentSignal->isMultiplexed);
                    fillSignalForm(currentSignal);
                }
            });

    connect(ui->rbNotMulti, &QRadioButton::toggled,
            [=](bool state)
            {
                if (!currentSignal) return;
                if (!state) return; //we only need to handle the case where it is true

                if (currentSignal->isMultiplexed || currentSignal->isMultiplexor)
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    currentSignal->isMultiplexed = false;
                    currentSignal->isMultiplexor = false;
                    if (dbcMessage->multiplexorSignal == currentSignal) dbcMessage->multiplexorSignal = nullptr;
                    ui->txtMultiplexValues->setEnabled(currentSignal->isMultiplexed);
                    ui->cbMultiplexParent->setEnabled(currentSignal->isMultiplexed);
                    fillSignalForm(currentSignal);
                }
            });

    connect(ui->cbMultiplexParent, &QComboBox::textActivated,
            [=]()
            {
                if (currentSignal == nullptr) return;
                if (inhibitMsgProc) return;

                //qDebug() << "Curr text: :" << ui->cbMultiplexParent->currentText();
                //try to look up the signal that we're set to now, remove this signal from existing children list
                //add it to this one, update this signal's parent multiplexor
                DBC_SIGNAL *newSig = dbcMessage->sigHandler->findSignalByName(ui->cbMultiplexParent->currentText());
                DBC_SIGNAL *oldParent = currentSignal->multiplexParent;
                if (newSig && oldParent && (newSig != oldParent))
                {
                    pushToUndoBuffer();
                    dbcFile->setDirtyFlag();
                    oldParent->multiplexedChildren.removeOne(currentSignal);
                    currentSignal->multiplexParent = newSig;
                    newSig->multiplexedChildren.append(currentSignal);
                    refreshBitGrid();
                    emit updatedTreeInfo(currentSignal);
                }
            });

    installEventFilter(this);
}

DBCSignalEditor::~DBCSignalEditor()
{
    removeEventFilter(this);
    delete ui;
}

void DBCSignalEditor::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    writeSettings();
}

bool DBCSignalEditor::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key())
        {
        case Qt::Key_F1:
            HelpWindow::getRef()->showHelp("signaleditor.md");
            break;
        case Qt::Key_Z:
            if (keyEvent->modifiers() == Qt::ControlModifier)
            {
                popFromUndoBuffer();
            }
            break;
        }
        return true;
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
    return false;
}

void DBCSignalEditor::setFileIdx(int idx)
{
    if (idx < 0 || idx > dbcHandler->getFileCount() - 1) return;
    dbcFile = dbcHandler->getFileByIdx(idx);

    for (int x = 0; x < dbcFile->dbc_nodes.size(); x++)
    {
        ui->comboReceiver->addItem(dbcFile->dbc_nodes[x].name);
    }
}

void DBCSignalEditor::readSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("DBCSignalEditor/WindowSize", QSize(1000, 600)).toSize());
        move(Utility::constrainedWindowPos(settings.value("DBCSignalEditor/WindowPos", QPoint(100, 100)).toPoint()));
    }
}

void DBCSignalEditor::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("DBCSignalEditor/WindowSize", size());
        settings.setValue("DBCSignalEditor/WindowPos", pos());
    }
}


void DBCSignalEditor::setMessageRef(DBC_MESSAGE *msg)
{
    dbcMessage = msg;
}

void DBCSignalEditor::setSignalRef(DBC_SIGNAL *sig)
{
    currentSignal = sig;
}


void DBCSignalEditor::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    fillSignalForm(currentSignal);
    fillValueTable(currentSignal);
}

void DBCSignalEditor::refreshView()
{
    fillSignalForm(currentSignal);
    fillValueTable(currentSignal);
}

void DBCSignalEditor::onValuesCellChanged(int row,int col)
{
    if (inhibitCellChanged) return;

    if (row == ui->valuesTable->rowCount() - 1)
    {
        DBC_VAL_ENUM_ENTRY newVal;
        newVal.value = 0;
        newVal.descript = "No Description";
        currentSignal->valList.append(newVal);
        qDebug() << "Created new entry in value list";

        //QTableWidgetItem *widgetVal = new QTableWidgetItem(QString::number(newVal.value));
        //ui->valuesTable->setItem(row, 0, widgetVal);
        //QTableWidgetItem *widgetDesc = new QTableWidgetItem(newVal.descript);
        //ui->valuesTable->setItem(row, 1, widgetDesc);

        //add the blank at the end again
        ui->valuesTable->insertRow(ui->valuesTable->rowCount());
    }

    if (col == 0)
    {
        currentSignal->valList[row].value = Utility::ParseStringToNum(ui->valuesTable->item(row, col)->text());
    }
    else if (col == 1)
    {
        currentSignal->valList[row].descript = ui->valuesTable->item(row, col)->text();
    }
}

void DBCSignalEditor::onCustomMenuValues(QPoint point)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(tr("Delete currently selected value"), this, SLOT(deleteCurrentValue()));

    menu->popup(ui->valuesTable->mapToGlobal(point));
}

void DBCSignalEditor::deleteCurrentValue()
{
    int currIdx = ui->valuesTable->currentRow();
    if (currIdx > -1)
    {
        ui->valuesTable->removeRow(currIdx);
        currentSignal->valList.removeAt(currIdx);
    }
}

/* fillSignalForm also handles group "enabled" state */
/* WARNING: fillSignalForm can be called recursively since it is in the listener of cbIntelFormat */
void DBCSignalEditor::fillSignalForm(DBC_SIGNAL *sig)
{
    //sanity checks
    if (!dbcMessage) return;
    if (!dbcMessage->sigHandler) return;

    inhibitMsgProc = true;

    if (sig == nullptr) {
        //ui->groupBox->setEnabled(false);
        ui->txtName->setText("");
        ui->txtBias->setText("");
        ui->txtBitLength->setText("");
        ui->txtStartBit->setText("");
        ui->txtComment->setText("");
        ui->txtMaxVal->setText("");
        ui->txtMinVal->setText("");
        ui->txtScale->setText("");
        ui->txtUnitName->setText("");
        ui->txtMultiplexValues->setText("");
        ui->rbMultiplexed->setChecked(false);
        ui->rbMultiplexor->setChecked(false);
        ui->rbNotMulti->setChecked(true);
        ui->comboReceiver->setCurrentIndex(0);
        ui->comboType->setCurrentIndex(0);
        inhibitMsgProc = false;
        return;
    }

    /* we have a signal */
    //ui->groupBox->setEnabled(true);

    ui->txtName->setText(sig->name);
    ui->txtBias->setText(QString::number(sig->bias));
    ui->txtBitLength->setText(QString::number(sig->signalSize));
    {
        int lsb = sig->intelByteOrder ? sig->startBit
                                      : motorolaLSBfromStartBit(sig->startBit, sig->signalSize);
        ui->txtStartBit->setText(QString::number(lsb));
    }
    ui->txtMultiplexValues->setText(sig->multiplexDbcString(DBC_SIGNAL::MuxStringFormat_UI));
    ui->txtComment->setText(sig->comment);
    ui->txtMaxVal->setText(QString::number(sig->max));
    ui->txtMinVal->setText(QString::number(sig->min));
    ui->txtScale->setText(QString::number(sig->factor));
    ui->txtUnitName->setText(sig->unitName);
    if (sig->isMultiplexed && sig->isMultiplexor)
    {
        ui->rbMultiplexed->setChecked(false);
        ui->rbMultiplexor->setChecked(false);
        ui->rbNotMulti->setChecked(false);
        ui->rbExtended->setChecked(true);
    }
    else
    {
        ui->rbMultiplexed->setChecked(sig->isMultiplexed);
        ui->rbMultiplexor->setChecked(sig->isMultiplexor);
        ui->rbNotMulti->setChecked( !(sig->isMultiplexor | sig->isMultiplexed) );
    }
    qDebug() << sig->isMultiplexor << "*" << sig->isMultiplexed;

    ui->cbMultiplexParent->clear();

    int numSigs = dbcMessage->sigHandler->getCount();
    for (int i = 0; i < numSigs; i++)
    {
        DBC_SIGNAL *sig_iter = dbcMessage->sigHandler->findSignalByIdx(i);
        if (sig_iter && sig_iter->isMultiplexor && (sig_iter != sig))
        {
            //only add this entry if there are no other entries with that name yet
            if (ui->cbMultiplexParent->findText(sig_iter->name) == -1)
                ui->cbMultiplexParent->addItem(sig_iter->name);
            if (sig->multiplexParent == sig_iter) ui->cbMultiplexParent->setCurrentIndex(ui->cbMultiplexParent->count() - 1);
        }
    }

    ui->txtMultiplexValues->setEnabled(sig->isMultiplexed);
    ui->cbMultiplexParent->setEnabled(sig->isMultiplexed);

    ui->cbIntelFormat->setChecked(sig->intelByteOrder);

    switch (sig->valType)
    {
    case UNSIGNED_INT:
        ui->comboType->setCurrentIndex(0);
        break;
    case SIGNED_INT:
        ui->comboType->setCurrentIndex(1);
        break;
    case SP_FLOAT:
        ui->comboType->setCurrentIndex(2);
        break;
    case DP_FLOAT:
        ui->comboType->setCurrentIndex(3);
        break;
    case STRING:
        ui->comboType->setCurrentIndex(4);
        break;
    }

    if (sig->receiver)
    {
        for (int i = 0; i < ui->comboReceiver->count(); i++)
        {
            if (ui->comboReceiver->itemText(i) == sig->receiver->name)
            {
                ui->comboReceiver->setCurrentIndex(i);
                break;
            }
        }
    }

    refreshBitGrid();

    inhibitMsgProc = false;
}

void DBCSignalEditor::refreshBitGrid()
{
    unsigned char bitpattern[64];

    memset(bitpattern, 0, 64); //clear it out
    ui->bitfield->setReference(bitpattern, false);
    ui->bitfield->updateData(bitpattern, true);

    ui->bitfield->clearSignalNames();
    for (int x = 0; x < dbcMessage->sigHandler->getCount(); x++)
    {
        DBC_SIGNAL *sig = dbcMessage->sigHandler->findSignalByIdx(x);
        //only set a signal name for signals which match multiplexparent with our currentsignal
        if (!sig->multiplexParent || ((sig->multiplexParent == currentSignal->multiplexParent) && (sig->multiplexesIdenticalToSignal(currentSignal)) ))
        {
            ui->bitfield->setSignalNames(x, sig->name);
            //qDebug() << sig->name << sig->multiplexParent;
        }
    }

    generateUsedBits();
    memset(bitpattern, 0, 64); //clear it out first.

    int startBit, endBit;

    startBit = currentSignal->startBit;
    //the DBC parser doesn't bound the start bit so clamp it to the 64 byte grid before indexing bitpattern
    if (startBit < 0) startBit = 0;
    if (startBit > 511) startBit = 511;

    bitpattern[startBit / 8] |= 1 << (startBit % 8); //make the start bit a different color to set it apart
    ui->bitfield->setReference(bitpattern, false);

    if (currentSignal->intelByteOrder)
    {
        endBit = startBit + currentSignal->signalSize - 1;
        if (startBit < 0) startBit = 0;
        if (endBit > 511) endBit = 511;
        for (int y = startBit; y <= endBit; y++)
        {
            int byt = y / 8;
            bitpattern[byt] |= 1 << (y % 8);
        }
    }
    else //big endian / motorola format
    {
        //much more irritating than the intel version...
        int size = currentSignal->signalSize;
        while (size > 0)
        {
            int byt = startBit / 8;
            bitpattern[byt] |= 1 << (startBit % 8);
            size--;
            if ((startBit % 8) == 0) startBit += 15;
            else startBit--;
            if (startBit > 511) startBit = 511;
        }
    }

    ui->bitfield->updateData(bitpattern, true);
    {
        int lsb = currentSignal->intelByteOrder ? currentSignal->startBit
                                                : motorolaLSBfromStartBit(currentSignal->startBit, currentSignal->signalSize);
        ui->txtStartBit->setText(QString::number(lsb));
    }

    /* Every path that moves or resizes the signal ends up here, so this is the one place the
     * overlap warning needs refreshing from. */
    checkForOverlap();
}

/* fillValueTable also handles "enabled" state */
void DBCSignalEditor::fillValueTable(DBC_SIGNAL *sig)
{
    int rowIdx;

    inhibitCellChanged = true;

    ui->valuesTable->clearContents();
    ui->valuesTable->setRowCount(0);

    if (sig == nullptr) {
        ui->valuesTable->setEnabled(false);
        inhibitCellChanged = false;
        return;
    }

    ui->valuesTable->setEnabled(true);

    for (int i = 0; i < sig->valList.size(); i++)
    {
        QTableWidgetItem *val = new QTableWidgetItem(Utility::formatNumber((uint64_t)sig->valList[i].value));
        QTableWidgetItem *desc = new QTableWidgetItem(sig->valList[i].descript);
        rowIdx = ui->valuesTable->rowCount();
        ui->valuesTable->insertRow(rowIdx);
        ui->valuesTable->setItem(rowIdx, 0, val);
        ui->valuesTable->setItem(rowIdx, 1, desc);
    }

    //Add a blank row at the end to allow for adding records easily
    ui->valuesTable->insertRow(ui->valuesTable->rowCount());

    inhibitCellChanged = false;
}

//Does the signal we're editing occupy the given bit? The grid's usedSignalNum map can't answer
//this because signals are allowed to overlap and the last one written wins there. The grid draws
//the signal being edited on top of any overlap so grabbing it has to work the same way.
bool DBCSignalEditor::currentSignalCoversBit(int bit)
{
    if (!currentSignal) return false;

    const int size = currentSignal->signalSize;
    if (size < 1) return false;

    if (currentSignal->intelByteOrder)
        return (bit >= currentSignal->startBit) && (bit < currentSignal->startBit + size);

    //motorola walks back through the byte then jumps to the top of the next one
    int walk = currentSignal->startBit;
    for (int i = 0; i < size; i++)
    {
        if (walk == bit) return true;
        if (walk % 8 == 0) walk += 15;
        else walk--;
    }
    return false;
}

//the full set of bits the current signal sits on, in the same numbering the bit grid uses
QList<int> DBCSignalEditor::currentSignalBits()
{
    QList<int> bits;
    if (!currentSignal) return bits;

    const int size = currentSignal->signalSize;
    if (size < 1) return bits;

    if (currentSignal->intelByteOrder)
    {
        for (int i = 0; i < size; i++) bits.append(currentSignal->startBit + i);
    }
    else
    {
        int walk = currentSignal->startBit;
        for (int i = 0; i < size; i++)
        {
            bits.append(walk);
            if (walk % 8 == 0) walk += 15;
            else walk--;
        }
    }

    return bits;
}

/*
 * A DBC message is not supposed to have two signals sharing a bit, but nothing stops you doing it -
 * and dragging a signal around the grid makes it easy to do by accident. Rather than refusing the
 * edit (there are legitimate reasons to pass through an overlapping state while rearranging things)
 * this just says so, and names the signal that is in the way.
 */
void DBCSignalEditor::checkForOverlap()
{
    if (!ui->lblOverlapWarning) return;

    auto clear = [this]() {
        ui->lblOverlapWarning->setText("");
        ui->lblOverlapWarning->setStyleSheet("");
    };

    if (!currentSignal || !dbcMessage || !dbcMessage->sigHandler)
    {
        clear();
        return;
    }

    const QList<int> ourBits = currentSignalBits();
    if (ourBits.isEmpty())
    {
        clear();
        return;
    }

    //also flag a signal that has been pushed off the end of the message while we are here
    const int maxBit = (int)(dbcMessage->len * 8) - 1;
    QStringList problems;
    foreach (int bit, ourBits)
    {
        if (bit > maxBit)
        {
            problems << QString("extends past the end of the %1 byte message").arg(dbcMessage->len);
            break;
        }
    }

    QSet<QString> clashes;
    for (int x = 0; x < dbcMessage->sigHandler->getCount(); x++)
    {
        DBC_SIGNAL *other = dbcMessage->sigHandler->findSignalByIdx(x);
        if (!other || other == currentSignal) continue;

        /* Only signals that can be present at the same time can really clash. Two signals under
         * different multiplex values share the bits by design, which is the whole point of
         * multiplexing. */
        if (other->multiplexParent || currentSignal->multiplexParent)
        {
            if (other->multiplexParent != currentSignal->multiplexParent) continue;
            if (!other->multiplexesIdenticalToSignal(currentSignal)) continue;
        }

        //walk the other signal's bits the same way we walked ours
        const int otherSize = other->signalSize;
        if (otherSize < 1) continue;

        bool overlaps = false;
        if (other->intelByteOrder)
        {
            for (int i = 0; i < otherSize && !overlaps; i++)
                overlaps = ourBits.contains(other->startBit + i);
        }
        else
        {
            int walk = other->startBit;
            for (int i = 0; i < otherSize && !overlaps; i++)
            {
                overlaps = ourBits.contains(walk);
                if (walk % 8 == 0) walk += 15;
                else walk--;
            }
        }

        if (overlaps) clashes.insert(other->name);
    }

    if (!clashes.isEmpty())
    {
        QStringList names = clashes.values();
        names.sort();
        problems << QString("overlaps %1").arg(names.join(", "));
    }

    if (problems.isEmpty())
    {
        clear();
        return;
    }

    ui->lblOverlapWarning->setText(QString("⚠ This signal %1").arg(problems.join("; ")));
    ui->lblOverlapWarning->setStyleSheet("color: #b06000; font-weight: bold;");
}

//Would the current signal still fit inside the message if it started at the given bit?
bool DBCSignalEditor::signalFitsAtStartBit(int startBit)
{
    if (!currentSignal) return false;

    const int maxBit = dbcMessage ? (int)(dbcMessage->len * 8) - 1 : 511;
    if (startBit < 0 || startBit > maxBit) return false;

    if (currentSignal->intelByteOrder) return (startBit + currentSignal->signalSize - 1) <= maxBit;

    //motorola bits walk back through the byte then jump forward so the last bit is the highest one
    return motorolaLSBfromStartBit(startBit, currentSignal->signalSize) <= maxBit;
}

//Pressing the left button grabs the signal being edited so it can be dragged around the grid.
//Grabbing anywhere else isn't a valid grab, so say so with the system warning sound. Use right
//click to switch to editing whichever signal owns the bit you're pointing at.
void DBCSignalEditor::bitfieldDragBegin(int bit)
{
    bitDragActive = false;
    bitDragMoved = false;

    if (!currentSignalCoversBit(bit))
    {
        QApplication::beep();
        return;
    }

    bitDragActive = true;
    bitDragAnchorBit = bit;
    bitDragStartBit = currentSignal->startBit;
    ui->bitfield->setCursor(Qt::ClosedHandCursor);
}

//The grabbed cell follows the cursor and the rest of the signal comes along with it. Shifting
//startBit by however far the grabbed cell moved does exactly that for both byte orders.
void DBCSignalEditor::bitfieldDragMove(int bit)
{
    if (!bitDragActive || currentSignal == nullptr) return;

    const int newStartBit = bitDragStartBit + (bit - bitDragAnchorBit);
    if (newStartBit == currentSignal->startBit) return;
    //ran into the end of the message, just stay where we are until the cursor comes back
    if (!signalFitsAtStartBit(newStartBit)) return;

    if (!bitDragMoved)
    {
        //one undo entry for the whole drag instead of one per bit crossed
        pushToUndoBuffer();
        dbcFile->setDirtyFlag();
        bitDragMoved = true;
    }

    currentSignal->startBit = newStartBit;
    refreshBitGrid();
}

void DBCSignalEditor::bitfieldDragEnd()
{
    const bool moved = bitDragMoved;

    bitDragActive = false;
    bitDragMoved = false;
    ui->bitfield->unsetCursor();

    //the rest of the form shows the start bit too so bring it back in sync
    if (moved && currentSignal) fillSignalForm(currentSignal);
}

//Right clicking the grid starts editing on whichever signal currently "owns" that bit.
//If there is no other signal then nothing happens (right now).
//Would be possible to create a new signal in that case
void DBCSignalEditor::bitfieldRightClicked(int bit)
{
    //will return -1 if there is no signal there. Otherwise, returns signal number
    //which is quite luckily also the index into the signal handler table
    int sigNum = ui->bitfield->getUsedSignalNum(bit);
    if (sigNum < 0) return;

    pushToUndoBuffer(); // undo to resume editing the previous signal

    currentSignal = dbcMessage->sigHandler->findSignalByIdx(sigNum);

    if (currentSignal)
    {
        fillSignalForm(currentSignal);
        fillValueTable(currentSignal);
    }
}


void DBCSignalEditor::generateUsedBits()
{
    uint8_t usedBits[64];
    int startBit, endBit;

    memset(usedBits, 0, 64);

    if (!dbcMessage || !dbcMessage->sigHandler) return;

    //ownership is rebuilt from scratch, otherwise bits a signal has moved off of still claim it
    ui->bitfield->clearUsedSignalNums();

    for (int x = 0; x < dbcMessage->sigHandler->getCount(); x++)
    {
        DBC_SIGNAL *sig = dbcMessage->sigHandler->findSignalByIdx(x);

        //only pay attention to this signal if it's multiplexParent matches currentSignal or is null

        if (sig->multiplexParent)
        {
            if (sig->multiplexParent != currentSignal->multiplexParent) continue; //go thee away!
            if (!sig->multiplexesIdenticalToSignal(currentSignal)) continue; //buzz off
        }
        startBit = sig->startBit;

        if (sig->intelByteOrder)
        {
            endBit = startBit + sig->signalSize - 1;
            if (startBit < 0) startBit = 0;
            int maxBit = (dbcMessage->len * 8) - 1;
            if (endBit > maxBit) endBit = maxBit;
            for (int y = startBit; y <= endBit; y++)
            {
                int byt = y / 8;
                usedBits[byt] |= 1 << (y % 8);
                ui->bitfield->setUsedSignalNum(y, x);
            }
        }
        else //big endian / motorola format
        {
            //much more irritating than the intel version...
            int size = sig->signalSize;
            while (size > 0)
            {
                int byt = startBit / 8;
                usedBits[byt] |= 1 << (startBit % 8);
                ui->bitfield->setUsedSignalNum(startBit, x);
                size--;
                if ((startBit % 8) == 0) startBit += 15;
                else startBit--;
                int maxBit = (dbcMessage->len * 8) - 1;
                if (startBit > maxBit) startBit = maxBit;
            }
        }
    }
    ui->bitfield->setUsed(usedBits, false);
    //a drag reads cursor position in terms of the current layout so don't reshape the grid
    //out from under it. The message length can't change mid drag anyway.
    if (!bitDragActive) ui->bitfield->setBytesToDraw(dbcMessage->len);
}

//Copy the current signal in its entirety to the undo buffer. Just for safe keeping
//Called before an edit is done to save the state so we can revert if necessary
void DBCSignalEditor::pushToUndoBuffer()
{
    if (!currentSignal) return;
    //store a copy of the pointer so that if we need to pop we can pop to the proper place
    currentSignal->self = currentSignal;
    undoBuffer.append(*currentSignal); //save the whole thing
    qDebug() << "Pushing to undo buffer";
}

//Pop the last copy of a signal from the stack and begin editing it
void DBCSignalEditor::popFromUndoBuffer()
{
    if (undoBuffer.empty())
    {
        dbcFile->clearDirtyFlag(); //TODO: Don't do this. Implement per-item dirty flags.
        qDebug() << "Undo buffer empty";
        return; //can't pop if there are no stored entries!
    }
    qDebug() << "Popping undo buffer";
    DBC_SIGNAL sig = undoBuffer.back();
    undoBuffer.pop_back();
    currentSignal = sig.self; //restore the pointer
    *currentSignal = sig; //write the contents into the memory pointed to

    fillSignalForm(currentSignal);
    fillValueTable(currentSignal);
}
