QT += core gui widgets network serialbus serialport testlib

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = savvycan_tests

INCLUDEPATH += ../ ../connections ../utils

# Only the pieces under test plus what they need to link. Deliberately not the whole application:
# these tests are meant to run anywhere, including CI, with no hardware and no display.
SOURCES += \
    main.cpp \
    tst_lfqueue.cpp \
    tst_codecs.cpp \
    tst_fileio.cpp \
    tst_framemodel.cpp \
    ../utils/msgpackcodec.cpp \
    ../framefileio.cpp \
    ../canframemodel.cpp \
    ../dbc/dbchandler.cpp \
    ../dbc/dbc_classes.cpp \
    ../blfhandler.cpp \
    ../pcaplite.cpp \
    ../can_structs.cpp \
    ../utility.cpp \
    ../connections/canconnection.cpp \
    ../connections/canbus.cpp \
    ../connections/seeedcan.cpp \
    ../connections/canalystii.cpp \
    ../connections/ixxatvci.cpp \
    ../connections/kvasercanlib.cpp

HEADERS += \
    tst_lfqueue.h \
    tst_codecs.h \
    tst_fileio.h \
    tst_framemodel.h \
    ../canframemodel.h \
    ../dbc/dbchandler.h \
    ../utils/lfqueue.h \
    ../utils/msgpackcodec.h \
    ../framefileio.h \
    ../blfhandler.h \
    ../pcaplite.h \
    ../can_structs.h \
    ../connections/canconnection.h \
    ../connections/canbus.h \
    ../connections/seeedcan.h \
    ../connections/canalystii.h \
    ../connections/ixxatvci.h \
    ../connections/kvasercanlib.h

# CANalyst-II talks to libusb directly, so the tests need it too even though they never open a device
win32-g++ {
   INCLUDEPATH += $$PWD/../third_party/libusb/include
   LIBS += -L$$PWD/../third_party/libusb/MinGW64/static -lusb-1.0
}
win32-msvc* {
   INCLUDEPATH += $$PWD/../third_party/libusb/include
   contains(QMAKE_TARGET.arch, x86_64) {
      LIBS += -L$$PWD/../third_party/libusb/VS2022/MS64/dll
   } else {
      LIBS += -L$$PWD/../third_party/libusb/VS2022/MS32/dll
   }
   LIBS += -llibusb-1.0
}
unix {
   packagesExist(libusb-1.0) {
      CONFIG += link_pkgconfig
      PKGCONFIG += libusb-1.0
   } else {
      INCLUDEPATH += /usr/include/libusb-1.0 /usr/local/include/libusb-1.0 /opt/homebrew/include/libusb-1.0
      LIBS += -L/usr/local/lib -L/opt/homebrew/lib -lusb-1.0
   }
}

# tst_cancon is an integration test: it needs a real vcan0 interface and it still refers to the
# old SOCKETCAN connection type that no longer exists. Left out of the default build; run it by
# hand on a Linux box with a virtual CAN interface set up.
# SOURCES += tst_cancon.cpp
# HEADERS += tst_cancon.h

target.path = .
INSTALLS += target
