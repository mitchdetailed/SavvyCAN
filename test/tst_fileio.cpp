#include <QtTest>
#include <QFile>
#include <QVector>

#include "tst_fileio.h"
#include "framefileio.h"

//every loader is a plain static so they can be driven straight from a table
typedef bool (*LoaderFn)(QString, QVector<CANFrame>*);

Q_DECLARE_METATYPE(LoaderFn)

void TestFileIO::initTestCase()
{
    QVERIFY2(mDir.isValid(), "could not create a temporary directory for the test corpus");
}

QString TestFileIO::path(const QString &name) const
{
    return mDir.path() + "/" + name;
}

void TestFileIO::writeFile(const QString &name, const QByteArray &data)
{
    QFile f(path(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
}

/***************************************************************************************************
 * good files still load
 **************************************************************************************************/

void TestFileIO::canDumpRoundTrip()
{
    writeFile("good.log",
              "(1609459200.000000) can0 123#DEADBEEF\n"
              "(1609459200.001000) can0 1F334455#0102030405060708\n");

    QVector<CANFrame> frames;
    QVERIFY(FrameFileIO::loadCanDumpFile(path("good.log"), &frames));
    QCOMPARE(frames.count(), 2);

    QCOMPARE(frames[0].frameId(), (quint32)0x123);
    QCOMPARE(frames[0].payload(), QByteArray::fromHex("deadbeef"));
    QCOMPARE(frames[0].hasExtendedFrameFormat(), false);

    QCOMPARE(frames[1].frameId(), (quint32)0x1F334455);
    QCOMPARE(frames[1].payload().length(), 8);
    //anything above the 11 bit range has to come back marked extended
    QCOMPARE(frames[1].hasExtendedFrameFormat(), true);
}

void TestFileIO::nativeCSVRoundTrip()
{
    //save then load through SavvyCAN's own format - the round trip has to preserve the frames
    QVector<CANFrame> out;
    for (int i = 0; i < 4; i++)
    {
        CANFrame f;
        f.setFrameId(0x100 + i);
        f.setExtendedFrameFormat(i % 2 == 1);
        f.setFrameType(QCanBusFrame::DataFrame);
        f.bus = 0;
        f.isReceived = true;
        f.setTimeStamp(QCanBusFrame::TimeStamp(0, 1000 * (i + 1)));
        f.setPayload(QByteArray::fromHex("0011223344556677").left(i + 1));
        out.append(f);
    }

    QVERIFY(FrameFileIO::saveNativeCSVFile(path("native.csv"), &out));

    QVector<CANFrame> in;
    QVERIFY(FrameFileIO::loadNativeCSVFile(path("native.csv"), &in));
    QCOMPARE(in.count(), out.count());
    for (int i = 0; i < in.count(); i++)
    {
        QCOMPARE(in[i].frameId(), out[i].frameId());
        QCOMPARE(in[i].payload(), out[i].payload());
        QCOMPARE(in[i].hasExtendedFrameFormat(), out[i].hasExtendedFrameFormat());
    }
}

void TestFileIO::crtdRoundTrip()
{
    //the loader treats the first line as a header and discards it, so a real file needs one
    writeFile("good.crtd", "1000.000 CXX SavvyCAN test\n"
                           "1000.000 R11 123 DE AD BE EF\n"
                           "1000.100 R11 456 01 02\n");
    QVector<CANFrame> frames;
    QVERIFY(FrameFileIO::loadCRTDFile(path("good.crtd"), &frames));
    QCOMPARE(frames.count(), 2);
    QCOMPARE(frames[0].frameId(), (quint32)0x123);
    QCOMPARE(frames[0].payload(), QByteArray::fromHex("deadbeef"));
    QCOMPARE(frames[1].payload(), QByteArray::fromHex("0102"));
}

/***************************************************************************************************
 * malformed text input
 **************************************************************************************************/

void TestFileIO::malformedTextFiles_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<LoaderFn>("loader");
    QTest::addColumn<QString>("name");

    //lines with fields missing, which used to index straight past the end of the token list
    const QByteArray shredded =
        "(1609459200.000000) can0\n"
        "(1609459200.000000)\n"
        "(bogus\n"
        "123#\n"
        "(1609459200.000000) can0 123#ZZZZ\n"
        "garbage line with no structure at all\n"
        "\n"
        "(1609459200.0000";

    const QByteArray csvish = "1000,123\n,\n123\n1000,123,00\nheader only,\n";
    const QByteArray crtdish = "1000.000 R11\nR11\n1000.000\nx\n";

    struct { const char *name; LoaderFn fn; } loaders[] = {
        {"canDump",    &FrameFileIO::loadCanDumpFile},
        {"crtd",       &FrameFileIO::loadCRTDFile},
        {"genericCSV", &FrameFileIO::loadGenericCSVFile},
        {"nativeCSV",  &FrameFileIO::loadNativeCSVFile},
        {"busmaster",  &FrameFileIO::loadLogFile},
        {"microchip",  &FrameFileIO::loadMicrochipFile},
        {"lawicel",    &FrameFileIO::loadLawicelFile},
        {"vectorTrace",&FrameFileIO::loadTraceFile},
        {"ixxat",      &FrameFileIO::loadIXXATFile},
        {"cando",      &FrameFileIO::loadCANDOFile},
        {"vehicleSpy", &FrameFileIO::loadVehicleSpyFile},
        {"cabana",     &FrameFileIO::loadCabanaFile},
        {"canopen",    &FrameFileIO::loadCANOpenFile},
        {"canalyzerASC",&FrameFileIO::loadCanalyzerASC},
        {"pcan",       &FrameFileIO::loadPCANFile},
        {"carbus",     &FrameFileIO::loadCARBUSAnalyzerFile},
        {"canhacker",  &FrameFileIO::loadCANHackerFile},
        {"clx000",     &FrameFileIO::loadCLX000File},
    };

    struct { const char *name; QByteArray data; } bodies[] = {
        {"shredded", shredded}, {"csvish", csvish}, {"crtdish", crtdish},
        {"empty", QByteArray()}, {"onlyNewlines", QByteArray("\n\n\n\n")},
        {"noTrailingNewline", QByteArray("(1609459200.000000) can0 123#DEAD")},
        {"binaryJunk", QByteArray::fromHex("00ff00ff00ff8080deadbeef")},
    };

    /* Every loader gets every body. Feeding a parser the wrong format on purpose is the point:
     * that is exactly what happens when a user picks the wrong entry in the file type list. */
    for (auto &l : loaders)
        for (auto &b : bodies)
            QTest::newRow(qPrintable(QString("%1-%2").arg(l.name).arg(b.name)))
                << b.data << l.fn << QString("%1_%2.log").arg(l.name).arg(b.name);
}

void TestFileIO::malformedTextFiles()
{
    QFETCH(QByteArray, content);
    QFETCH(LoaderFn, loader);
    QFETCH(QString, name);

    writeFile(name, content);

    /* The contract is only that the loader returns. Whether it rejects the file or manufactures
     * nonsense frames from nonsense input is not something we pin down here - not crashing,
     * hanging or reading out of bounds is. */
    QVector<CANFrame> frames;
    loader(path(name), &frames);
    QVERIFY(true);
}

/***************************************************************************************************
 * malformed binary input
 **************************************************************************************************/

void TestFileIO::malformedBinaryFiles_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("name");

    const quint32 BLF_SIG = 0x47474F4C;
    const quint32 OBJ_SIG = 0x4A424F4C;

    auto le32 = [](quint32 v) {
        QByteArray b;
        for (int i = 0; i < 4; i++) b.append((char)((v >> (8 * i)) & 0xFF));
        return b;
    };
    auto le16 = [](quint16 v) {
        QByteArray b;
        b.append((char)(v & 0xFF));
        b.append((char)((v >> 8) & 0xFF));
        return b;
    };
    auto blfHeader = [&]() { return le32(BLF_SIG) + QByteArray(140, 0); };
    auto objBase = [&](quint32 size, quint32 type) {
        return le32(OBJ_SIG) + le16(16) + le16(1) + le32(size) + le32(type);
    };
    auto containerHdr = [&](quint32 uncomp) {
        return le16(0) + QByteArray(6, 0) + le32(uncomp) + QByteArray(4, 0);
    };

    //an object claiming to be smaller than its own header - used to underflow into a wild read
    QTest::newRow("blf-undersized-object")
        << (blfHeader() + objBase(4, 10)) << QString("undersized.blf");

    /* An object inside a container claiming zero size. This is the exact shape that used to spin
     * the parser forever and hang the program. The inner stream has to be longer than the object
     * header for the processing loop to be entered at all. */
    {
        QByteArray inner = objBase(0, 1) + QByteArray(16, 0) + QByteArray(32, 0);
        QByteArray payload = containerHdr(inner.length()) + inner;
        QTest::newRow("blf-inner-zero-size")
            << (blfHeader() + objBase(16 + payload.length(), 10) + payload) << QString("innerzero.blf");
    }

    QTest::newRow("blf-truncated-header") << blfHeader().left(40) << QString("trunchdr.blf");
    QTest::newRow("blf-truncated-container")
        << (blfHeader() + objBase(4096, 10) + QByteArray(8, 0x11)) << QString("trunccont.blf");
    QTest::newRow("blf-garbage") << QByteArray(512, (char)0xA5) << QString("garbage.blf");

    //pcap: a classic capture whose packet is far too short for the fixed offsets the loader reads
    {
        QByteArray hdr = le32(0xA1B2C3D4) + le16(2) + le16(4) + le32(0) + le32(0) + le32(65535) + le32(227);
        QByteArray shortPkt = le32(1) + le32(0) + le32(2) + le32(2) + QByteArray::fromHex("0102");
        QTest::newRow("pcap-short-packet") << (hdr + shortPkt) << QString("shortpkt.pcap");
        QTest::newRow("pcap-header-only") << hdr << QString("hdronly.pcap");
        QByteArray liesPkt = le32(1) + le32(0) + le32(200) + le32(200) + QByteArray(10, (char)0xAA);
        QTest::newRow("pcap-truncated-payload") << (hdr + liesPkt) << QString("truncpay.pcap");
    }

    /* pcapng: a block declaring size zero. Seeking to "the next block" lands back on this one,
     * which used to loop forever. */
    {
        QByteArray ng = le32(0x0A0D0D0A) + le32(28) + QByteArray(20, 0);
        QByteArray zeroBlock = le32(0x01) + le32(0) + QByteArray(20, 0);
        QTest::newRow("pcapng-zero-block") << (ng + zeroBlock) << QString("zeroblock.pcapng");
        QTest::newRow("pcapng-truncated") << (le32(0x0A0D0D0A) + le32(28) + QByteArray(10, 0))
                                          << QString("truncng.pcapng");
    }

    //tesla AP records are fixed size; a file that isn't a whole multiple used to read stale stack
    QTest::newRow("teslaap-partial-record") << QByteArray(28 * 2 + 13, 0x22) << QString("partial.can");
    QTest::newRow("teslaap-one-byte") << QByteArray(1, 0x05) << QString("onebyte.can");

    //canserver: valid signature then a frame marker with the header cut short
    {
        QByteArray v2("CANSERVER_v2_CANSERVER");
        QTest::newRow("canserver-header-only") << v2 << QString("hdronly.cslog");
        QTest::newRow("canserver-truncated-frame")
            << (v2 + QByteArray::fromHex("CF") + QByteArray::fromHex("0102")) << QString("truncframe.cslog");
        QTest::newRow("canserver-truncated-mark")
            << (v2 + QByteArray::fromHex("CD")) << QString("truncmark.cslog");
        QTest::newRow("canserver-mark-lies")
            << (v2 + QByteArray::fromHex("CDFF") + QByteArray(4, 0x41)) << QString("marklies.cslog");
    }
}

void TestFileIO::malformedBinaryFiles()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, name);

    writeFile(name, content);
    const QString p = path(name);

    /* Run every binary loader and detector against every corrupt file. A hang shows up as the
     * test timing out, a crash takes the runner down - either way it is unmistakable. */
    QVector<CANFrame> frames;

    frames.clear(); FrameFileIO::loadCanalyzerBLF(p, &frames);
    frames.clear(); FrameFileIO::loadWiresharkFile(p, &frames);
    frames.clear(); FrameFileIO::loadWiresharkSocketCANFile(p, &frames);
    frames.clear(); FrameFileIO::loadTeslaAPFile(p, &frames);
    frames.clear(); FrameFileIO::loadCANServerFile(p, &frames);

    FrameFileIO::isCanalyzerBLF(p);
    FrameFileIO::isWiresharkFile(p);
    FrameFileIO::isWiresharkSocketCANFile(p);
    FrameFileIO::isTeslaAPFile(p);
    FrameFileIO::isCANServerFile(p);

    QVERIFY(true);
}

void TestFileIO::blfContainerStillParses()
{
    /* A hand built but structurally valid BLF holding one CAN message inside an uncompressed
     * container. This is the regression guard for the hardening above: the size checks must
     * reject only malformed objects, never a real log. */
    auto le32 = [](quint32 v) {
        QByteArray b;
        for (int i = 0; i < 4; i++) b.append((char)((v >> (8 * i)) & 0xFF));
        return b;
    };
    auto le16 = [](quint16 v) {
        QByteArray b;
        b.append((char)(v & 0xFF));
        b.append((char)((v >> 8) & 0xFF));
        return b;
    };

    QByteArray blfHeader = le32(0x47474F4C) + QByteArray(140, 0);
    auto objBase = [&](quint32 size, quint32 type) {
        return le32(0x4A424F4C) + le16(16) + le16(1) + le32(size) + le32(type);
    };

    //BLF_CAN_OBJ: channel(2) flags(1) dlc(1) id(4) data(8)
    QByteArray canObj = le16(1) + QByteArray(1, 0) + QByteArray(1, 8) + le32(0x123)
                        + QByteArray::fromHex("0001020304050607");
    QByteArray inner = objBase(32 + canObj.length(), 1) + QByteArray(16, 0) + canObj;
    QByteArray payload = le16(0) + QByteArray(6, 0) + le32(inner.length()) + QByteArray(4, 0) + inner;
    QByteArray file = blfHeader + objBase(16 + payload.length(), 10) + payload;

    writeFile("valid.blf", file);

    QVector<CANFrame> frames;
    QVERIFY(FrameFileIO::loadCanalyzerBLF(path("valid.blf"), &frames));
    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames[0].frameId(), (quint32)0x123);
    QCOMPARE(frames[0].payload(), QByteArray::fromHex("0001020304050607"));
}

/***************************************************************************************************
 * format detection probes
 **************************************************************************************************/

//the probes autodetect relies on to decide what a file is
typedef bool (*ProbeFn)(QString);

struct ProbeEntry { const char *name; ProbeFn fn; };

static QVector<ProbeEntry> allProbes()
{
    return {
        {"nativeCSV",    &FrameFileIO::isNativeCSVFile},
        {"genericCSV",   &FrameFileIO::isGenericCSVFile},
        {"busmaster",    &FrameFileIO::isLogFile},
        {"microchip",    &FrameFileIO::isMicrochipFile},
        {"vectorTrace",  &FrameFileIO::isTraceFile},
        {"canDump",      &FrameFileIO::isCanDumpFile},
        {"kvaser",       &FrameFileIO::isKvaserFile},
        {"cabana",       &FrameFileIO::isCabanaFile},
        {"cando",        &FrameFileIO::isCANDOFile},
        {"ixxat",        &FrameFileIO::isIXXATFile},
        {"canalyzerASC", &FrameFileIO::isCanalyzerASC},
        {"crtd",         &FrameFileIO::isCRTDFile},
        {"vehicleSpy",   &FrameFileIO::isVehicleSpyFile},
        {"carbus",       &FrameFileIO::isCARBUSAnalyzerFile},
        {"canhacker",    &FrameFileIO::isCANHackerFile},
        {"canopen",      &FrameFileIO::isCANOpenFile},
        {"pcan",         &FrameFileIO::isPCANFile},
        {"lawicel",      &FrameFileIO::isLawicelFile},
        {"teslaAP",      &FrameFileIO::isTeslaAPFile},
        {"clx000",       &FrameFileIO::isCLX000File},
        {"canserver",    &FrameFileIO::isCANServerFile},
        {"blf",          &FrameFileIO::isCanalyzerBLF},
        {"wireshark",    &FrameFileIO::isWiresharkFile},
        {"wiresharkSCAN",&FrameFileIO::isWiresharkSocketCANFile},
    };
}

void TestFileIO::probesRejectEmptyFile()
{
    /* An empty file is not any format. These probes used to start from "yes" and only say no on
     * contrary evidence, so a zero byte file matched nearly all of them and autodetect picked
     * whichever was tried first. */
    writeFile("empty.dat", QByteArray());

    foreach (const ProbeEntry &probe, allProbes())
    {
        QVERIFY2(!probe.fn(path("empty.dat")),
                 qPrintable(QString("%1 claimed an empty file as its own format").arg(probe.name)));
    }
}

void TestFileIO::probesRejectJunk()
{
    //random bytes are not a CAN log in any format either
    QByteArray junk;
    for (int i = 0; i < 512; i++) junk.append((char)((i * 37 + 11) & 0xFF));
    writeFile("junk.dat", junk);

    foreach (const ProbeEntry &probe, allProbes())
    {
        QVERIFY2(!probe.fn(path("junk.dat")),
                 qPrintable(QString("%1 claimed random bytes as its own format").arg(probe.name)));
    }
}

void TestFileIO::probesAcceptTheirOwnFormat()
{
    /* The other half of the contract: tightening the probes must not stop them recognising a real
     * file. Only the formats we can construct a genuine sample of by hand are checked here. */
    writeFile("real.log",
              "(1609459200.000000) can0 123#DEADBEEF\n"
              "(1609459200.001000) can0 456#0102030405060708\n");
    QVERIFY2(FrameFileIO::isCanDumpFile(path("real.log")), "candump probe rejected a real candump file");

    //a native CSV written by SavvyCAN itself must be recognised by SavvyCAN
    QVector<CANFrame> out;
    CANFrame f;
    f.setFrameId(0x123);
    f.setFrameType(QCanBusFrame::DataFrame);
    f.setExtendedFrameFormat(false);
    f.bus = 0;
    f.isReceived = true;
    f.setTimeStamp(QCanBusFrame::TimeStamp(0, 1000));
    f.setPayload(QByteArray::fromHex("deadbeef"));
    out.append(f);
    QVERIFY(FrameFileIO::saveNativeCSVFile(path("real.csv"), &out));
    QVERIFY2(FrameFileIO::isNativeCSVFile(path("real.csv")), "native CSV probe rejected our own output");
}
