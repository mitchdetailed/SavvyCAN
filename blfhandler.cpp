#include "blfhandler.h"
#include <QDebug>
#include <QFile>
#include <QString>
#include <QtEndian>

#define BLF_REMOTE_FLAG 0x80

BLFHandler::BLFHandler()
{

}

/*
 Written while peeking at source code here:
https://python-can.readthedocs.io/en/latest/_modules/can/io/blf.html
https://bitbucket.org/tobylorenz/vector_blf/

All the code actually below is freshly written but heavily based upon things seen in those
two source repos.
*/
bool BLFHandler::loadBLF(QString filename, QVector<CANFrame>* frames)
{
    BLF_OBJ_HEADER objHeader;
    QByteArray fileData;
    QByteArray uncompressedData;
    QByteArray junk;
    BLF_OBJECT obj;
    //signed so a runaway object size can't wrap it around into a huge positive offset
    int64_t pos;
    BLF_CAN_OBJ canObject;
    BLF_CAN_OBJ2 canObject2;

    QFile inFile(filename);

    if (!inFile.open(QIODevice::ReadOnly))
    {
        return false;
    }
    memset(&header, 0, sizeof(header));
    //a short read leaves the header partly uninitialized, so demand the whole thing
    if (inFile.read((char *)&header, sizeof(header)) != (qint64)sizeof(header))
    {
        qDebug() << "File is too short to even hold a BLF header";
        return false;
    }
    if (qFromLittleEndian(header.sig) == 0x47474F4C)
    {
        qDebug() << "Proper BLF file header token";
    }
    else
    {
        return false;
    }

    while (!inFile.atEnd())
    {
        qDebug() << "Position within file: " << inFile.pos();
        memset(&objHeader.base, 0, sizeof(BLF_OBJ_HEADER_BASE));
        if (inFile.read((char *)&objHeader.base, sizeof(BLF_OBJ_HEADER_BASE)) != (qint64)sizeof(BLF_OBJ_HEADER_BASE))
        {
            qDebug() << "Truncated object header, stopping here";
            break;
        }
        if (qFromLittleEndian(objHeader.base.sig) == 0x4A424F4C)
        {
            /* objSize comes straight out of the file. Anything smaller than the header it is
             * supposed to describe makes readSize negative, which used to hand a negative size to
             * read() and then memcpy out of the resulting empty buffer - a wild read and a crash. */
            if (objHeader.base.objSize < sizeof(BLF_OBJ_HEADER_BASE))
            {
                qDebug() << "Object claims an impossible size" << objHeader.base.objSize << "- aborting";
                break;
            }
            int readSize = objHeader.base.objSize - sizeof(BLF_OBJ_HEADER_BASE);
            qDebug() << "Proper object header token. Read Size: " << readSize;
            fileData = inFile.read(readSize);
            junk = inFile.read(readSize % 4); //file is padded so sizes must always end up on even multiple of 4
            //qDebug() << "Fudge bytes in readSize: " << (readSize % 4);

            switch (objHeader.base.objType)
            {
                case BLF_CONTAINER:
                qDebug() << "Object is a container.";
                //the file can end mid object, in which case there is nothing to copy out of
                if (fileData.size() < (int)sizeof(BLF_OBJ_HEADER_CONTAINER))
                {
                    qDebug() << "Truncated container object, aborting";
                    return frames->count() > 0;
                }
                memcpy(&objHeader.containerObj, fileData.constData(), sizeof(BLF_OBJ_HEADER_CONTAINER));
                fileData.remove(0, sizeof(BLF_OBJ_HEADER_CONTAINER));
                if (objHeader.containerObj.compressionMethod == BLF_CONT_NO_COMPRESSION)
                {
                    qDebug() << "Container is not compressed";
                    uncompressedData = fileData;
                }
                else if (objHeader.containerObj.compressionMethod == BLF_CONT_ZLIB_COMPRESSION)
                {
                    qDebug() << "Compressed container. Unpacking it.";
                    fileData.prepend(objHeader.containerObj.uncompressedSize & 0xFF);
                    fileData.prepend((objHeader.containerObj.uncompressedSize >> 8) & 0xFF);
                    fileData.prepend((objHeader.containerObj.uncompressedSize >> 16) & 0xFF);
                    fileData.prepend((objHeader.containerObj.uncompressedSize >> 24) & 0xFF);
                    uncompressedData += qUncompress(fileData);
                }
                else
                {
                    qDebug() << "Dunno what this is... " << objHeader.containerObj.compressionMethod;
                }
                qDebug() << "Uncompressed size: " << uncompressedData.size();
                qDebug() << "Currently loaded frames at this point: " << frames->count();
                pos = 0;
                //bool foundHeader = false;
                //first skip forward to find a header signature - usually not necessary
                while ( (pos + (int64_t)sizeof(BLF_OBJ_HEADER)) < (int64_t)uncompressedData.size())
                {
                    int32_t *headerSig = (int32_t *)(uncompressedData.constData() + pos);
                    if (*headerSig == 0x4A424F4C) break;
                    pos += 4;
                }
                //then process all the objects
                while ( (pos + (int64_t)sizeof(BLF_OBJ_HEADER)) < (int64_t)uncompressedData.size())
                {
                    memcpy(&obj.header.base, (uncompressedData.constData() + pos), sizeof(BLF_OBJ_HEADER_BASE));
                    memcpy(&obj.header.v1Obj, (uncompressedData.constData() + pos) + sizeof(BLF_OBJ_HEADER_BASE), sizeof(BLF_OBJ_HEADER_V1));
                    /* A zero (or absurd) object size leaves pos where it was and this loop spins
                     * forever on the same bytes, hanging the program on a corrupt file. */
                    if (obj.header.base.objSize < sizeof(BLF_OBJ_HEADER))
                    {
                        qDebug() << "Object inside container claims size" << obj.header.base.objSize << "- aborting";
                        break;
                    }
                    //if (obj.header.base.objType != 1)
                        //qDebug() << "Pos: " << pos << " Type: " << obj.header.base.objType << "Obj Size: " << obj.header.base.objSize;
                    if (qFromLittleEndian(objHeader.base.sig) == 0x4A424F4C)
                    {
                        fileData = uncompressedData.mid(pos + sizeof(BLF_OBJ_HEADER_BASE) + sizeof(BLF_OBJ_HEADER_V1), obj.header.base.objSize - sizeof(BLF_OBJ_HEADER_BASE) - sizeof(BLF_OBJ_HEADER_V1));
                        if (obj.header.base.objType == BLF_CAN_MSG)
                        {
                            if (fileData.size() < (int)sizeof(BLF_CAN_OBJ)) { pos += obj.header.base.objSize + (obj.header.base.objSize % 4); break; }
                            memcpy(&canObject, fileData.constData(), sizeof(BLF_CAN_OBJ));
                            CANFrame frame;
                            frame.bus = canObject.channel;
                            frame.setExtendedFrameFormat((canObject.id & 0x80000000ull)?true:false);
                            frame.setFrameId(canObject.id & 0x1FFFFFFFull);
                            frame.isReceived = true;
                            int safeDlc = qMin((int)canObject.dlc, (int)sizeof(canObject.data));
                            QByteArray bytes(safeDlc, 0);

                            if (canObject.flags & BLF_REMOTE_FLAG) {
                                frame.setFrameType(QCanBusFrame::RemoteRequestFrame);
                            } else {
                                frame.setFrameType(QCanBusFrame::DataFrame);
                                for (int i = 0; i < safeDlc; i++) bytes[i] = canObject.data[i];
                            }
                            frame.setPayload(bytes);
                            //Should we divide by a thousand or a million? Unsure here. It appears some logs are stamped in microseconds and some in milliseconds?
                            frame.setTimeStamp(QCanBusFrame::TimeStamp(0, obj.header.v1Obj.uncompSize / 1000.0)); //uncompsize field also used for timestamp oddly enough
                            frames->append(frame);
                        }
                        else if (obj.header.base.objType == BLF_CAN_MSG2)
                        {
                            if (fileData.size() < (int)sizeof(BLF_CAN_OBJ2)) { pos += obj.header.base.objSize + (obj.header.base.objSize % 4); break; }
                            memcpy(&canObject2, fileData.constData(), sizeof(BLF_CAN_OBJ2));
                            CANFrame frame;
                            frame.bus = canObject2.channel;
                            frame.setExtendedFrameFormat((canObject2.id & 0x80000000ull)?true:false);
                            frame.setFrameId(canObject2.id & 0x1FFFFFFFull);
                            frame.isReceived = true;
                            int safeDlc2 = qMin((int)canObject2.dlc, (int)sizeof(canObject2.data));
                            QByteArray bytes(safeDlc2, 0);

                            if (canObject2.flags & BLF_REMOTE_FLAG) {
                                frame.setFrameType(QCanBusFrame::RemoteRequestFrame);
                            } else {
                                frame.setFrameType(QCanBusFrame::DataFrame);
                                for (int i = 0; i < safeDlc2; i++) bytes[i] = canObject2.data[i];
                            }
                            frame.setPayload(bytes);
                            //Should we divide by a thousand or a million? Unsure here. It appears some logs are stamped in microseconds and some in milliseconds?
                            frame.setTimeStamp(QCanBusFrame::TimeStamp(0, obj.header.v1Obj.uncompSize / 1000.0)); //uncompsize field also used for timestamp oddly enough
                            frames->append(frame);
                        }
                        else
                        {
                            qDebug() << "Not a can frame! ObjType: " << obj.header.base.objType;
                        }
                        pos += obj.header.base.objSize + (obj.header.base.objSize % 4);
                    }
                    else
                    {
                        qDebug() << "Unexpected object header signature, aborting";
                        return false;
                    }
                }
                uncompressedData.remove(0, pos);
                qDebug() << "After removing used data uncompressedData is now this big: " << uncompressedData.size();

                break;
            }
        }
        else return false;
    }
    return true;
}

bool BLFHandler::saveBLF(QString filename, QVector<CANFrame> *frames)
{
    Q_UNUSED(filename)
    Q_UNUSED(frames)
    return false;
}
