#ifndef TST_FILEIO_H
#define TST_FILEIO_H

#include <QObject>
#include <QTemporaryDir>

/*
 * Regression tests for the log file loaders.
 *
 * Two jobs. First, prove that good files still load - the guards added for malformed input must not
 * start rejecting valid logs. Second, throw truncated and corrupt files at every loader and require
 * that each one returns rather than crashing, hanging or reading past the end of a buffer.
 *
 * The corrupt corpus is generated here rather than committed so each case documents what it is
 * testing. Several of these files are the exact shapes that used to hang the program.
 */
class TestFileIO: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    //valid files still load correctly
    void canDumpRoundTrip();
    void nativeCSVRoundTrip();
    void crtdRoundTrip();

    //malformed text input
    void malformedTextFiles();
    void malformedTextFiles_data();

    //malformed binary input, including the two shapes that used to hang
    void malformedBinaryFiles();
    void malformedBinaryFiles_data();

    //a valid BLF must still parse after the hardening
    void blfContainerStillParses();

    //format probes must not claim an empty or junk file as their own
    void probesRejectEmptyFile();
    void probesRejectJunk();
    //...but must still recognise the real thing
    void probesAcceptTheirOwnFormat();

private:
    QTemporaryDir mDir;
    QString path(const QString &name) const;
    void writeFile(const QString &name, const QByteArray &data);
};

#endif // TST_FILEIO_H
