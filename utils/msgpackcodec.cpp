#include "msgpackcodec.h"

#include <QtEndian>
#include <cstring>

/*
 * Implementation notes: python-can packs a Message as a map of eleven fixed keys and unpacks it
 * with Message(**dict), so the key names and value types here have to match exactly. Reading is
 * deliberately tolerant - unknown keys and types are consumed and ignored - because this data
 * arrives over a network socket.
 */

void MsgPack::packStr(QByteArray &out, const char *str)
{
    const int len = (int)strlen(str);
    //every key we use is short so a fixstr always fits
    out.append((char)(0xA0 | (len & 0x1F)));
    out.append(str, len);
}

void MsgPack::packBool(QByteArray &out, bool val)
{
    out.append((char)(val ? 0xC3 : 0xC2));
}

void MsgPack::packNil(QByteArray &out)
{
    out.append((char)0xC0);
}

void MsgPack::packUInt(QByteArray &out, quint64 val)
{
    if (val < 0x80)
    {
        out.append((char)val); //positive fixint
        return;
    }
    if (val <= 0xFF)
    {
        out.append((char)0xCC);
        out.append((char)val);
        return;
    }
    if (val <= 0xFFFF)
    {
        out.append((char)0xCD);
        quint16 be = qToBigEndian((quint16)val);
        out.append((const char*)&be, 2);
        return;
    }
    if (val <= 0xFFFFFFFFull)
    {
        out.append((char)0xCE);
        quint32 be = qToBigEndian((quint32)val);
        out.append((const char*)&be, 4);
        return;
    }
    out.append((char)0xCF);
    quint64 be = qToBigEndian(val);
    out.append((const char*)&be, 8);
}

void MsgPack::packDouble(QByteArray &out, double val)
{
    out.append((char)0xCB);
    quint64 bits;
    memcpy(&bits, &val, sizeof(bits));
    bits = qToBigEndian(bits);
    out.append((const char*)&bits, 8);
}

void MsgPack::packBin(QByteArray &out, const QByteArray &val)
{
    out.append((char)0xC4); //bin8, our payloads are never longer than 255
    out.append((char)(val.length() & 0xFF));
    out.append(val);
}

void MsgPack::packMapHeader(QByteArray &out, int pairs)
{
    //python-can never sends more than a handful of keys so a fixmap always suffices
    out.append((char)(0x80 | (pairs & 0x0F)));
}

namespace {

static bool mpHave(const QByteArray &data, int pos, qint64 need)
{
    return need >= 0 && pos >= 0 && (qint64)pos + need <= data.length();
}

template<typename T> static bool mpTake(const QByteArray &data, int &pos, T &out)
{
    if (!mpHave(data, pos, (int)sizeof(T))) return false;
    T raw;
    memcpy(&raw, data.constData() + pos, sizeof(T));
    out = qFromBigEndian(raw);
    pos += sizeof(T);
    return true;
}

}

//reads one value of any type. Containers are consumed recursively so this doubles as a skip.
bool MsgPack::readValue(const QByteArray &data, int &pos, Value &val, int depth)
{
    if (depth > 8) return false; //nothing python-can sends is nested, stop runaway recursion
    if (!mpHave(data, pos, 1)) return false;

    const quint8 tag = (quint8)data.at(pos++);
    val = Value();

    if (tag <= 0x7F) { val.type = Value::UInt; val.u = tag; return true; }
    if (tag >= 0xE0) { val.type = Value::Int; val.i = (qint8)tag; return true; }

    if ((tag & 0xE0) == 0xA0) //fixstr
    {
        const int len = tag & 0x1F;
        if (!mpHave(data, pos, len)) return false;
        val.type = Value::Str;
        val.bytes = data.mid(pos, len);
        pos += len;
        return true;
    }

    if ((tag & 0xF0) == 0x80) //fixmap
    {
        const int count = tag & 0x0F;
        for (int i = 0; i < count * 2; i++)
        {
            Value tmp;
            if (!readValue(data, pos, tmp, depth + 1)) return false;
        }
        return true;
    }

    if ((tag & 0xF0) == 0x90) //fixarray
    {
        const int count = tag & 0x0F;
        for (int i = 0; i < count; i++)
        {
            Value tmp;
            if (!readValue(data, pos, tmp, depth + 1)) return false;
        }
        return true;
    }

    switch (tag)
    {
    case 0xC0: val.type = Value::Nil; return true;
    case 0xC2: val.type = Value::Bool; val.b = false; return true;
    case 0xC3: val.type = Value::Bool; val.b = true; return true;

    case 0xC4: case 0xC5: case 0xC6: //bin 8/16/32
    {
        quint32 len = 0;
        if (tag == 0xC4) { quint8 l; if (!mpTake(data, pos, l)) return false; len = l; }
        else if (tag == 0xC5) { quint16 l; if (!mpTake(data, pos, l)) return false; len = l; }
        else { quint32 l; if (!mpTake(data, pos, l)) return false; len = l; }
        if (!mpHave(data, pos, (qint64)len)) return false;
        val.type = Value::Bin;
        val.bytes = data.mid(pos, len);
        pos += len;
        return true;
    }

    case 0xD9: case 0xDA: case 0xDB: //str 8/16/32
    {
        quint32 len = 0;
        if (tag == 0xD9) { quint8 l; if (!mpTake(data, pos, l)) return false; len = l; }
        else if (tag == 0xDA) { quint16 l; if (!mpTake(data, pos, l)) return false; len = l; }
        else { quint32 l; if (!mpTake(data, pos, l)) return false; len = l; }
        if (!mpHave(data, pos, (qint64)len)) return false;
        val.type = Value::Str;
        val.bytes = data.mid(pos, len);
        pos += len;
        return true;
    }

    case 0xCA: //float32
    {
        quint32 bits;
        if (!mpTake(data, pos, bits)) return false;
        float f;
        memcpy(&f, &bits, sizeof(f));
        val.type = Value::Double;
        val.d = f;
        return true;
    }
    case 0xCB: //float64
    {
        quint64 bits;
        if (!mpTake(data, pos, bits)) return false;
        double d;
        memcpy(&d, &bits, sizeof(d));
        val.type = Value::Double;
        val.d = d;
        return true;
    }

    case 0xCC: { quint8 v;  if (!mpTake(data, pos, v)) return false; val.type = Value::UInt; val.u = v; return true; }
    case 0xCD: { quint16 v; if (!mpTake(data, pos, v)) return false; val.type = Value::UInt; val.u = v; return true; }
    case 0xCE: { quint32 v; if (!mpTake(data, pos, v)) return false; val.type = Value::UInt; val.u = v; return true; }
    case 0xCF: { quint64 v; if (!mpTake(data, pos, v)) return false; val.type = Value::UInt; val.u = v; return true; }
    case 0xD0: { quint8 v;  if (!mpTake(data, pos, v)) return false; val.type = Value::Int; val.i = (qint8)v; return true; }
    case 0xD1: { quint16 v; if (!mpTake(data, pos, v)) return false; val.type = Value::Int; val.i = (qint16)v; return true; }
    case 0xD2: { quint32 v; if (!mpTake(data, pos, v)) return false; val.type = Value::Int; val.i = (qint32)v; return true; }
    case 0xD3: { quint64 v; if (!mpTake(data, pos, v)) return false; val.type = Value::Int; val.i = (qint64)v; return true; }

    case 0xDC: case 0xDD: //array 16/32
    {
        quint32 count = 0;
        if (tag == 0xDC) { quint16 c; if (!mpTake(data, pos, c)) return false; count = c; }
        else { quint32 c; if (!mpTake(data, pos, c)) return false; count = c; }
        for (quint32 i = 0; i < count; i++)
        {
            Value tmp;
            if (!readValue(data, pos, tmp, depth + 1)) return false;
        }
        return true;
    }

    case 0xDE: case 0xDF: //map 16/32
    {
        quint32 count = 0;
        if (tag == 0xDE) { quint16 c; if (!mpTake(data, pos, c)) return false; count = c; }
        else { quint32 c; if (!mpTake(data, pos, c)) return false; count = c; }
        for (quint32 i = 0; i < count * 2; i++)
        {
            Value tmp;
            if (!readValue(data, pos, tmp, depth + 1)) return false;
        }
        return true;
    }

    default:
        //fixext / ext types, nothing python-can produces for a Message
        return false;
    }
}


//reads the outer map header and hands back how many key/value pairs follow
bool MsgPack::readMapHeader(const QByteArray &data, int &pos, int &count)
{
    if (!mpHave(data, pos, 1)) return false;

    const quint8 tag = (quint8)data.at(pos++);
    if ((tag & 0xF0) == 0x80) { count = tag & 0x0F; return true; }
    if (tag == 0xDE) { quint16 c; if (!mpTake(data, pos, c)) return false; count = c; return true; }
    if (tag == 0xDF) { quint32 c; if (!mpTake(data, pos, c)) return false; count = (int)c; return true; }
    return false;
}
