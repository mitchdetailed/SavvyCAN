#ifndef KVASERCANLIB_H
#define KVASERCANLIB_H

#include <QLibrary>
#include <QTimer>

#include "canconnection.h"

/*
 * Kvaser interfaces through Kvaser's own CANlib.
 *
 * CANlib ships with Kvaser's driver package and cannot be redistributed, so nothing here is linked
 * at build time. The library is opened with QLibrary at connect time and the handful of entry points
 * we need are resolved by name. If the driver isn't installed the connection simply reports that it
 * couldn't come up.
 *
 * The port name is the CANlib channel number, so "0" is the first channel.
 */

//CANlib is __stdcall on Windows. That's a no-op for 64 bit builds but matters for 32 bit ones.
#ifdef Q_OS_WIN
#define KVASER_CALL __stdcall
#else
#define KVASER_CALL
#endif

class KvaserCanlib : public CANConnection
{
    Q_OBJECT

public:
    KvaserCanlib(QString portName, int busSpeed, bool canFd, int dataRate);
    virtual ~KvaserCanlib();

    //pure lookups, public so the unit tests can check the tables
    static long bitrateConstant(int speed);
    //CAN FD data phase presets, separate table to the arbitration phase one above
    static long fdDataRateConstant(int dataRate);

    //ask CANlib what channels exist, for the connection dialog's device scan
    static QList<CANDeviceInfo> enumerateDevices();

protected:
    virtual void piStarted() override;
    virtual void piStop() override;
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus) override;
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus) override;
    virtual void piSuspend(bool pSuspend) override;
    virtual bool piSendFrame(const CANFrame& pFrame) override;

private slots:
    void handleTick();

private:
    bool loadLibrary();
    void unloadLibrary();
    bool openChannel();
    void closeChannel();
    bool applyBusSettings();
    void sendStatus();

    /* CANlib entry points. Types spelled out here so we don't need Kvaser's headers.
     * canStatus and CanHandle are both plain ints in CANlib. */
    typedef void (KVASER_CALL *fnInitializeLibrary)(void);
    typedef int  (KVASER_CALL *fnUnloadLibrary)(void);
    typedef int  (KVASER_CALL *fnGetNumberOfChannels)(int *channelCount);
    typedef int  (KVASER_CALL *fnOpenChannel)(int channel, int flags);
    typedef int  (KVASER_CALL *fnClose)(int handle);
    typedef int  (KVASER_CALL *fnSetBusParams)(int handle, long freq, unsigned int tseg1, unsigned int tseg2,
                                               unsigned int sjw, unsigned int noSamp, unsigned int syncmode);
    //data phase timing for CAN FD. Only present in CANlib builds that support FD.
    typedef int  (KVASER_CALL *fnSetBusParamsFd)(int handle, long freq_brs, unsigned int tseg1_brs,
                                                 unsigned int tseg2_brs, unsigned int sjw_brs);
    typedef int  (KVASER_CALL *fnSetBusOutputControl)(int handle, unsigned int drivertype);
    typedef int  (KVASER_CALL *fnBusOn)(int handle);
    typedef int  (KVASER_CALL *fnBusOff)(int handle);
    typedef int  (KVASER_CALL *fnWrite)(int handle, long id, void *msg, unsigned int dlc, unsigned int flag);
    typedef int  (KVASER_CALL *fnRead)(int handle, long *id, void *msg, unsigned int *dlc,
                                       unsigned int *flag, unsigned long *time);
    typedef int  (KVASER_CALL *fnGetChannelData)(int channel, int item, void *buffer, size_t bufsize);

    fnInitializeLibrary canInitializeLibrary;
    fnUnloadLibrary canUnloadLibrary;
    fnGetNumberOfChannels canGetNumberOfChannels;
    fnOpenChannel canOpenChannel;
    fnClose canClose;
    fnSetBusParams canSetBusParams;
    fnSetBusParamsFd canSetBusParamsFd;   //null when this CANlib has no FD support
    fnSetBusOutputControl canSetBusOutputControl;
    fnBusOn canBusOn;
    fnBusOff canBusOff;
    fnWrite canWrite;
    fnRead canRead;
    fnGetChannelData canGetChannelData;

    QLibrary mLib;
    QTimer mTimer;
    int mHandle;   //CANlib handle, negative means not open
    int mChannel;  //CANlib channel number
    int mBusSpeed;
    //whether the channel was actually opened in CAN FD mode
    bool mFdEnabled;
    qint64 mTimeBasis;
    bool mHaveTimeBasis;
};

#endif // KVASERCANLIB_H
