#pragma once

#include <QDateTime>
#include <QMainWindow>
#include <QSettings>

class QAction;
class BackgroundWidget;
class EditorHighlighter;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTimer;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openInitialFile(const QString& path);
    void restoreSession();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupActions();
    void applyAppearance();
    void ensureWorkspace();
    QString workspaceDirectory() const;
    QString selectedDocumentPath() const;
    QString documentKey(const QString& path) const;

    void refreshFileList();
    void toggleSidebar();
    void chooseWorkspace();
    void createDocument();
    void renameDocument();
    void moveDocumentToTrash();
    bool openDocument(const QString& path);
    bool saveCurrentDocument(bool forceBackup = false);
    void createBackup(bool force);
    void saveCursorPosition();

    void onTextChanged();
    void centerCurrentLine();
    void updateStatistics(int currentWords = -1);
    void ensureDailyBaseline(int currentWords);
    void persistDailyProgress(int currentWords);
    int dailyWordCount(int currentWords) const;

    void showFindReplace();
    void showPreferences();
    void startWritingTimer();
    void updateWritingTimer();
    void requestPowerOff();
    void toggleFullScreen();

    BackgroundWidget* m_background = nullptr;
    QWidget* m_sidebar = nullptr;
    QListWidget* m_fileList = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    EditorHighlighter* m_highlighter = nullptr;
    QLabel* m_documentLabel = nullptr;
    QLabel* m_statisticsLabel = nullptr;
    QLabel* m_goalLabel = nullptr;
    QLabel* m_timerLabel = nullptr;
    QTimer* m_saveTimer = nullptr;
    QTimer* m_writingTimer = nullptr;
    QAction* m_typewriterAction = nullptr;
    QAction* m_spellCheckAction = nullptr;

    QSettings m_settings;
    QString m_currentPath;
    QDateTime m_lastBackupAt;
    int m_timerRemainingSeconds = 0;
    bool m_loading = false;
    bool m_dirty = false;
};
