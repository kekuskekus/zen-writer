#pragma once

#include <QString>

namespace ZenWriter {

int wordCount(const QString& text);
QString normalizedDocumentName(const QString& requestedName);
bool isSupportedDocument(const QString& path);

} // namespace ZenWriter
