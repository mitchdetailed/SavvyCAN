#ifndef TST_CODECS_H
#define TST_CODECS_H

#include <QObject>

/*
 * Unit tests for the pure, self contained pieces of the protocol drivers - the parts that turn
 * bytes into frames and back. These need no hardware and no ports, which is exactly why they are
 * worth having: a mistake in one of these tables or encoders otherwise only shows up as an adapter
 * that mysteriously talks to nothing.
 */
class TestCodecs: public QObject
{
    Q_OBJECT

private slots:
    //msgpack, as used to interoperate with python-can's udp_multicast backend
    void msgpackRoundTrip();
    void msgpackTruncatedInputIsRejected();
    void msgpackTruncatedInputIsRejected_data();
    void msgpackSkipsUnknownValues();
    void msgpackPythonCanMessageShape();
    void msgpackRejectsRunawayNesting();

    //bit timing / bitrate tables
    void seeedBitrateCodes();
    void seeedBitrateCodes_data();
    void canalystBitTiming();
    void canalystBitTiming_data();
    void ixxatBitTiming();
    void ixxatBitTiming_data();
    void kvaserBitrateConstants();
    void kvaserBitrateConstants_data();
    void bitTimingTablesAgree();
    void kvaserFdDataRates();
    void kvaserFdDataRates_data();

    //error frame decoding
    void errorFrameDecode();
    void errorFrameDecode_data();
    void errorFrameDecodeHandlesShortPayload();
};

#endif // TST_CODECS_H
