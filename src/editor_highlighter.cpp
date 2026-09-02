#include "editor_highlighter.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>

#ifdef ZEN_WRITER_HAS_HUNSPELL
#include <hunspell.hxx>
#endif

class SpellDictionary
{
public:
#ifdef ZEN_WRITER_HAS_HUNSPELL
    explicit SpellDictionary(const QString& basePath)
        : hunspell((basePath + QStringLiteral(".aff")).toUtf8().constData(),
                   (basePath + QStringLiteral(".dic")).toUtf8().constData())
    {
    }

    bool contains(const QString& word)
    {
        return hunspell.spell(word.toUtf8().constData()) != 0;
    }

private:
    Hunspell hunspell;
#else
    explicit SpellDictionary(const QString&) {}
    bool contains(const QString&) { return true; }
#endif
};

EditorHighlighter::EditorHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document)
{
    loadDictionaries();
}

EditorHighlighter::~EditorHighlighter() = default;

void EditorHighlighter::setSpellCheckEnabled(bool enabled)
{
    if (m_spellCheckEnabled == enabled) {
        return;
    }
    m_spellCheckEnabled = enabled;
    rehighlight();
}

void EditorHighlighter::setDarkTheme(bool dark)
{
    if (m_darkTheme == dark) {
        return;
    }
    m_darkTheme = dark;
    rehighlight();
}

void EditorHighlighter::loadDictionaries()
{
#ifdef ZEN_WRITER_HAS_HUNSPELL
    const QStringList roots = {
        QStringLiteral("/usr/share/hunspell"),
        QStringLiteral("/usr/share/myspell/dicts"),
    };
    const QStringList names = {
        QStringLiteral("ru_RU"),
        QStringLiteral("ru_RU_yo"),
        QStringLiteral("en_US"),
        QStringLiteral("en_GB"),
    };

    for (const QString& root : roots) {
        for (const QString& name : names) {
            const QString base = QDir(root).filePath(name);
            if (QFileInfo::exists(base + QStringLiteral(".aff"))
                && QFileInfo::exists(base + QStringLiteral(".dic"))) {
                m_dictionaries.push_back(std::make_unique<SpellDictionary>(base));
            }
        }
    }
#endif
}

bool EditorHighlighter::isSpelledCorrectly(const QString& word) const
{
    if (m_dictionaries.empty()) {
        return true;
    }

    for (const auto& dictionary : m_dictionaries) {
        if (dictionary->contains(word)) {
            return true;
        }
    }
    return false;
}

void EditorHighlighter::highlightBlock(const QString& text)
{
    const QColor accent = m_darkTheme ? QColor(QStringLiteral("#9ecbff"))
                                      : QColor(QStringLiteral("#245b8a"));
    const QColor subtle = m_darkTheme ? QColor(QStringLiteral("#91a38e"))
                                      : QColor(QStringLiteral("#557255"));
    const QColor code = m_darkTheme ? QColor(QStringLiteral("#e8c48a"))
                                    : QColor(QStringLiteral("#8a521f"));

    QTextCharFormat headingFormat;
    headingFormat.setForeground(accent);
    headingFormat.setFontWeight(QFont::Bold);

    QTextCharFormat emphasisFormat;
    emphasisFormat.setForeground(accent);
    emphasisFormat.setFontWeight(QFont::DemiBold);

    QTextCharFormat subtleFormat;
    subtleFormat.setForeground(subtle);

    QTextCharFormat codeFormat;
    codeFormat.setForeground(code);
    codeFormat.setFontFamilies({QStringLiteral("monospace")});

    const auto apply = [this, &text](const QString& pattern, const QTextCharFormat& format) {
        QRegularExpression expression(pattern,
                                      QRegularExpression::UseUnicodePropertiesOption);
        auto matches = expression.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    };

    apply(QStringLiteral("^#{1,6}\\s.*$"), headingFormat);
    apply(QStringLiteral("^\\s*(?:>|[-+*] |\\d+\\. ).*$"), subtleFormat);
    apply(QStringLiteral("(?:\\*\\*[^*]+\\*\\*|__[^_]+__)"), emphasisFormat);
    apply(QStringLiteral("(?:^|[^*])\\*[^*\\n]+\\*(?:[^*]|$)|(?:^|[^_])_[^_\\n]+_(?:[^_]|$)"),
          emphasisFormat);
    apply(QStringLiteral("`[^`]+`"), codeFormat);
    apply(QStringLiteral("\\[[^\\]]+\\]\\([^)]+\\)"), emphasisFormat);

    if (!m_spellCheckEnabled || m_dictionaries.empty()) {
        return;
    }

    static const QRegularExpression wordPattern(
        QStringLiteral("[\\p{L}]+(?:['’\u2010-\u2015-][\\p{L}]+)*"),
        QRegularExpression::UseUnicodePropertiesOption);
    auto words = wordPattern.globalMatch(text);
    while (words.hasNext()) {
        const auto match = words.next();
        const QString word = match.captured();
        if (word.size() < 3 || isSpelledCorrectly(word)) {
            continue;
        }

        QTextCharFormat misspelled = format(match.capturedStart());
        misspelled.setUnderlineColor(QColor(QStringLiteral("#e65f5f")));
        misspelled.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        setFormat(match.capturedStart(), match.capturedLength(), misspelled);
    }
}
