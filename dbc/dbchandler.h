#ifndef DBCHANDLER_H
#define DBCHANDLER_H

#include <QObject>
#include "dbc_classes.h"
#include "can_structs.h"

    typedef enum
    {
        EXACT,
        J1939,
        GMLAN
    } MatchingCriteria_t;

/*
 * TODO:
 * Finish coding up the decoupled design
 *
*/
class DBCSignalHandler: public QObject
{
    Q_OBJECT
public:
    DBC_SIGNAL *findSignalByName(QString name);
    DBC_SIGNAL *findSignalByIdx(int idx);
    bool addSignal(DBC_SIGNAL &sig);
    bool removeSignal(DBC_SIGNAL *sig);
    bool removeSignal(int idx);
    bool removeSignal(QString name);
    void removeAllSignals();
    int getCount();
    void sort();

private:
    QList<DBC_SIGNAL> sigs; //signals is a reserved word or I'd have used that
};

class DBCMessageHandler: public QObject
{
    Q_OBJECT
public:
    DBC_MESSAGE *findMsgByID(uint32_t id);
    DBC_MESSAGE *findMsgByIdx(int idx);
    DBC_MESSAGE *findMsgByName(QString name);
    DBC_MESSAGE *findMsgByPartialName(QString name);
    QList<DBC_MESSAGE*> findMsgsByNode(DBC_NODE *node);
    bool addMessage(DBC_MESSAGE &msg);
    bool removeMessage(DBC_MESSAGE *msg);
    bool removeMessageByIndex(int idx);
    bool removeMessage(uint32_t ID);
    bool removeMessage(QString name);
    void removeAllMessages();
    int getCount();
    MatchingCriteria_t getMatchingCriteria();
    void setMatchingCriteria(MatchingCriteria_t mc);
    void setFilterLabeling( bool labelFiltering );
    bool filterLabeling();
    void sort();

private:
    QList<DBC_MESSAGE> messages;
    MatchingCriteria_t matchingCriteria;
    bool filterLabelingEnabled;
};

//technically there should be a node handler too but I'm sort of treating nodes as second class
//citizens since they aren't really all that important (to me anyway)
class DBCFile: public QObject
{
    Q_OBJECT
public:
    DBCFile();
    DBCFile(const DBCFile& cpy);
    DBCFile& operator=(const DBCFile& cpy);
    DBC_NODE *findNodeByName(QString name);
    DBC_NODE *findNodeByNameAndComment(QString fullname);
    DBC_NODE *findNodeByIdx(int idx);
    void addNode(const DBC_NODE &node);
    bool removeNodeByName(QString name);
    DBC_VAL_TABLE *findValueTableByName(QString name);
    DBC_ATTRIBUTE *findAttributeByName(QString name, DBC_ATTRIBUTE_TYPE type = ATTR_TYPE_ANY);
    DBC_ATTRIBUTE *findAttributeByIdx(int idx);
    void addAttribute(DBC_ATTRIBUTE &attr);
    void findAttributesByType(DBC_ATTRIBUTE_TYPE typ, QList<DBC_ATTRIBUTE> *list);
    bool saveFile(QString);
    bool loadFile(QString);
    QString getFullFilename();
    QString getFilename();
    QString getFilenameNoExt();
    /**
     * @brief Does this signal live inside this file?
     * @note Used when reacting to DBCHandler::dbcFileAboutToBeRemoved so a window can tell whether
     * a pointer it is holding is about to be destroyed. Compares by address, not by name.
     */
    bool ownsSignal(const DBC_SIGNAL *sig);
    QString getPath();
    int getAssocBus();
    void setAssocBus(int bus);
    void setDirtyFlag();
    bool getDirtyFlag();
    void clearDirtyFlag();
    void sort();
    void remapInternalPointers();

    DBCMessageHandler *messageHandler;
    QList<DBC_NODE> dbc_nodes;
    QList<DBC_VAL_TABLE> dbc_value_tables;
    QList<DBC_ATTRIBUTE> dbc_attributes;
    QList<DBC_SIGNAL> unassignedSignals;
private:
    QString fileName;
    QString filePath;
    int assocBuses; //-1 = all buses, 0 = first bus, 1 = second bus, etc.
    bool isDirty; //has the file been modified?

    QList<QString> snapshotNodeReferences();
    void restoreNodeReferences(const QList<QString> &nodeNames);
    bool parseAttribute(QString inpString, DBC_ATTRIBUTE &attr);
    QVariant processAttributeVal(QString input, DBC_ATTRIBUTE_VAL_TYPE typ);
    DBC_SIGNAL* parseSignalLine(QString line, DBC_MESSAGE *msg);
    bool parseSignalMultiplexValueLine(QString line);
    DBC_MESSAGE* parseMessageLine(QString line);
    bool parseValueTableLine(QString line);
    bool parseValueLine(QString line);
    bool parseSignalValueTypeLine(QString line);
    bool parseAttributeLine(QString line);
    bool parseDefaultAttrLine(QString line);
};

class DBCHandler: public QObject
{
    Q_OBJECT
public:
    DBCFile* loadDBCFile(QString filename);
    DBCFile* loadDBCFile(int);
    void saveDBCFile(int);
    void removeDBCFile(int);
    void removeAllFiles();
    void swapFiles(int pos1, int pos2);
    DBC_MESSAGE* findMessage(const CANFrame &frame);
    DBC_MESSAGE* findMessage(const QString msgName);
    DBC_MESSAGE* findMessage(const QString msgName, const QString fullyQualifiedNodeName);
    DBC_MESSAGE* findMessage(const QString msgName, const QString nodeName, const QString fileNameNoExt);
    //bus defaults to -1 which means "caller doesn't know the bus" and searches every loaded file
    DBC_MESSAGE* findMessage(uint32_t id, int bus = -1);
    DBC_MESSAGE* findMessageForFilter(uint32_t id, MatchingCriteria_t * matchingCriteria);
    int getFileCount();
    DBCFile* getFileByIdx(int idx);
    DBCFile* getFileByName(QString name);
    int createBlankFile();
    DBCFile* loadJSONFile(QString);
    DBCFile* loadSecretCSVFile(QString);
    static DBCHandler *getReference();

signals:
    /**
     * @brief Emitted just before a loaded DBC file is destroyed.
     * @param pFile - the file that is about to go away
     *
     * Windows that hold DBC_MESSAGE* or DBC_SIGNAL* pointers into a file must drop them when they
     * see this, because everything inside the file is deleted immediately afterwards. Use
     * DBCFile::ownsSignal() to work out whether a pointer you are holding belongs to it.
     */
    void dbcFileAboutToBeRemoved(DBCFile *pFile);

private:
    /* Files live on the heap and the list holds pointers, so a DBC_MESSAGE* or DBC_SIGNAL* handed
     * out to the rest of the program stays valid no matter how the list is grown, shuffled or
     * shrunk. Storing DBCFile by value here invalidated every outstanding pointer whenever the
     * list reallocated or an entry was removed, which crashed any window still showing a signal. */
    QList<DBCFile*> loadedFiles;

    DBCHandler();
    static DBCHandler *instance;
};

#endif // DBCHANDLER_H
