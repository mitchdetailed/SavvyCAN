#include <QString>
#include "canconfactory.h"
#include "serialbusconnection.h"
#include "gvretserial.h"
#include "mqtt_bus.h"
#include "socketcand.h"
#include "lawicel_serial.h"
#include "canserver.h"
#include "canlogserver.h"
#include "gsusbconnection.h"
#include "seeedcan.h"
#include "robotellcan.h"
#include "pythoncanserial.h"
#include "udpmulticast.h"
#include "canalystii.h"
#include "kvasercanlib.h"
#include "ixxatvci.h"
#include "usb2canlib.h"
#include "iscanlib.h"
#include "nicanlib.h"
#include "neousyscan.h"

using namespace CANCon;

CANConnection* CanConFactory::create(type pType, QString pPortName, QString pDriverName, int pSerialSpeed, int pBusSpeed, bool pCanFd, int pDataRate)
{
    switch(pType) {
    case SERIALBUS:
      return new SerialBusConnection(pPortName, pDriverName, pBusSpeed, pDataRate, pCanFd);
    case GVRET_SERIAL:
        if(pPortName.contains(".") && !pPortName.contains("tty") && !pPortName.contains("serial"))
        return new GVRetSerial(pPortName, true);
        else
        return new GVRetSerial(pPortName, false);
    case REMOTE:
        return new GVRetSerial(pPortName, true);  //it's a special case of GVRET connected over TCP/IP so it uses the same class
    case LAWICEL:
        return new LAWICELSerial(pPortName, pSerialSpeed, pBusSpeed, pCanFd, pDataRate);
    case KAYAK:
        return new SocketCANd(pPortName);
    case MQTT:
        return new MQTT_BUS(pPortName);
    case CANSERVER:
        return new CANserver(pPortName);
    case CANLOGSERVER:
        return new CanLogServer(pPortName);
    case GSUSB:
        return new GSUSBConnection(pPortName, pBusSpeed);
    case SEEEDSTUDIO:
        return new SeeedCAN(pPortName, pSerialSpeed, pBusSpeed);
    case ROBOTELL:
        return new RobotellCAN(pPortName, pSerialSpeed, pBusSpeed);
    case PYCAN_SERIAL:
        return new PythonCanSerial(pPortName, pSerialSpeed);
    case UDP_MULTICAST:
        return new UDPMulticast(pPortName);
    case CANALYSTII:
        return new CanalystII(pPortName, pBusSpeed);
    case KVASER:
        return new KvaserCanlib(pPortName, pBusSpeed, pCanFd, pDataRate);
    case IXXAT:
        return new IxxatVci(pPortName, pBusSpeed);
    case USB2CAN:
        return new Usb2CanLib(pPortName, pBusSpeed);
    case ISCAN:
        return new IscanLib(pPortName, pBusSpeed);
    case NICAN:
        return new NicanLib(pPortName, pBusSpeed);
    case NEOUSYS:
        return new NeousysCan(pPortName, pBusSpeed);
    default: {}
    }

    return nullptr;
}
