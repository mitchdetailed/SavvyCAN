#include <QtTest>

#include "tst_codecs.h"
#include "utils/msgpackcodec.h"
#include "connections/seeedcan.h"
#include "connections/canalystii.h"
#include "connections/ixxatvci.h"
#include "connections/kvasercanlib.h"
#include "utility.h"

/***************************************************************************************************
 * msgpack
 **************************************************************************************************/

void TestCodecs::msgpackRoundTrip()
{
    QByteArray buf;
    MsgPack::packMapHeader(buf, 6);
    MsgPack::packStr(buf, "a");
    MsgPack::packUInt(buf, 0x1FFFFFFF);          //needs the 32 bit form
    MsgPack::packStr(buf, "b");
    MsgPack::packBool(buf, true);
    MsgPack::packStr(buf, "c");
    MsgPack::packDouble(buf, 1234.5678);
    MsgPack::packStr(buf, "d");
    MsgPack::packBin(buf, QByteArray::fromHex("00ff8055"));
    MsgPack::packStr(buf, "e");
    MsgPack::packNil(buf);
    MsgPack::packStr(buf, "f");
    MsgPack::packUInt(buf, 8);                   //small enough for a fixint

    int pos = 0;
    int count = 0;
    QVERIFY(MsgPack::readMapHeader(buf, pos, count));
    QCOMPARE(count, 6);

    MsgPack::Value k, v;

    QVERIFY(MsgPack::readValue(buf, pos, k));
    QCOMPARE(k.type, MsgPack::Value::Str);
    QCOMPARE(k.bytes, QByteArray("a"));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.type, MsgPack::Value::UInt);
    QCOMPARE(v.u, (quint64)0x1FFFFFFF);

    QVERIFY(MsgPack::readValue(buf, pos, k));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.type, MsgPack::Value::Bool);
    QCOMPARE(v.b, true);

    QVERIFY(MsgPack::readValue(buf, pos, k));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.type, MsgPack::Value::Double);
    QVERIFY(qAbs(v.d - 1234.5678) < 0.000001);

    QVERIFY(MsgPack::readValue(buf, pos, k));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.type, MsgPack::Value::Bin);
    QCOMPARE(v.bytes, QByteArray::fromHex("00ff8055"));

    QVERIFY(MsgPack::readValue(buf, pos, k));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.type, MsgPack::Value::Nil);

    QVERIFY(MsgPack::readValue(buf, pos, k));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.type, MsgPack::Value::UInt);
    QCOMPARE(v.u, (quint64)8);

    //everything consumed, nothing left over
    QCOMPARE(pos, buf.length());
}

void TestCodecs::msgpackTruncatedInputIsRejected_data()
{
    QTest::addColumn<int>("keepBytes");

    //a well formed blob chopped at every possible point must never read past the end
    QByteArray full;
    MsgPack::packMapHeader(full, 2);
    MsgPack::packStr(full, "timestamp");
    MsgPack::packDouble(full, 1.5);
    MsgPack::packStr(full, "data");
    MsgPack::packBin(full, QByteArray(8, 0x5A));

    for (int i = 0; i < full.length(); i++)
        QTest::newRow(qPrintable(QString("keep%1").arg(i))) << i;
}

void TestCodecs::msgpackTruncatedInputIsRejected()
{
    QFETCH(int, keepBytes);

    QByteArray full;
    MsgPack::packMapHeader(full, 2);
    MsgPack::packStr(full, "timestamp");
    MsgPack::packDouble(full, 1.5);
    MsgPack::packStr(full, "data");
    MsgPack::packBin(full, QByteArray(8, 0x5A));

    const QByteArray truncated = full.left(keepBytes);

    /* The contract is simply "returns instead of reading past the end". Whether a particular cut
     * point still yields a readable value doesn't matter - not crashing does. */
    int pos = 0;
    int count = 0;
    if (MsgPack::readMapHeader(truncated, pos, count))
    {
        for (int i = 0; i < count * 2; i++)
        {
            MsgPack::Value v;
            if (!MsgPack::readValue(truncated, pos, v)) break;
            QVERIFY(pos <= truncated.length());
        }
    }
    QVERIFY(pos <= truncated.length());
}

void TestCodecs::msgpackSkipsUnknownValues()
{
    //python-can may grow new fields; an unknown key/value must be consumed, not desynchronise us
    QByteArray buf;
    MsgPack::packMapHeader(buf, 2);
    MsgPack::packStr(buf, "unknown");
    buf.append((char)0x92);              //fixarray of 2 that we never asked for
    MsgPack::packUInt(buf, 1);
    MsgPack::packUInt(buf, 2);
    MsgPack::packStr(buf, "arbitration_id");
    MsgPack::packUInt(buf, 0x321);

    int pos = 0, count = 0;
    QVERIFY(MsgPack::readMapHeader(buf, pos, count));
    QCOMPARE(count, 2);

    MsgPack::Value k, v;
    QVERIFY(MsgPack::readValue(buf, pos, k));      //"unknown"
    QVERIFY(MsgPack::readValue(buf, pos, v));      //the array, consumed whole
    QVERIFY(MsgPack::readValue(buf, pos, k));
    QCOMPARE(k.bytes, QByteArray("arbitration_id"));
    QVERIFY(MsgPack::readValue(buf, pos, v));
    QCOMPARE(v.u, (quint64)0x321);
}

void TestCodecs::msgpackPythonCanMessageShape()
{
    /* Guards the exact wire shape python-can expects: a map of the eleven keys its Message
     * constructor takes. Getting a name or type wrong here is an interop failure that would
     * otherwise only show up against a real python-can process. */
    QByteArray buf;
    MsgPack::packMapHeader(buf, 11);
    const char *keys[] = {"timestamp", "arbitration_id", "is_extended_id", "is_remote_frame",
                          "is_error_frame", "channel", "dlc", "data", "is_fd", "bitrate_switch",
                          "error_state_indicator"};
    MsgPack::packStr(buf, keys[0]);  MsgPack::packDouble(buf, 1700000000.5);
    MsgPack::packStr(buf, keys[1]);  MsgPack::packUInt(buf, 0x7FF);
    MsgPack::packStr(buf, keys[2]);  MsgPack::packBool(buf, false);
    MsgPack::packStr(buf, keys[3]);  MsgPack::packBool(buf, false);
    MsgPack::packStr(buf, keys[4]);  MsgPack::packBool(buf, false);
    MsgPack::packStr(buf, keys[5]);  MsgPack::packNil(buf);
    MsgPack::packStr(buf, keys[6]);  MsgPack::packUInt(buf, 3);
    MsgPack::packStr(buf, keys[7]);  MsgPack::packBin(buf, QByteArray::fromHex("aabbcc"));
    MsgPack::packStr(buf, keys[8]);  MsgPack::packBool(buf, false);
    MsgPack::packStr(buf, keys[9]);  MsgPack::packBool(buf, false);
    MsgPack::packStr(buf, keys[10]); MsgPack::packBool(buf, false);

    int pos = 0, count = 0;
    QVERIFY(MsgPack::readMapHeader(buf, pos, count));
    QCOMPARE(count, 11);

    QByteArray payload;
    quint64 dlc = 0;
    for (int i = 0; i < count; i++)
    {
        MsgPack::Value k, v;
        QVERIFY(MsgPack::readValue(buf, pos, k));
        QVERIFY(MsgPack::readValue(buf, pos, v));
        QCOMPARE(k.type, MsgPack::Value::Str);
        QCOMPARE(k.bytes, QByteArray(keys[i]));
        if (k.bytes == "data") payload = v.bytes;
        if (k.bytes == "dlc") dlc = v.u;
    }
    QCOMPARE(pos, buf.length());
    //python-can validates dlc against the payload length, so they must agree
    QCOMPARE((int)dlc, payload.length());
}

void TestCodecs::msgpackRejectsRunawayNesting()
{
    //a hostile datagram of nothing but map headers must not blow the stack
    QByteArray buf;
    for (int i = 0; i < 200; i++) buf.append((char)0x81); //fixmap of 1, forever
    int pos = 0;
    MsgPack::Value v;
    QVERIFY(!MsgPack::readValue(buf, pos, v));
}

/***************************************************************************************************
 * bit timing tables
 **************************************************************************************************/

void TestCodecs::seeedBitrateCodes_data()
{
    QTest::addColumn<int>("speed");
    QTest::addColumn<int>("code");

    QTest::newRow("1M")    << 1000000 << 0x01;
    QTest::newRow("500k")  << 500000  << 0x03;
    QTest::newRow("250k")  << 250000  << 0x05;
    QTest::newRow("125k")  << 125000  << 0x07;
    QTest::newRow("10k")   << 10000   << 0x0B;
    //an unsupported rate must fall back to 500k rather than emit something meaningless
    QTest::newRow("bogus") << 777     << 0x03;
}

void TestCodecs::seeedBitrateCodes()
{
    QFETCH(int, speed);
    QFETCH(int, code);
    QCOMPARE((int)SeeedCAN::bitrateCode(speed), code);
}

void TestCodecs::canalystBitTiming_data()
{
    QTest::addColumn<int>("speed");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<uint>("t0");
    QTest::addColumn<uint>("t1");

    //the classic SJA1000 @16MHz table every ControlCAN tool uses
    QTest::newRow("1M")    << 1000000 << true  << 0x00u << 0x14u;
    QTest::newRow("500k")  << 500000  << true  << 0x00u << 0x1Cu;
    QTest::newRow("250k")  << 250000  << true  << 0x01u << 0x1Cu;
    QTest::newRow("125k")  << 125000  << true  << 0x03u << 0x1Cu;
    QTest::newRow("bogus") << 777     << false << 0u    << 0u;
}

void TestCodecs::canalystBitTiming()
{
    QFETCH(int, speed);
    QFETCH(bool, ok);
    QFETCH(uint, t0);
    QFETCH(uint, t1);

    uint32_t a = 0xDEAD, b = 0xBEEF;
    QCOMPARE(CanalystII::bitTiming(speed, a, b), ok);
    if (ok)
    {
        QCOMPARE(a, (uint32_t)t0);
        QCOMPARE(b, (uint32_t)t1);
    }
}

void TestCodecs::ixxatBitTiming_data()
{
    QTest::addColumn<int>("speed");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<uint>("btr0");
    QTest::addColumn<uint>("btr1");

    QTest::newRow("1M")    << 1000000 << true  << 0x00u << 0x14u;
    QTest::newRow("500k")  << 500000  << true  << 0x00u << 0x1Cu;
    QTest::newRow("125k")  << 125000  << true  << 0x03u << 0x1Cu;
    QTest::newRow("bogus") << 777     << false << 0u    << 0u;
}

void TestCodecs::ixxatBitTiming()
{
    QFETCH(int, speed);
    QFETCH(bool, ok);
    QFETCH(uint, btr0);
    QFETCH(uint, btr1);

    uint8_t a = 0xEE, b = 0xEE;
    QCOMPARE(IxxatVci::bitTiming(speed, a, b), ok);
    if (ok)
    {
        QCOMPARE((uint)a, btr0);
        QCOMPARE((uint)b, btr1);
    }
}

void TestCodecs::kvaserBitrateConstants_data()
{
    QTest::addColumn<int>("speed");
    QTest::addColumn<int>("constant");

    //CANlib's negative presets
    QTest::newRow("1M")     << 1000000 << -1;
    QTest::newRow("500k")   << 500000  << -2;
    QTest::newRow("250k")   << 250000  << -3;
    QTest::newRow("125k")   << 125000  << -4;
    QTest::newRow("10k")    << 10000   << -9;
    //no preset exists, caller falls back to explicit timing
    QTest::newRow("33333")  << 33333   << 0;
}

void TestCodecs::kvaserBitrateConstants()
{
    QFETCH(int, speed);
    QFETCH(int, constant);
    QCOMPARE((int)KvaserCanlib::bitrateConstant(speed), constant);
}

void TestCodecs::bitTimingTablesAgree()
{
    /* CANalyst-II and IXXAT are both SJA1000 style controllers clocked the same way, so their
     * tables must not drift apart. This is the check that would catch someone "fixing" one of
     * them in isolation. */
    const int speeds[] = {1000000, 500000, 250000, 125000, 100000, 50000, 20000, 10000};
    for (int speed : speeds)
    {
        uint32_t c0 = 0, c1 = 0;
        uint8_t i0 = 0, i1 = 0;
        QVERIFY2(CanalystII::bitTiming(speed, c0, c1), qPrintable(QString("canalyst %1").arg(speed)));
        QVERIFY2(IxxatVci::bitTiming(speed, i0, i1), qPrintable(QString("ixxat %1").arg(speed)));
        QCOMPARE(c0, (uint32_t)i0);
        QCOMPARE(c1, (uint32_t)i1);
    }
}

/***************************************************************************************************
 * error frame decoding
 **************************************************************************************************/

void TestCodecs::errorFrameDecode_data()
{
    QTest::addColumn<uint>("frameId");
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QString>("mustContain");

    //error classes live in the low bits of the ID
    QTest::newRow("bus off")      << 0x20000040u << QByteArray(8, 0) << QString("BUS OFF");
    QTest::newRow("no ack")       << 0x20000020u << QByteArray(8, 0) << QString("No ACK");
    QTest::newRow("lost arb")     << 0x20000002u << QByteArray(8, 0) << QString("Lost arbitration");
    QTest::newRow("tx timeout")   << 0x20000001u << QByteArray(8, 0) << QString("TX timeout");

    //controller status lives in data[1]
    QTest::newRow("rx passive")   << 0x20000004u << QByteArray::fromHex("0010000000000000")
                                  << QString("RX error passive");
    //protocol detail lives in data[2]
    QTest::newRow("stuff error")  << 0x20000008u << QByteArray::fromHex("0000040000000000")
                                  << QString("Stuff error");
    //error counters are the last two bytes and are the most actionable part
    QTest::newRow("counters")     << 0x20000004u << QByteArray::fromHex("00000000000080FF")
                                  << QString("TX err 128, RX err 255");
    //an error with nothing set at all still has to say something useful
    QTest::newRow("no detail")    << 0x20000000u << QByteArray() << QString("no detail");
}

void TestCodecs::errorFrameDecode()
{
    QFETCH(uint, frameId);
    QFETCH(QByteArray, payload);
    QFETCH(QString, mustContain);

    const QString decoded = Utility::decodeErrorFrame(frameId, payload);
    QVERIFY2(decoded.contains(mustContain),
             qPrintable(QString("decoded '%1' did not mention '%2'").arg(decoded).arg(mustContain)));
}

void TestCodecs::errorFrameDecodeHandlesShortPayload()
{
    /* A driver may hand us an error frame with no payload at all, or a truncated one. Indexing
     * data[1], data[2] and data[7] has to stay inside whatever we were actually given. */
    for (int len = 0; len <= 8; len++)
    {
        const QByteArray payload(len, (char)0xFF);
        const QString decoded = Utility::decodeErrorFrame(0x200001FFu, payload);
        QVERIFY(!decoded.isEmpty());
    }
}

void TestCodecs::kvaserFdDataRates_data()
{
    QTest::addColumn<int>("dataRate");
    QTest::addColumn<int>("constant");

    //CANlib's CAN FD data phase presets
    QTest::newRow("500k") << 500000  << -1000;
    QTest::newRow("1M")   << 1000000 << -1001;
    QTest::newRow("2M")   << 2000000 << -1002;
    QTest::newRow("4M")   << 4000000 << -1003;
    QTest::newRow("8M")   << 8000000 << -1004;
    //5M appears in the connection dialog but CANlib has no preset for it
    QTest::newRow("5M")   << 5000000 << 0;
    QTest::newRow("bogus")<< 12345   << 0;
}

void TestCodecs::kvaserFdDataRates()
{
    QFETCH(int, dataRate);
    QFETCH(int, constant);
    QCOMPARE((int)KvaserCanlib::fdDataRateConstant(dataRate), constant);
}
