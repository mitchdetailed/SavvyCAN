#include <QtTest>
#include <QMutex>
#include <QtEndian>

#include "tst_cantriple.h"
#include "cantriple.h"
#include "cantriple_protocol.h"
#include "canframemodel.h"

/*
 * What the user saw: in overwrite mode the Time Delta column jumped to an enormous value for an
 * instant, over and over. Two frames of one ID, 100 ms apart, straddling a second boundary, and
 * stamped the way Qt's TimeStamp::fromMicroSeconds() stamps them (seconds plus a 0-999999 rest).
 * Unrepaired, the frame list keeps only the rest, so 0.05 s minus 0.95 s goes negative and is
 * shown as about 1.8e13 seconds. Repaired on the way in, the delta is the real 100000 us.
 */
void TestCANTriple::deltaAcrossSecondBoundary()
{
    const qint64 base = 1762862400LL * 1000000;   //a whole second, epoch microseconds
    auto make = [](qint64 us) {
        CANFrame f;
        f.bus = 0;
        f.setFrameId(0x123);
        f.setPayload(QByteArray(8, 0));
        f.isReceived = true;
        f.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(us));
        return f;
    };
    CANFrame a = make(base + 950000);
    CANFrame b = make(base + 1050000);
    QVERIFY(a.timeStamp().seconds() != 0);   //split, as such a driver hands it over

    CANConnection::normalizeTimeStamp(a);
    CANConnection::normalizeTimeStamp(b);
    QCOMPARE(a.timeStamp().seconds(), (qint64)0);
    QCOMPARE(a.timeStamp().microSeconds(), base + 950000);

    //normalising twice changes nothing
    CANFrame again = b;
    CANConnection::normalizeTimeStamp(again);
    QCOMPARE(again.timeStamp().microSeconds(), b.timeStamp().microSeconds());

    CANFrameModel model;
    model.setOverwriteMode(true);
    model.addFrame(a, false);
    model.addFrame(b, false);
    const QVector<CANFrame> *rows = model.getFilteredListReference();
    QCOMPARE(rows->size(), 1);
    QCOMPARE((qint64)rows->at(0).timedelta, (qint64)100000);
}

namespace {

/*
 * Stands in for the firmware. Everything the driver writes is parsed as the device would parse it,
 * and the replies come back through feedBytes on a queued call - the way bytes arrive from a real
 * port - so nothing re-enters the driver while it is still in the middle of sending.
 */
class FakeTriple : public CANTriple
{
public:
    FakeTriple() : CANTriple("fake", 500000, false, 0) {}

    QMutex lock;
    QList<ctsc_bus_config_t> configs;
    QList<QByteArray> txPayloads;
    int starts = 0;
    int stops = 0;
    int pings = 0;

    //starts just short of the 32 bit wrap so the frames below cross it
    static const uint32_t kStartTime = 0xFFFFF000u;

protected:
    bool transportOpen() override { return true; }
    void transportClose() override {}

    void transportWrite(const QByteArray &bytes) override
    {
        for (char c : bytes)
        {
            if (c != 0) { mEnc.append(c); continue; }
            uint8_t dec[CTSC_MAX_PACKET + 8];
            const size_t n = ctsc_cobs_decode((const uint8_t*)mEnc.constData(), mEnc.size(), dec);
            mEnc.clear();
            if (n < 4 || dec[0] != CTSC_SYNC) continue;
            const uint16_t crc = ((uint16_t)dec[n - 2] << 8) | dec[n - 1];
            if (ctsc_crc16(dec, n - 2) != crc) continue;
            handle(dec[1], QByteArray((const char*)dec + 2, (int)n - 4));
        }
    }

private:
    QByteArray mEnc;

    void reply(uint8_t type, const QByteArray &payload)
    {
        QByteArray raw;
        raw.append((char)CTSC_SYNC);
        raw.append((char)type);
        raw.append(payload);
        const uint16_t crc = ctsc_crc16((const uint8_t*)raw.constData(), raw.size());
        raw.append((char)(crc >> 8));
        raw.append((char)(crc & 0xFF));
        QByteArray enc(raw.size() + raw.size() / 254 + 3, Qt::Uninitialized);
        const size_t n = ctsc_cobs_encode((const uint8_t*)raw.constData(), raw.size(), (uint8_t*)enc.data());
        enc.truncate((int)n);
        enc.append((char)0);
        QMetaObject::invokeMethod(this, [this, enc]() { feedBytes(enc); }, Qt::QueuedConnection);
    }

    void ack(uint8_t type, uint8_t status, uint8_t bus, uint32_t time)
    {
        ctsc_ack_t a;
        memset(&a, 0, sizeof(a));
        a.acked_type = type;
        a.status = status;
        a.bus = bus;
        a.time_us = qToLittleEndian(time);
        reply(CTSC_ACK, QByteArray((const char*)&a, sizeof(a)));
    }

    static void rxRecord(QByteArray &out, uint8_t hdr, uint8_t dlc, uint32_t ts, uint32_t id, const QByteArray &data)
    {
        out.append((char)hdr);
        out.append((char)dlc);
        const uint32_t t = qToLittleEndian(ts);
        out.append((const char*)&t, 4);
        if (hdr & CTSC_HDR_EXT) { const uint32_t i = qToLittleEndian(id); out.append((const char*)&i, 4); }
        else { const uint16_t i = qToLittleEndian((uint16_t)id); out.append((const char*)&i, 2); }
        out.append(data);
    }

    void handle(uint8_t type, const QByteArray &payload)
    {
        QMutexLocker locker(&lock);

        switch (type)
        {
        case CTSC_HELLO:
        {
            ctsc_info_t info;
            memset(&info, 0, sizeof(info));
            info.proto_version = CTSC_PROTOCOL_VERSION;
            info.model = CTSC_MODEL_V2;
            info.hw_revision = 1;
            info.num_buses = 3;
            info.caps = qToLittleEndian<quint16>(CTSC_CAP_FD | CTSC_CAP_TERMINATION | CTSC_CAP_LISTEN_ONLY);
            info.fw_major = 1;
            info.can_clock_hz = qToLittleEndian<quint32>(80000000);
            memcpy(info.serial, "TESTSERIAL", 10);
            reply(CTSC_INFO, QByteArray((const char*)&info, sizeof(info)));
            break;
        }
        case CTSC_BUS_CONFIG:
        {
            ctsc_bus_config_t c;
            memcpy(&c, payload.constData(), sizeof(c));
            configs.append(c);
            const bool reachable = qFromLittleEndian(c.nominal_bps) != 123456;
            ack(CTSC_BUS_CONFIG, reachable ? CTSC_OK : CTSC_ERR_BAD_BITRATE, c.bus, kStartTime);
            break;
        }
        case CTSC_START:
        {
            starts++;
            ack(CTSC_START, CTSC_OK, 0xFF, kStartTime);

            QByteArray frames;
            rxRecord(frames, 0, 8, kStartTime + 100, 0x123, QByteArray::fromHex("0102030405060708"));
            //this one crosses the wrap: kStartTime + 0x2000 is 0x00001000
            rxRecord(frames, 1 | CTSC_HDR_EXT, 3, kStartTime + 0x2000, 0x18FF50E5, QByteArray::fromHex("AABBCC"));
            QByteArray fd;
            for (int i = 0; i < 64; i++) fd.append((char)i);
            rxRecord(frames, 2 | CTSC_HDR_FDF | CTSC_HDR_BRS, 15, kStartTime + 0x3000, 0x7FF, fd);
            rxRecord(frames, 0 | CTSC_HDR_RTR, 2, kStartTime + 0x3100, 0x055, QByteArray());
            reply(CTSC_FRAMES, frames);
            break;
        }
        case CTSC_PING:
        {
            pings++;
            QByteArray st;
            ctsc_status_t s;
            memset(&s, 0, sizeof(s));
            s.time_us = qToLittleEndian<quint32>(kStartTime + 0x10000);
            s.num_buses = 3;
            st.append((const char*)&s, sizeof(s));
            ctsc_bus_status_t b[3];
            memset(b, 0, sizeof(b));
            b[0].state = CTSC_STATE_ACTIVE; b[0].flags = CTSC_BST_TERMINATION;
            b[0].rx_dropped = qToLittleEndian<quint16>(2); b[0].bus_errors = qToLittleEndian<quint16>(1);
            b[1].state = CTSC_STATE_PASSIVE; b[1].tec = 130;
            b[2].state = CTSC_STATE_OFF;
            st.append((const char*)b, sizeof(b));
            reply(CTSC_STATUS, st);
            break;
        }
        case CTSC_TX:
            txPayloads.append(payload);
            break;
        case CTSC_STOP:
            stops++;
            break;
        }
    }
};

//the last configuration the device was sent for a bus
ctsc_bus_config_t lastConfig(FakeTriple &dev, int bus)
{
    QMutexLocker locker(&dev.lock);
    for (int i = dev.configs.size() - 1; i >= 0; i--)
        if (dev.configs[i].bus == bus) return dev.configs[i];
    ctsc_bus_config_t none;
    memset(&none, 0xFF, sizeof(none));
    return none;
}

QList<CANFrame> drain(CANConnection &conn)
{
    QList<CANFrame> frames;
    CANFrame *f;
    while ((f = conn.getQueue().peek()))
    {
        frames.append(*f);
        conn.getQueue().dequeue();
    }
    return frames;
}

/* Read a timestamp the way the rest of SavvyCAN does: the frame model, the graphs and the file
 * writers all take microSeconds() as the whole time and never look at seconds(). Adding seconds()
 * back in here once hid a driver that split its stamps, which showed up in the app as the time
 * delta jumping to nonsense every time a frame crossed a second boundary. */
qint64 micros(const CANFrame &f)
{
    return f.timeStamp().microSeconds();
}

}

void TestCANTriple::protocolHelpers()
{
    //CRC-16/CCITT-FALSE check value, shared with the Device Manager protocol
    QCOMPARE(ctsc_crc16((const uint8_t*)"123456789", 9), (uint16_t)0x29B1);

    //the structs are the wire format, so their sizes are part of the contract
    QCOMPARE(sizeof(ctsc_bus_config_t), (size_t)12);
    QCOMPARE(sizeof(ctsc_info_t), (size_t)46);
    QCOMPARE(sizeof(ctsc_ack_t), (size_t)8);
    QCOMPARE(sizeof(ctsc_status_t), (size_t)8);
    QCOMPARE(sizeof(ctsc_bus_status_t), (size_t)12);

    for (uint8_t d = 0; d < 16; d++) QCOMPARE(ctsc_len_to_dlc(ctsc_dlc_to_len(d)), d);

    const uint8_t in[] = { 0x11, 0x22, 0x00, 0x33 };
    uint8_t enc[8], dec[8];
    const size_t n = ctsc_cobs_encode(in, sizeof(in), enc);
    const uint8_t want[] = { 0x03, 0x11, 0x22, 0x02, 0x33 };
    QCOMPARE(n, sizeof(want));
    QVERIFY(memcmp(enc, want, n) == 0);
    QCOMPARE(ctsc_cobs_decode(enc, n, dec), sizeof(in));
    QVERIFY(memcmp(dec, in, sizeof(in)) == 0);
}

void TestCANTriple::session()
{
    FakeTriple dev;

    CANBus b0; b0.setSpeed(500000); b0.setActive(true); b0.setTermination(true);
    CANBus b1; b1.setSpeed(250000); b1.setActive(true); b1.setListenOnly(true);
    CANBus b2; b2.setSpeed(500000); b2.setActive(true); b2.setCanFD(true); b2.setDataRate(2000000);
    dev.setBusSettings(0, b0);
    dev.setBusSettings(1, b1);
    dev.setBusSettings(2, b2);

    dev.start();
    QTRY_COMPARE_WITH_TIMEOUT(dev.getStatus(), CANCon::CONNECTED, 3000);

    //every bus was configured as asked
    ctsc_bus_config_t c = lastConfig(dev, 0);
    QCOMPARE(c.mode, (uint8_t)CTSC_MODE_NORMAL);
    QCOMPARE(c.flags, (uint8_t)CTSC_CFG_TERMINATION);
    QCOMPARE(qFromLittleEndian(c.nominal_bps), 500000u);
    c = lastConfig(dev, 1);
    QCOMPARE(c.mode, (uint8_t)CTSC_MODE_LISTEN_ONLY);
    QCOMPARE(c.flags, (uint8_t)0);
    QCOMPARE(qFromLittleEndian(c.nominal_bps), 250000u);
    c = lastConfig(dev, 2);
    QCOMPARE(c.mode, (uint8_t)CTSC_MODE_NORMAL);
    QCOMPARE(c.flags, (uint8_t)(CTSC_CFG_FD | CTSC_CFG_BRS));
    QCOMPARE(qFromLittleEndian(c.data_bps), 2000000u);

    //the frames the emulator sent right after START
    QList<CANFrame> rx;
    QTRY_VERIFY_WITH_TIMEOUT((rx += drain(dev)).size() >= 4, 2000);
    QCOMPARE(rx.size(), 4);

    QCOMPARE(rx[0].bus, 0);
    QCOMPARE(rx[0].frameId(), 0x123u);
    QVERIFY(!rx[0].hasExtendedFrameFormat());
    QCOMPARE(rx[0].payload(), QByteArray::fromHex("0102030405060708"));
    QVERIFY(rx[0].isReceived);

    QCOMPARE(rx[1].bus, 1);
    QCOMPARE(rx[1].frameId(), 0x18FF50E5u);
    QVERIFY(rx[1].hasExtendedFrameFormat());
    QCOMPARE(rx[1].payload(), QByteArray::fromHex("AABBCC"));

    QCOMPARE(rx[2].bus, 2);
    QVERIFY(rx[2].hasFlexibleDataRateFormat());
    QVERIFY(rx[2].hasBitrateSwitch());
    QCOMPARE(rx[2].payload().size(), 64);
    QCOMPARE((int)(uint8_t)rx[2].payload()[63], 63);

    QCOMPARE(rx[3].frameType(), QCanBusFrame::RemoteRequestFrame);
    QCOMPARE(rx[3].frameId(), 0x55u);

    //whole time in the microseconds field, anchored to the wall clock, nothing in seconds()
    const qint64 nowUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    for (const CANFrame &f : rx)
    {
        QCOMPARE(f.timeStamp().seconds(), (qint64)0);
        QVERIFY(qAbs(micros(f) - nowUs) < 60LL * 1000000);
    }

    //the device clock wrapped between frames 0 and 1, which must not show as a jump backwards
    QCOMPARE(micros(rx[1]) - micros(rx[0]), (qint64)(0x2000 - 100));
    QCOMPARE(micros(rx[2]) - micros(rx[1]), (qint64)0x1000);

    //transmit: classic on bus 0 goes out as a tx record
    CANFrame tx;
    tx.bus = 0;
    tx.setFrameId(0x321);
    tx.setPayload(QByteArray::fromHex("010203"));
    QVERIFY(dev.sendFrame(tx));

    //bus 1 is listen only, so nothing may be sent on it
    tx.bus = 1;
    QVERIFY(!dev.sendFrame(tx));

    //CAN FD on bus 2, 20 bytes is a DLC step of its own
    CANFrame fdtx;
    fdtx.bus = 2;
    fdtx.setFrameId(0x100);
    fdtx.setFlexibleDataRateFormat(true);
    fdtx.setBitrateSwitch(true);
    fdtx.setPayload(QByteArray(20, (char)0x5A));
    QVERIFY(dev.sendFrame(fdtx));

    QTRY_COMPARE_WITH_TIMEOUT(([&]{ QMutexLocker l(&dev.lock); return dev.txPayloads.size(); })(), 2, 1000);
    {
        QMutexLocker l(&dev.lock);
        QCOMPARE(dev.txPayloads[0], QByteArray::fromHex("0003" "2103" "010203"));
        const QByteArray &fdrec = dev.txPayloads[1];
        QCOMPARE((uint8_t)fdrec[0], (uint8_t)(2 | CTSC_HDR_FDF | CTSC_HDR_BRS));
        QCOMPARE((uint8_t)fdrec[1], (uint8_t)11);
        QCOMPARE(fdrec.size(), 2 + 2 + 20);
    }
    drain(dev); //sendFrame logs its own copy of what was sent

    //a PING goes out every half second and the STATUS it draws shows up as controller state
    CANControllerState st;
    QTRY_VERIFY_WITH_TIMEOUT(dev.getControllerState(0, st), 2000);
    QCOMPARE(st.state, QString("Error Active"));
    QCOMPARE(st.rxOverruns, (uint64_t)2);
    QCOMPARE(st.busErrors, (uint64_t)1);
    QVERIFY(st.note.contains("Terminated"));
    QVERIFY(dev.getControllerState(1, st));
    QCOMPARE(st.state, QString("Error Passive"));
    QCOMPARE(st.txErrorCount, 130);

    //a rate the device can't reach is refused, and that reaches the health panel
    CANBus bad = b0;
    bad.setSpeed(123456);
    dev.setBusSettings(0, bad);
    QTRY_VERIFY_WITH_TIMEOUT(dev.getControllerState(0, st) && st.note.contains("bit rate"), 2000);

    dev.stop();
    QMutexLocker l(&dev.lock);
    QCOMPARE(dev.starts, 1);
    QCOMPARE(dev.stops, 1);
    QVERIFY(dev.pings >= 1);
}
