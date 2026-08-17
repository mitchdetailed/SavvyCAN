#-------------------------------------------------
#
# Project created by QtCreator 2015-04-25T22:57:44
#
#-------------------------------------------------

!versionAtLeast(QT_VERSION, 5.14.0) {
    error("Current version of Qt ($${QT_VERSION}) is too old, this project requires Qt 5.14 or newer")
}

QT = core gui printsupport qml serialbus serialport widgets help network opengl concurrent

CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT

CONFIG += c++17
CONFIG += NO_UNIT_TESTS

DEFINES += QCUSTOMPLOT_USE_OPENGL

TARGET = SavvyCAN
TEMPLATE = app

QMAKE_INFO_PLIST = Info.plist.template
ICON = icons/SavvyIcon.icns

SOURCES += main.cpp\
    canbridgewindow.cpp \
    connections/canlogserver.cpp \
    connections/canserver.cpp \
    connections/lawicel_serial.cpp \
    connections/mqtt_bus.cpp \
    dbc/dbcnodeduplicateeditor.cpp \
    framesenderobject.cpp \
    mqtt/qmqtt_client.cpp \
    mqtt/qmqtt_client_p.cpp \
    mqtt/qmqtt_frame.cpp \
    mqtt/qmqtt_message.cpp \
    mqtt/qmqtt_network.cpp \
    mqtt/qmqtt_router.cpp \
    mqtt/qmqtt_routesubscription.cpp \
    mqtt/qmqtt_socket.cpp \
    mqtt/qmqtt_ssl_socket.cpp \
    mqtt/qmqtt_timer.cpp \
    mqtt/qmqtt_websocket.cpp \
    mqtt/qmqtt_websocketiodevice.cpp \
    qcpaxistickerhex.cpp \
    re/dbccomparatorwindow.cpp \
    mainwindow.cpp \
    canframemodel.cpp \
    simplecrypt.cpp \
    triggerdialog.cpp \
    utility.cpp \
    qcustomplot.cpp \
    frameplaybackwindow.cpp \
    candatagrid.cpp \
    framesenderwindow.cpp \
    framefileio.cpp \
    mainsettingsdialog.cpp \
    scriptingwindow.cpp \
    scriptcontainer.cpp \
    canfilter.cpp \
    can_structs.cpp \
    motorcontrollerconfigwindow.cpp \
    connections/canconnection.cpp \
    connections/serialbusconnection.cpp \
    connections/canconfactory.cpp \
    connections/gvretserial.cpp \
    connections/socketcand.cpp \
    connections/canconmanager.cpp \
    connections/gsusbconnection.cpp \
    connections/seeedcan.cpp \
    connections/robotellcan.cpp \
    connections/pythoncanserial.cpp \
    connections/udpmulticast.cpp \
    utils/msgpackcodec.cpp \
    connections/canalystii.cpp \
    connections/kvasercanlib.cpp \
    connections/ixxatvci.cpp \
    connections/usb2canlib.cpp \
    connections/iscanlib.cpp \
    connections/nicanlib.cpp \
    connections/neousyscan.cpp \
    re/sniffer/snifferitem.cpp \
    re/sniffer/sniffermodel.cpp \
    re/sniffer/snifferwindow.cpp \
    dbc/dbcmessageeditor.cpp \
    dbc/dbc_classes.cpp \
    dbc/dbchandler.cpp \
    dbc/dbcloadsavewindow.cpp \
    dbc/dbcmaineditor.cpp \
    dbc/dbcnodeeditor.cpp \
    dbc/dbcsignaleditor.cpp \
    dbc/dbcnoderebaseeditor.cpp \
    re/discretestatewindow.cpp \
    re/filecomparatorwindow.cpp \
    re/flowviewwindow.cpp \
    re/frameinfowindow.cpp \
    re/fuzzingwindow.cpp \
    re/isotp_interpreterwindow.cpp \
    re/rangestatewindow.cpp \
    re/udsscanwindow.cpp \
    connections/canbus.cpp \
    connections/canconnectionmodel.cpp \
    connections/connectionwindow.cpp \
    re/graphingwindow.cpp \
    re/newgraphdialog.cpp \
    bisectwindow.cpp \
    signalviewerwindow.cpp \
    bus_protocols/isotp_handler.cpp \
    bus_protocols/j1939_handler.cpp \
    bus_protocols/uds_handler.cpp \
    jsedit.cpp \
    frameplaybackobject.cpp \
    helpwindow.cpp \
    blfhandler.cpp \
    re/sniffer/SnifferDelegate.cpp \
    connections/newconnectiondialog.cpp \
    re/temporalgraphwindow.cpp \
    re/udsfirmwareuploaderwindow.cpp \
    filterutility.cpp \
    pcaplite.cpp

HEADERS  += mainwindow.h \
    can_structs.h \
    canbridgewindow.h \
    canframemodel.h \
    connections/canlogserver.h \
    connections/canserver.h \
    connections/lawicel_serial.h \
    connections/socketcand.h \
    connections/mqtt_bus.h \
    dbc/dbcnodeduplicateeditor.h \
    dbc/dbcnoderebaseeditor.h \
    framesenderobject.h \
    mqtt/qmqtt.h \
    mqtt/qmqtt_client.h \
    mqtt/qmqtt_client_p.h \
    mqtt/qmqtt_frame.h \
    mqtt/qmqtt_global.h \
    mqtt/qmqtt_message.h \
    mqtt/qmqtt_message_p.h \
    mqtt/qmqtt_network_p.h \
    mqtt/qmqtt_networkinterface.h \
    mqtt/qmqtt_routedmessage.h \
    mqtt/qmqtt_router.h \
    mqtt/qmqtt_routesubscription.h \
    mqtt/qmqtt_socket_p.h \
    mqtt/qmqtt_socketinterface.h \
    mqtt/qmqtt_ssl_socket_p.h \
    mqtt/qmqtt_timer_p.h \
    mqtt/qmqtt_timerinterface.h \
    mqtt/qmqtt_websocket_p.h \
    mqtt/qmqtt_websocketiodevice_p.h \
    qcpaxistickerhex.h \
    re/dbccomparatorwindow.h \
    simplecrypt.h \
    triggerdialog.h \
    utility.h \
    qcustomplot.h \
    frameplaybackwindow.h \
    candatagrid.h \
    framesenderwindow.h \
    can_trigger_structs.h \
    framefileio.h \
    config.h \
    mainsettingsdialog.h \
    scriptingwindow.h \
    scriptcontainer.h \
    canfilter.h \
    utils/lfqueue.h \
    utils/msgpackcodec.h \
    motorcontrollerconfigwindow.h \
    connections/canconnection.h \
    connections/serialbusconnection.h \
    connections/canconconst.h \
    connections/canconfactory.h \
    connections/gvretserial.h \
    connections/canconmanager.h \
    connections/gsusbconnection.h \
    connections/seeedcan.h \
    connections/robotellcan.h \
    connections/pythoncanserial.h \
    connections/udpmulticast.h \
    connections/canalystii.h \
    connections/kvasercanlib.h \
    connections/ixxatvci.h \
    connections/usb2canlib.h \
    connections/iscanlib.h \
    connections/nicanlib.h \
    connections/neousyscan.h \
    re/sniffer/snifferitem.h \
    re/sniffer/sniffermodel.h \
    re/sniffer/snifferwindow.h \
    dbc/dbc_classes.h \
    dbc/dbchandler.h \
    dbc/dbcloadsavewindow.h \
    dbc/dbcmaineditor.h \
    dbc/dbcsignaleditor.h \
    dbc/dbcmessageeditor.h \
    dbc/dbcnodeeditor.h \
    re/discretestatewindow.h \
    re/filecomparatorwindow.h \
    re/flowviewwindow.h \
    re/frameinfowindow.h \
    re/fuzzingwindow.h \
    re/isotp_interpreterwindow.h \
    re/rangestatewindow.h \
    re/udsscanwindow.h \
    connections/canbus.h \
    connections/canconnectionmodel.h \
    connections/connectionwindow.h \
    re/graphingwindow.h \
    re/newgraphdialog.h \
    bisectwindow.h \
    signalviewerwindow.h \
    bus_protocols/isotp_handler.h \
    bus_protocols/j1939_handler.h \
    bus_protocols/uds_handler.h \
    bus_protocols/isotp_message.h \
    jsedit.h \
    frameplaybackobject.h \
    helpwindow.h \
    blfhandler.h \
    re/sniffer/SnifferDelegate.h \
    connections/newconnectiondialog.h \
    re/temporalgraphwindow.h \
    re/udsfirmwareuploaderwindow.h \
    filterutility.h \
    pcaplite.h

FORMS    += ui/candatagrid.ui \
    triggerdialog.ui \
    ui/canbridgewindow.ui \
    ui/dbcnodeduplicateeditor.ui \
    ui/dbccomparatorwindow.ui \
    ui/dbcmessageeditor.ui \
    ui/connectionwindow.ui \
    ui/dbcloadsavewindow.ui \
    ui/dbcmaineditor.ui \
    ui/dbcnoderebaseeditor.ui \
    ui/dbcsignaleditor.ui \
    ui/dbcnodeeditor.ui \
    ui/discretestatewindow.ui \
    ui/filecomparatorwindow.ui \
    ui/flowviewwindow.ui \
    ui/frameinfowindow.ui \
    ui/frameplaybackwindow.ui \
    ui/framesenderwindow.ui \
    ui/fuzzingwindow.ui \
    ui/graphingwindow.ui \
    ui/isotp_interpreterwindow.ui \
    ui/mainsettingsdialog.ui \
    ui/mainwindow.ui \
    ui/motorcontrollerconfigwindow.ui \
    ui/newgraphdialog.ui \
    ui/rangestatewindow.ui \
    ui/scriptingwindow.ui \
    ui/snifferwindow.ui \
    ui/udsscanwindow.ui \
    ui/bisectwindow.ui \
    ui/signalviewerwindow.ui \
    ui/helpwindow.ui \
    ui/newconnectiondialog.ui \
    ui/temporalgraphwindow.ui \
    ui/udsfirmwareuploaderwindow.ui
    
RESOURCES += \
    icons.qrc \
    images.qrc

#ASAM MDF4 (.mf4) support needs the mdflib submodule to be checked out and built
#in third_party/mdflib. Once it is there run qmake with CONFIG+=mdf4 to turn the
#format on. Without it SavvyCAN simply does not offer .mf4 in the file dialogs.
mdf4 {
   DEFINES += MDF4_SUPPORT
   INCLUDEPATH += $$PWD/third_party/mdflib/include
   win32-g++ {
      LIBS += -L$$PWD/third_party/mdflib/build/mdflib -lmdf
      LIBS += -L"C:/Program Files/mingw64/x86_64-w64-mingw32/lib" -lz
      LIBS += -L"C:/Program Files/mingw64/opt/lib" -lexpat
   }
   !win32-g++ {
      LIBS += -L$$PWD/third_party/mdflib/build/mdflib -lmdf -lz -lexpat
   }
}

#libusb is needed by the GSUSB (Candlelight) connection. On windows we use the
#copy that ships in third_party, everywhere else we pick up the system one.
win32-msvc* {
   LIBS += opengl32.lib
   INCLUDEPATH += $$PWD/third_party/libusb/include
   contains(QMAKE_TARGET.arch, x86_64) {
      LIBS += -L$$PWD/third_party/libusb/VS2022/MS64/dll
   } else {
      LIBS += -L$$PWD/third_party/libusb/VS2022/MS32/dll
   }
   LIBS += -llibusb-1.0
}

win32-g++ {
   LIBS += libopengl32
   INCLUDEPATH += $$PWD/third_party/libusb/include
   LIBS += -L$$PWD/third_party/libusb/MinGW64/static -lusb-1.0
}

#Both windows link paths above resolve -lusb-1.0 to the import library, so the DLL has to sit next
#to the binary or it will not start at all - Windows reports the missing library before main() runs.
#Nothing else copies it for a plain build, so do it here and every local build just works.
win32 {
   win32-msvc* {
      contains(QMAKE_TARGET.arch, x86_64) {
         LIBUSB_DLL = $$PWD/third_party/libusb/VS2022/MS64/dll/libusb-1.0.dll
      } else {
         LIBUSB_DLL = $$PWD/third_party/libusb/VS2022/MS32/dll/libusb-1.0.dll
      }
   } else {
      LIBUSB_DLL = $$PWD/third_party/libusb/MinGW64/dll/libusb-1.0.dll
   }

   LIBUSB_DEST = $$OUT_PWD
   !isEmpty(DESTDIR): LIBUSB_DEST = $$DESTDIR
   else: CONFIG(release, debug|release): LIBUSB_DEST = $$OUT_PWD/release
   else: LIBUSB_DEST = $$OUT_PWD/debug

   QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$LIBUSB_DLL)) $$shell_quote($$shell_path($$LIBUSB_DEST))
}

unix {
   packagesExist(libusb-1.0) {
      CONFIG += link_pkgconfig
      PKGCONFIG += libusb-1.0
   } else {
      #no pkg-config available, fall back to the usual install locations
      INCLUDEPATH += /usr/include/libusb-1.0 /usr/local/include/libusb-1.0 /opt/homebrew/include/libusb-1.0
      LIBS += -L/usr/local/lib -L/opt/homebrew/lib -lusb-1.0
   }

   isEmpty(PREFIX) {
      PREFIX=/usr/local
   }
   target.path = $$PREFIX/bin
   shortcutfiles.files=SavvyCAN.desktop
   shortcutfiles.path = $$PREFIX/share/applications
   INSTALLS += shortcutfiles
   DISTFILES += SavvyCAN.desktop
}

windows {
RC_ICONS=icons/SavvyIcon.ico
}

examplefiles.files=examples
examplefiles.path = $$PREFIX/share/savvycan/examples
INSTALLS += examplefiles

iconfiles.files=icons
iconfiles.path = $$PREFIX/share
INSTALLS += iconfiles

helpfiles.files=help/*
helpfiles.path = $$PREFIX/bin/help
INSTALLS += helpfiles

INSTALLS += target

TRANSLATIONS += \
    translations/SavvyCAN_en.ts \
    translations/SavvyCAN_pt_BR.ts

DISTFILES += \
    translations/pt_BR.qph
