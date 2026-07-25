#ifndef CANCONCONST_H
#define CANCONCONST_H

#include <QString>

namespace CANCon {

    /**
     * @brief The status enum
     */
    enum status
    {
        NOT_CONNECTED,  /*!< device is not connected */
        CONNECTED       /*!< device is connected */
    };

    /* NB: these are written into QSettings as plain integers by ConnectionWindow::saveConnections
     * so the existing values must keep their numbering. Add new types at the end, before NONE. */
    enum type
    {
        GVRET_SERIAL,
        KVASER,
        SERIALBUS,
        REMOTE,
        KAYAK,
        MQTT,
        LAWICEL,
        CANSERVER,
        CANLOGSERVER,
        GSUSB,
        SEEEDSTUDIO,
        ROBOTELL,
        PYCAN_SERIAL,
        UDP_MULTICAST,
        CANALYSTII,
        IXXAT,
        USB2CAN,
        ISCAN,
        NICAN,
        NEOUSYS,
        NONE
    };
}

class CANConStatus
{
public:
    CANCon::status conStatus;
    int numHardwareBuses;
};

/**
 * @brief Running health figures for a single bus.
 *
 * Everything here is worked out from the traffic itself rather than asked of the hardware, so it
 * is available for every connection type including the network and file based ones. A driver that
 * can also report the controller's own error counters fills in the error fields on top.
 */
class CANBusStats
{
public:
    CANBusStats() { reset(); }

    void reset()
    {
        framesReceived = 0;
        framesSent = 0;
        frameRate = 0;
        framesThisPeriod = 0;
        bitsThisPeriod = 0;
        busLoadPercent = 0.0;
        rxErrorCount = -1;
        txErrorCount = -1;
        errorFrames = 0;
        busState = QStringLiteral("Unknown");
    }

    uint64_t framesReceived;
    uint64_t framesSent;
    int frameRate;              //frames per second over the last sampling period
    uint64_t framesThisPeriod;  //accumulator behind frameRate, reset each period
    uint64_t bitsThisPeriod;    //accumulator used to work out bus load, reset each period
    double busLoadPercent;      //of the configured bit rate, so it needs a speed to mean anything
    int rxErrorCount;           //-1 when the driver can't tell us
    int txErrorCount;           //-1 when the driver can't tell us
    uint64_t errorFrames;
    QString busState;           //"Active", "Bus Off", ... whatever the driver reports
};

/**
 * @brief One entry from a driver's device scan.
 *
 * Drivers that can ask their hardware or driver library what is attached return these from a static
 * enumerateDevices(). "key" is what the connection's port name has to be set to in order to open
 * that device, "description" is the human readable version for the connection dialog.
 */
class CANDeviceInfo
{
public:
    CANDeviceInfo() {}
    CANDeviceInfo(const QString &pKey, const QString &pDescription) : key(pKey), description(pDescription) {}

    QString key;
    QString description;
};

#endif // CANCONCONST_H
