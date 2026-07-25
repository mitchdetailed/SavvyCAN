#ifndef MSGPACKCODEC_H
#define MSGPACKCODEC_H

#include <QByteArray>

/*
 * Just enough msgpack to interoperate with python-can, which packs a can.Message as a map of
 * fixed keys and unpacks it straight into the Message constructor.
 *
 * This lives on its own rather than inside the UDP multicast driver so it can be unit tested
 * without a socket - the encoding is fiddly enough that a silent mistake here would look like a
 * mysterious interop failure rather than an obvious bug.
 */
namespace MsgPack
{
    //writers
    void packStr(QByteArray &out, const char *str);
    void packBool(QByteArray &out, bool val);
    void packNil(QByteArray &out);
    void packUInt(QByteArray &out, quint64 val);
    void packDouble(QByteArray &out, double val);
    void packBin(QByteArray &out, const QByteArray &val);
    //a fixmap header for the given number of key/value pairs
    void packMapHeader(QByteArray &out, int pairs);

    //one decoded value. Containers are consumed whole and reported as Other.
    struct Value
    {
        enum Type { Nil, Bool, UInt, Int, Double, Str, Bin, Other };
        Type type = Other;
        bool b = false;
        quint64 u = 0;
        qint64 i = 0;
        double d = 0;
        QByteArray bytes;
    };

    /* Reads one value of any type, advancing pos. Containers are consumed recursively so this
     * doubles as a skip. Returns false on malformed or truncated input rather than reading past
     * the end - this parses data straight off the network. */
    bool readValue(const QByteArray &data, int &pos, Value &val, int depth = 0);

    //reads the outer map header and hands back how many key/value pairs follow
    bool readMapHeader(const QByteArray &data, int &pos, int &count);
}

#endif // MSGPACKCODEC_H
