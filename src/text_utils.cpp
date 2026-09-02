#include "text_utils.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace ZenWriter {

int wordCount(const QString& text)
{
    static const QRegularExpression words(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['’\u2010-\u2015-][\\p{L}\\p{N}]+)*"),
        QRegularExpression::UseUnicodePropertiesOption);

    int count = 0;
    auto matches = words.globalMatch(text);
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    return count;
}

QString normalizedDocumentName(const QString& requestedName)
{
    QString name = QFileInfo(requestedName.trimmed()).fileName();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return {};
    }

    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
        && !name.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        name += QStringLiteral(".md");
    }
    return name;
}

bool isSupportedDocument(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("txt");
}

} // namespace ZenWriter
