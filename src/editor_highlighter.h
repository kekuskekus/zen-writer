#pragma once

#include <QSyntaxHighlighter>

#include <memory>
#include <vector>

class SpellDictionary;

class EditorHighlighter final : public QSyntaxHighlighter
{
public:
    explicit EditorHighlighter(QTextDocument* document);
    ~EditorHighlighter() override;

    void setSpellCheckEnabled(bool enabled);
    void setDarkTheme(bool dark);
    void setMatrixTheme(bool matrix);

protected:
    void highlightBlock(const QString& text) override;

private:
    void loadDictionaries();
    bool isSpelledCorrectly(const QString& word) const;

    bool m_spellCheckEnabled = true;
    bool m_darkTheme = true;
    bool m_matrixTheme = false;
    std::vector<std::unique_ptr<SpellDictionary>> m_dictionaries;
};
