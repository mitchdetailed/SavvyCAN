#ifndef TST_CANTRIPLE_H
#define TST_CANTRIPLE_H

#include <QObject>

/*
 * Drives the CAN Triple connection through a whole session against an in-memory emulation of the
 * SavvyCAN firmware: handshake, per bus configuration, received frames (including a clock wrap and
 * CAN FD), transmit encoding, status reporting and an orderly stop. No hardware or serial port.
 */
class TestCANTriple : public QObject
{
    Q_OBJECT

private slots:
    void protocolHelpers();
    void session();
};

#endif // TST_CANTRIPLE_H
