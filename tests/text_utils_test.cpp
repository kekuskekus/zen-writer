#include "text_utils.h"

#include <QtTest>

class TextUtilsTest final : public QObject
{
    Q_OBJECT

private slots:
    void countsUnicodeWords()
    {
        QCOMPARE(ZenWriter::wordCount(QStringLiteral("Один, два — three-four.")), 3);
    }

    void normalizesNames()
    {
        QCOMPARE(ZenWriter::normalizedDocumentName(QStringLiteral("  Глава 1  ")),
                 QStringLiteral("Глава 1.md"));
        QCOMPARE(ZenWriter::normalizedDocumentName(QStringLiteral("draft.TXT")),
                 QStringLiteral("draft.TXT"));
        QCOMPARE(ZenWriter::normalizedDocumentName(QStringLiteral("../note.md")),
                 QStringLiteral("note.md"));
    }

    void recognizesDocuments()
    {
        QVERIFY(ZenWriter::isSupportedDocument(QStringLiteral("draft.md")));
        QVERIFY(ZenWriter::isSupportedDocument(QStringLiteral("draft.TXT")));
        QVERIFY(!ZenWriter::isSupportedDocument(QStringLiteral("draft.docx")));
    }
};

QTEST_GUILESS_MAIN(TextUtilsTest)
#include "text_utils_test.moc"
