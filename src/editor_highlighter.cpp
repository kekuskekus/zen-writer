#include "editor_highlighter.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>

#ifdef Q_OS_WIN
#include <spellcheck.h>
#include <windows.h>
#include <wrl/client.h>
#elif defined(ZEN_WRITER_HAS_HUNSPELL)
#include <hunspell.hxx>
#endif

#ifdef Q_OS_WIN
namespace {

class ComApartment final
{
public:
    ComApartment()
        : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
    {
    }

    ~ComApartment()
    {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }

private:
    HRESULT m_result;
};

ComApartment windowsComApartment;

} // namespace
#endif

class SpellDictionary
{
public:
#ifdef Q_OS_WIN
    explicit SpellDictionary(const QString& language)
    {
        Microsoft::WRL::ComPtr<ISpellCheckerFactory> factory;
        if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            return;
        }

        BOOL supported = FALSE;
        const auto locale = reinterpret_cast<const wchar_t*>(language.utf16());
        if (FAILED(factory->IsSupported(locale, &supported)) || !supported) {
            return;
        }
        factory->CreateSpellChecker(locale, m_spellChecker.GetAddressOf());
    }

    bool isAvailable() const
    {
        return m_spellChecker.Get() != nullptr;
    }

    bool contains(const QString& word)
    {
        if (!m_spellChecker) {
            return true;
        }

        Microsoft::WRL::ComPtr<IEnumSpellingError> errors;
        const auto text = reinterpret_cast<const wchar_t*>(word.utf16());
        if (FAILED(m_spellChecker->Check(text, errors.GetAddressOf())) || !errors) {
            return true;
        }

        Microsoft::WRL::ComPtr<ISpellingError> error;
        return errors->Next(error.GetAddressOf()) == S_FALSE;
    }

private:
    Microsoft::WRL::ComPtr<ISpellChecker> m_spellChecker;
#elif defined(ZEN_WRITER_HAS_HUNSPELL)
    explicit SpellDictionary(const QString& basePath)
        : hunspell((basePath + QStringLiteral(".aff")).toUtf8().constData(),
                   (basePath + QStringLiteral(".dic")).toUtf8().constData())
    {
    }

    bool contains(const QString& word)
    {
        return hunspell.spell(word.toUtf8().toStdString());
    }

    bool isAvailable() const { return true; }

private:
    Hunspell hunspell;
#else
    explicit SpellDictionary(const QString&) {}
    bool contains(const QString&) { return true; }
    bool isAvailable() const { return false; }
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

void EditorHighlighter::setMatrixTheme(bool matrix)
{
    if (m_matrixTheme == matrix) {
        return;
    }
    m_matrixTheme = matrix;
    rehighlight();
}

void EditorHighlighter::loadDictionaries()
{
#ifdef Q_OS_WIN
    for (const QString& language : {QStringLiteral("ru-RU"), QStringLiteral("en-US")}) {
        auto dictionary = std::make_unique<SpellDictionary>(language);
        if (dictionary->isAvailable()) {
            m_dictionaries.push_back(std::move(dictionary));
        }
    }
#elif defined(ZEN_WRITER_HAS_HUNSPELL)
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
                auto dictionary = std::make_unique<SpellDictionary>(base);
                if (dictionary->isAvailable()) {
                    m_dictionaries.push_back(std::move(dictionary));
                }
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
    const QColor accent = m_matrixTheme
        ? QColor(QStringLiteral("#c7ffd0"))
        : (m_darkTheme ? QColor(QStringLiteral("#9ecbff"))
                       : QColor(QStringLiteral("#245b8a")));
    const QColor subtle = m_matrixTheme
        ? QColor(QStringLiteral("#42c966"))
        : (m_darkTheme ? QColor(QStringLiteral("#91a38e"))
                       : QColor(QStringLiteral("#557255")));
    const QColor code = m_matrixTheme
        ? QColor(QStringLiteral("#70ff8d"))
        : (m_darkTheme ? QColor(QStringLiteral("#e8c48a"))
                       : QColor(QStringLiteral("#8a521f")));

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
