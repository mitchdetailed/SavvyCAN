#include <QtTest>
#include <QApplication>

#include "tst_lfqueue.h"
#include "tst_codecs.h"
#include "tst_fileio.h"
#include "tst_framemodel.h"

/*
 * Runs every test class in turn and returns non zero if any of them failed, which is what CI keys
 * off. QApplication rather than QCoreApplication because the file loaders call qApp->processEvents()
 * while loading and some of them construct widgets for error dialogs.
 *
 * TestCanCon is not run here - it needs a real vcan0 interface. See the note in test.pro.
 */
int main(int argc, char** argv)
{
   QApplication app(argc, argv);

   /* qExec writes to whatever -o names, so running several classes in a row would leave only the
    * last one's results on disk. Give each class its own file by folding the class name into the
    * argument, which keeps CI logs readable when something fails. */
   QStringList baseArgs;
   QString outFile, outFormat;
   for (int i = 0; i < argc; i++)
   {
       const QString arg = QString::fromLocal8Bit(argv[i]);
       if (arg == "-o" && (i + 1) < argc)
       {
           const QString spec = QString::fromLocal8Bit(argv[++i]);
           const int comma = spec.lastIndexOf(',');
           if (comma > 0)
           {
               outFile = spec.left(comma);
               outFormat = spec.mid(comma + 1);
           }
           else outFile = spec;
           continue;
       }
       baseArgs << arg;
   }

   int status = 0;
   auto ASSERT_TEST = [&](QObject* obj, const char *name) {
       QStringList args = baseArgs;
       if (!outFile.isEmpty())
       {
           QString perClass = outFile;
           const int dot = perClass.lastIndexOf('.');
           if (dot > 0) perClass.insert(dot, QString("_") + name);
           else perClass += QString("_") + name;
           args << "-o" << (outFormat.isEmpty() ? perClass : perClass + "," + outFormat);
       }

       QList<QByteArray> stash;
       QVector<char*> argvCopy;
       for (const QString &a : args) stash.append(a.toLocal8Bit());
       for (QByteArray &a : stash) argvCopy.append(a.data());

       status |= QTest::qExec(obj, argvCopy.size(), argvCopy.data());
       delete obj;
   };

   ASSERT_TEST(new TestLFQueue(), "lfqueue");
   ASSERT_TEST(new TestCodecs(), "codecs");
   ASSERT_TEST(new TestFileIO(), "fileio");
   ASSERT_TEST(new TestFrameModel(), "framemodel");

   return status;
}
