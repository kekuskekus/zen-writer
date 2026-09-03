#include "main_window.h"

#include "editor_highlighter.h"
#include "text_utils.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyCombination>
#include <QLabel>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSizePolicy>
#include <QStringConverter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

class BackgroundWidget final : public QWidget
{
public:
    explicit BackgroundWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        m_matrixFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_matrixFont.setPixelSize(MatrixCellSize - 2);
        m_matrixFont.setBold(true);
        m_matrixTimer.setInterval(100);
        m_matrixTimer.setTimerType(Qt::CoarseTimer);
        connect(&m_matrixTimer, &QTimer::timeout, this, [this] {
            advanceMatrix();
        });
    }

    void setBackground(const QString& path, const QColor& fallback)
    {
        m_fallback = fallback;
        m_pixmap = path.isEmpty() ? QPixmap() : QPixmap(path);
        update();
    }

    void setMatrixEnabled(bool enabled)
    {
        if (m_matrixEnabled == enabled) {
            return;
        }
        m_matrixEnabled = enabled;
        if (enabled) {
            resetMatrix();
            m_matrixTimer.start();
        } else {
            m_matrixTimer.stop();
            m_matrixDrops.clear();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.fillRect(rect(), m_fallback);
        if (!m_pixmap.isNull()) {
            const QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
            const QPoint origin((width() - scaled.width()) / 2,
                                (height() - scaled.height()) / 2);
            painter.drawPixmap(origin, scaled);
        }

        if (!m_matrixEnabled) {
            return;
        }

        painter.fillRect(rect(), QColor(0, 5, 1, 105));
        painter.setFont(m_matrixFont);

        static const QString glyphs = QStringLiteral(
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%&*+-<>[]{}");
        for (qsizetype column = 0; column < m_matrixDrops.size(); ++column) {
            const MatrixDrop& drop = m_matrixDrops.at(column);
            for (int trail = 0; trail <= drop.trail; ++trail) {
                const int row = drop.row - trail;
                const int y = (row + 1) * MatrixCellSize;
                if (y < 0 || y > height() + MatrixCellSize) {
                    continue;
                }

                const int glyphIndex = std::abs(
                    (static_cast<int>(column) * 17) + (row * 13) + m_matrixFrame)
                    % glyphs.size();
                if (trail == 0) {
                    painter.setPen(QColor(210, 255, 218, 235));
                } else {
                    const int alpha = std::max(18, 175 - (trail * 15));
                    painter.setPen(QColor(28, 255, 83, alpha));
                }
                painter.drawText(static_cast<int>(column) * MatrixCellSize, y,
                                 QString(glyphs.at(glyphIndex)));
            }
        }

        painter.setPen(QColor(0, 0, 0, 22));
        for (int y = 0; y < height(); y += 5) {
            painter.drawLine(0, y, width(), y);
        }
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (m_matrixEnabled) {
            resetMatrix();
        }
    }

private:
    struct MatrixDrop
    {
        int row = 0;
        int speed = 1;
        int trail = 8;
    };

    void resetMatrix()
    {
        const int columns = std::max(1, (width() / MatrixCellSize) + 1);
        const int rows = std::max(1, (height() / MatrixCellSize) + 1);
        m_matrixDrops.resize(columns);
        for (MatrixDrop& drop : m_matrixDrops) {
            drop.row = -QRandomGenerator::global()->bounded(rows + 1);
            drop.speed = 1 + QRandomGenerator::global()->bounded(2);
            drop.trail = 6 + QRandomGenerator::global()->bounded(10);
        }
    }

    void advanceMatrix()
    {
        const int rows = std::max(1, (height() / MatrixCellSize) + 1);
        for (MatrixDrop& drop : m_matrixDrops) {
            drop.row += drop.speed;
            if (drop.row - drop.trail > rows
                && QRandomGenerator::global()->bounded(100) < 35) {
                drop.row = -QRandomGenerator::global()->bounded(rows + 1);
                drop.speed = 1 + QRandomGenerator::global()->bounded(2);
                drop.trail = 6 + QRandomGenerator::global()->bounded(10);
            }
        }
        m_matrixFrame = (m_matrixFrame + 1) % 1000000;
        update();
    }

    static constexpr int MatrixCellSize = 18;
    QColor m_fallback = QColor(QStringLiteral("#16191d"));
    QPixmap m_pixmap;
    QFont m_matrixFont;
    QTimer m_matrixTimer;
    QVector<MatrixDrop> m_matrixDrops;
    int m_matrixFrame = 0;
    bool m_matrixEnabled = false;
};

namespace {

constexpr int AutosaveDelayMs = 1500;
constexpr int BackupIntervalSeconds = 300;
constexpr int BackupsToKeep = 50;

const auto& configurableShortcutDefaults()
{
    static const auto defaults = std::array{
        std::pair{QStringLiteral("shortcuts/save"), QKeySequence(QStringLiteral("Ctrl+S"))},
        std::pair{QStringLiteral("shortcuts/find"), QKeySequence(QStringLiteral("Ctrl+F"))},
        std::pair{QStringLiteral("shortcuts/quit"), QKeySequence(QStringLiteral("Ctrl+Q"))},
    };
    return defaults;
}

const auto& fixedShortcutBindings()
{
    static const auto bindings = std::array{
        std::pair{QStringLiteral("Список файлов"), QKeySequence(QStringLiteral("Ctrl+O"))},
        std::pair{QStringLiteral("Новый текст"), QKeySequence(QKeySequence::New)},
        std::pair{QStringLiteral("Выбрать папку"), QKeySequence(QStringLiteral("Ctrl+Shift+O"))},
        std::pair{QStringLiteral("Настройки"), QKeySequence(QStringLiteral("Ctrl+,"))},
        std::pair{QStringLiteral("Таймер"), QKeySequence(QStringLiteral("Ctrl+T"))},
        std::pair{QStringLiteral("Полный экран"), QKeySequence(Qt::Key_F11)},
        std::pair{QStringLiteral("Выключение"), QKeySequence(QStringLiteral("Ctrl+Shift+Q"))},
        std::pair{QStringLiteral("Печатная машинка"), QKeySequence(QStringLiteral("Ctrl+Alt+T"))},
        std::pair{QStringLiteral("Орфография"), QKeySequence(QStringLiteral("Ctrl+Alt+S"))},
        std::pair{QStringLiteral("Matrix-тема"), QKeySequence(QStringLiteral("Ctrl+Alt+M"))},
    };
    return bindings;
}

QKeySequence configuredShortcut(const QSettings& settings, const QString& key,
                                const QKeySequence& fallback)
{
    const QKeySequence sequence = QKeySequence::fromString(
        settings.value(key, fallback.toString(QKeySequence::PortableText)).toString(),
        QKeySequence::PortableText);
    return sequence.isEmpty() ? fallback : sequence;
}

bool isSafeEditorShortcut(const QKeySequence& sequence)
{
    if (sequence.isEmpty() || sequence.count() != 1) {
        return false;
    }
    const QKeyCombination combination = sequence[0];
    const Qt::Key key = combination.key();
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const bool hasCommandModifier = modifiers.testFlag(Qt::ControlModifier)
        || modifiers.testFlag(Qt::AltModifier)
        || modifiers.testFlag(Qt::MetaModifier);
    return hasCommandModifier
        || (key >= Qt::Key_F1 && key <= Qt::Key_F35);
}

QString readableTime(int totalSeconds)
{
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    setupActions();
    ensureWorkspace();
    applyAppearance();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("Zen Writer"));
    setMinimumSize(640, 400);

    m_background = new BackgroundWidget(this);
    auto* rootLayout = new QHBoxLayout(m_background);
    rootLayout->setContentsMargins(20, 20, 20, 12);
    rootLayout->setSpacing(18);

    m_sidebar = new QWidget(m_background);
    m_sidebar->setObjectName(QStringLiteral("fileSidebar"));
    m_sidebar->setFixedWidth(250);
    auto* sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(10, 10, 10, 10);

    auto* sidebarTitle = new QLabel(QStringLiteral("Тексты"), m_sidebar);
    QFont titleFont = sidebarTitle->font();
    titleFont.setBold(true);
    sidebarTitle->setFont(titleFont);
    sidebarLayout->addWidget(sidebarTitle);

    auto* fileButtons = new QHBoxLayout();
    auto* newButton = new QPushButton(QStringLiteral("+"), m_sidebar);
    newButton->setToolTip(QStringLiteral("Новый текст (Ctrl+N)"));
    auto* renameButton = new QPushButton(QStringLiteral("Имя"), m_sidebar);
    renameButton->setToolTip(QStringLiteral("Переименовать"));
    auto* trashButton = new QPushButton(QStringLiteral("В корзину"), m_sidebar);
    fileButtons->addWidget(newButton);
    fileButtons->addWidget(renameButton);
    fileButtons->addWidget(trashButton);
    sidebarLayout->addLayout(fileButtons);

    m_fileList = new QListWidget(m_sidebar);
    m_fileList->setAlternatingRowColors(false);
    sidebarLayout->addWidget(m_fileList, 1);
    m_sidebar->hide();

    m_editor = new QPlainTextEdit(m_background);
    m_editor->setObjectName(QStringLiteral("writingEditor"));
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setTabStopDistance(40.0);
    m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_editor->setPlaceholderText(QStringLiteral("Начните писать…"));

    rootLayout->addWidget(m_sidebar);
    rootLayout->addStretch(1);
    rootLayout->addWidget(m_editor, 4);
    rootLayout->addStretch(1);
    setCentralWidget(m_background);

    m_documentLabel = new QLabel(this);
    m_statisticsLabel = new QLabel(this);
    m_goalLabel = new QLabel(this);
    m_timerLabel = new QLabel(this);
    statusBar()->addWidget(m_documentLabel, 1);
    statusBar()->addPermanentWidget(m_statisticsLabel);
    statusBar()->addPermanentWidget(m_goalLabel);
    statusBar()->addPermanentWidget(m_timerLabel);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(AutosaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { saveCurrentDocument(); });

    m_writingTimer = new QTimer(this);
    m_writingTimer->setInterval(1000);
    connect(m_writingTimer, &QTimer::timeout, this, &MainWindow::updateWritingTimer);

    m_highlighter = new EditorHighlighter(m_editor->document());

    connect(newButton, &QPushButton::clicked, this, &MainWindow::createDocument);
    connect(renameButton, &QPushButton::clicked, this, &MainWindow::renameDocument);
    connect(trashButton, &QPushButton::clicked, this, &MainWindow::moveDocumentToTrash);
    connect(m_fileList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        openDocument(item->data(Qt::UserRole).toString());
        m_sidebar->hide();
        m_editor->setFocus();
    });
    connect(m_editor, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this,
            &MainWindow::centerCurrentLine);
}

void MainWindow::setupActions()
{
    const auto addAction = [this](const QString& label, const QKeySequence& shortcut,
                                  const std::function<void()>& callback) {
        auto* action = new QAction(label, this);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, callback);
        QMainWindow::addAction(action);
        return action;
    };

    addAction(QStringLiteral("Список файлов"), QKeySequence(QStringLiteral("Ctrl+O")),
              [this] { toggleSidebar(); });
    addAction(QStringLiteral("Новый текст"), QKeySequence::New,
              [this] { createDocument(); });
    m_saveAction = addAction(QStringLiteral("Сохранить"), QKeySequence::Save,
                             [this] { saveCurrentDocument(); });
    addAction(QStringLiteral("Выбрать папку"), QKeySequence(QStringLiteral("Ctrl+Shift+O")),
              [this] { chooseWorkspace(); });
    m_findAction = addAction(QStringLiteral("Поиск и замена"), QKeySequence::Find,
                             [this] { showFindReplace(); });
    addAction(QStringLiteral("Настройки"), QKeySequence(QStringLiteral("Ctrl+,")),
              [this] { showPreferences(); });
    addAction(QStringLiteral("Таймер"), QKeySequence(QStringLiteral("Ctrl+T")),
              [this] { startWritingTimer(); });
    addAction(QStringLiteral("Полный экран"), QKeySequence(Qt::Key_F11),
              [this] { toggleFullScreen(); });
    addAction(QStringLiteral("Matrix-тема"), QKeySequence(QStringLiteral("Ctrl+Alt+M")),
              [this] {
                  const QString current = m_settings.value(
                      QStringLiteral("appearance/theme"), QStringLiteral("matrix")).toString();
                  if (current == QStringLiteral("matrix")) {
                      m_settings.setValue(
                          QStringLiteral("appearance/theme"),
                          m_settings.value(QStringLiteral("appearance/themeBeforeMatrix"),
                                           QStringLiteral("dark")));
                  } else {
                      m_settings.setValue(QStringLiteral("appearance/themeBeforeMatrix"), current);
                      m_settings.setValue(QStringLiteral("appearance/theme"),
                                          QStringLiteral("matrix"));
                  }
                  applyAppearance();
              });
    m_quitAction = addAction(QStringLiteral("Выход"), QKeySequence::Quit,
                             [this] { close(); });
    addAction(QStringLiteral("Выключить Raspberry Pi"),
              QKeySequence(QStringLiteral("Ctrl+Shift+Q")),
              [this] { requestPowerOff(); });

    m_typewriterAction = addAction(
        QStringLiteral("Режим печатной машинки"),
        QKeySequence(QStringLiteral("Ctrl+Alt+T")),
        [this] {
            const bool enabled = !m_settings.value(QStringLiteral("editor/typewriter"), true).toBool();
            m_settings.setValue(QStringLiteral("editor/typewriter"), enabled);
            m_typewriterAction->setChecked(enabled);
            centerCurrentLine();
        });
    m_typewriterAction->setCheckable(true);

    m_spellCheckAction = addAction(
        QStringLiteral("Проверка орфографии"),
        QKeySequence(QStringLiteral("Ctrl+Alt+S")),
        [this] {
            const bool enabled = !m_settings.value(QStringLiteral("editor/spellCheck"), true).toBool();
            m_settings.setValue(QStringLiteral("editor/spellCheck"), enabled);
            m_spellCheckAction->setChecked(enabled);
            m_highlighter->setSpellCheckEnabled(enabled);
        });
    m_spellCheckAction->setCheckable(true);

    applyConfiguredShortcuts();
}

void MainWindow::applyConfiguredShortcuts()
{
    const auto& defaults = configurableShortcutDefaults();
    m_saveAction->setShortcut(configuredShortcut(
        m_settings, defaults[0].first, defaults[0].second));
    m_findAction->setShortcut(configuredShortcut(
        m_settings, defaults[1].first, defaults[1].second));
    m_quitAction->setShortcut(configuredShortcut(
        m_settings, defaults[2].first, defaults[2].second));
}

void MainWindow::showShortcutPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Горячие клавиши"));
    dialog.setMinimumWidth(520);
    auto* rootLayout = new QVBoxLayout(&dialog);

    auto* introduction = new QLabel(
        QStringLiteral("Нажмите новое сочетание в нужном поле. Изменения сохраняются "
                       "между запусками Zen Writer."),
        &dialog);
    introduction->setWordWrap(true);
    rootLayout->addWidget(introduction);

    auto* form = new QFormLayout();
    auto* saveEdit = new QKeySequenceEdit(m_saveAction->shortcut(), &dialog);
    auto* findEdit = new QKeySequenceEdit(m_findAction->shortcut(), &dialog);
    auto* quitEdit = new QKeySequenceEdit(m_quitAction->shortcut(), &dialog);
    form->addRow(QStringLiteral("Сохранить:"), saveEdit);
    form->addRow(QStringLiteral("Поиск и замена:"), findEdit);
    form->addRow(QStringLiteral("Выйти:"), quitEdit);
    rootLayout->addLayout(form);

    QStringList fixedLines;
    for (const auto& [label, shortcut] : fixedShortcutBindings()) {
        fixedLines.append(QStringLiteral("%1 — %2")
                              .arg(shortcut.toString(QKeySequence::NativeText), label));
    }
    auto* fixedShortcuts = new QLabel(
        QStringLiteral("Остальные сочетания фиксированы:\n%1").arg(fixedLines.join(QStringLiteral(" · "))),
        &dialog);
    fixedShortcuts->setWordWrap(true);
    fixedShortcuts->setStyleSheet(QStringLiteral("color: palette(mid);"));
    rootLayout->addWidget(fixedShortcuts);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel
            | QDialogButtonBox::RestoreDefaults,
        &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    buttons->button(QDialogButtonBox::RestoreDefaults)
        ->setText(QStringLiteral("По умолчанию"));
    rootLayout->addWidget(buttons);

    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            &dialog, [saveEdit, findEdit, quitEdit] {
                const auto& defaults = configurableShortcutDefaults();
                saveEdit->setKeySequence(defaults[0].second);
                findEdit->setKeySequence(defaults[1].second);
                quitEdit->setKeySequence(defaults[2].second);
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [this, &dialog, saveEdit, findEdit, quitEdit] {
                const std::array<QString, 3> labels = {
                    QStringLiteral("Сохранить"),
                    QStringLiteral("Поиск и замена"),
                    QStringLiteral("Выйти"),
                };
                const std::array<QKeySequence, 3> sequences = {
                    saveEdit->keySequence(),
                    findEdit->keySequence(),
                    quitEdit->keySequence(),
                };

                for (std::size_t index = 0; index < sequences.size(); ++index) {
                    if (!isSafeEditorShortcut(sequences[index])) {
                        QMessageBox::warning(
                            &dialog, QStringLiteral("Горячие клавиши"),
                            QStringLiteral("Для «%1» задайте одно сочетание с Ctrl, Alt "
                                           "или Meta либо клавишу F1–F35.")
                                .arg(labels[index]));
                        return;
                    }
                    for (std::size_t other = 0; other < index; ++other) {
                        if (sequences[index] == sequences[other]) {
                            QMessageBox::warning(
                                &dialog, QStringLiteral("Горячие клавиши"),
                                QStringLiteral("«%1» и «%2» не могут использовать %3 одновременно.")
                                    .arg(labels[other], labels[index],
                                         sequences[index].toString(QKeySequence::NativeText)));
                            return;
                        }
                    }
                    for (const auto& [fixedLabel, fixedShortcut] : fixedShortcutBindings()) {
                        if (sequences[index] == fixedShortcut) {
                            QMessageBox::warning(
                                &dialog, QStringLiteral("Горячие клавиши"),
                                QStringLiteral("%1 уже используется для действия «%2».")
                                    .arg(sequences[index].toString(QKeySequence::NativeText),
                                         fixedLabel));
                            return;
                        }
                    }
                }

                const auto& defaults = configurableShortcutDefaults();
                for (std::size_t index = 0; index < sequences.size(); ++index) {
                    m_settings.setValue(
                        defaults[index].first,
                        sequences[index].toString(QKeySequence::PortableText));
                }
                m_settings.sync();
                applyConfiguredShortcuts();
                statusBar()->showMessage(QStringLiteral("Горячие клавиши обновлены"), 2500);
                dialog.accept();
            });

    dialog.exec();
}

void MainWindow::ensureWorkspace()
{
    QString path = m_settings.value(QStringLiteral("files/workspace")).toString();
    if (path.isEmpty()) {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        path = QDir(documents.isEmpty() ? QDir::homePath() : documents)
                   .filePath(QStringLiteral("Zen Writer"));
        m_settings.setValue(QStringLiteral("files/workspace"), path);
    }
    QDir().mkpath(path);
    refreshFileList();
}

QString MainWindow::workspaceDirectory() const
{
    return m_settings.value(QStringLiteral("files/workspace")).toString();
}

QString MainWindow::selectedDocumentPath() const
{
    if (const QListWidgetItem* item = m_fileList->currentItem()) {
        return item->data(Qt::UserRole).toString();
    }
    return m_currentPath;
}

QString MainWindow::documentKey(const QString& path) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}

void MainWindow::refreshFileList()
{
    const QString selected = selectedDocumentPath();
    m_fileList->clear();

    QDir directory(workspaceDirectory());
    directory.setNameFilters({QStringLiteral("*.md"), QStringLiteral("*.txt"),
                              QStringLiteral("*.MD"), QStringLiteral("*.TXT")});
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::Readable,
                                                        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& file : files) {
        auto* item = new QListWidgetItem(file.fileName(), m_fileList);
        item->setData(Qt::UserRole, file.absoluteFilePath());
        if (file.absoluteFilePath() == selected) {
            m_fileList->setCurrentItem(item);
        }
    }
}

void MainWindow::toggleSidebar()
{
    const bool show = !m_sidebar->isVisible();
    m_sidebar->setVisible(show);
    if (show) {
        refreshFileList();
        m_fileList->setFocus();
    } else {
        m_editor->setFocus();
    }
}

void MainWindow::chooseWorkspace()
{
    const QString selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Папка с текстами"), workspaceDirectory());
    if (selected.isEmpty() || selected == workspaceDirectory()) {
        return;
    }

    if (!saveCurrentDocument(true) && m_dirty) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось сохранить текущий текст."));
        return;
    }
    m_settings.setValue(QStringLiteral("files/workspace"), selected);
    m_currentPath.clear();
    m_editor->clear();
    refreshFileList();
    restoreSession();
}

void MainWindow::createDocument()
{
    bool accepted = false;
    const QString requested = QInputDialog::getText(
        this, QStringLiteral("Новый текст"), QStringLiteral("Название файла:"),
        QLineEdit::Normal, QString(), &accepted);
    if (!accepted) {
        return;
    }

    const QString name = ZenWriter::normalizedDocumentName(requested);
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Zen Writer"),
                             QStringLiteral("Введите корректное название файла."));
        return;
    }

    const QString path = QDir(workspaceDirectory()).filePath(name);
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Zen Writer"),
                             QStringLiteral("Файл с таким названием уже существует."));
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || !file.commit()) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось создать файл."));
        return;
    }
    refreshFileList();
    openDocument(path);
    m_sidebar->hide();
    m_editor->setFocus();
}

void MainWindow::renameDocument()
{
    const QString oldPath = selectedDocumentPath();
    if (oldPath.isEmpty()) {
        return;
    }

    bool accepted = false;
    const QString requested = QInputDialog::getText(
        this, QStringLiteral("Переименовать"), QStringLiteral("Новое название:"),
        QLineEdit::Normal, QFileInfo(oldPath).fileName(), &accepted);
    if (!accepted) {
        return;
    }

    const QString name = ZenWriter::normalizedDocumentName(requested);
    const QString newPath = QDir(workspaceDirectory()).filePath(name);
    if (name.isEmpty() || (QFileInfo::exists(newPath) && newPath != oldPath)) {
        QMessageBox::warning(this, QStringLiteral("Zen Writer"),
                             QStringLiteral("Такое название использовать нельзя."));
        return;
    }
    if (newPath == oldPath) {
        return;
    }

    if (oldPath == m_currentPath) {
        if (!saveCurrentDocument(true)) {
            QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                                  QStringLiteral("Не удалось сохранить текущий текст."));
            return;
        }
    }
    if (!QFile::rename(oldPath, newPath)) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось переименовать файл."));
        return;
    }

    if (oldPath == m_currentPath) {
        m_currentPath = newPath;
        m_settings.setValue(QStringLiteral("session/lastFile"), newPath);
    }
    refreshFileList();
    updateStatistics();
}

void MainWindow::moveDocumentToTrash()
{
    const QString path = selectedDocumentPath();
    if (path.isEmpty()) {
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("В корзину"),
        QStringLiteral("Переместить «%1» в корзину?").arg(QFileInfo(path).fileName()));
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (path == m_currentPath) {
        if (!saveCurrentDocument(true)) {
            QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                                  QStringLiteral("Не удалось сохранить текущий текст."));
            return;
        }
    }
    if (!QFile::moveToTrash(path)) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Система не смогла переместить файл в корзину."));
        return;
    }

    if (path == m_currentPath) {
        m_currentPath.clear();
        m_editor->clear();
    }
    refreshFileList();
    restoreSession();
}

bool MainWindow::openDocument(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !ZenWriter::isSupportedDocument(path)) {
        return false;
    }
    if (info.absoluteFilePath() == m_currentPath) {
        return true;
    }

    if (!m_currentPath.isEmpty() && !saveCurrentDocument(true)) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось сохранить текущий текст."));
        return false;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось открыть файл."));
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString content = stream.readAll();

    m_loading = true;
    m_editor->setPlainText(content);
    m_currentPath = info.absoluteFilePath();
    const int savedCursor = m_settings
                                .value(QStringLiteral("session/cursors/") + documentKey(m_currentPath), 0)
                                .toInt();
    QTextCursor cursor = m_editor->textCursor();
    cursor.setPosition(std::clamp(savedCursor, 0, static_cast<int>(content.size())));
    m_editor->setTextCursor(cursor);
    m_loading = false;
    m_dirty = false;
    m_lastBackupAt = {};
    m_editor->document()->setModified(false);

    m_settings.setValue(QStringLiteral("session/lastFile"), m_currentPath);
    m_documentLabel->setText(info.fileName());
    setWindowTitle(QStringLiteral("%1 — Zen Writer").arg(info.completeBaseName()));
    ensureDailyBaseline(ZenWriter::wordCount(content));
    refreshFileList();
    updateStatistics();
    QTimer::singleShot(0, this, &MainWindow::centerCurrentLine);
    return true;
}

bool MainWindow::saveCurrentDocument(bool forceBackup)
{
    if (m_currentPath.isEmpty()) {
        return false;
    }
    if (!m_dirty && !forceBackup) {
        saveCursorPosition();
        return true;
    }

    createBackup(forceBackup);
    QSaveFile file(m_currentPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage(QStringLiteral("Ошибка автосохранения"), 5000);
        return false;
    }
    file.write(m_editor->toPlainText().toUtf8());
    if (!file.commit()) {
        statusBar()->showMessage(QStringLiteral("Ошибка автосохранения"), 5000);
        return false;
    }

    m_dirty = false;
    m_editor->document()->setModified(false);
    const int words = ZenWriter::wordCount(m_editor->toPlainText());
    persistDailyProgress(words);
    saveCursorPosition();
    m_settings.sync();
    updateStatistics(words);
    statusBar()->showMessage(QStringLiteral("Сохранено"), 1200);
    return true;
}

void MainWindow::createBackup(bool force)
{
    if (m_currentPath.isEmpty() || !QFileInfo::exists(m_currentPath)) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!force && m_lastBackupAt.isValid()
        && m_lastBackupAt.secsTo(now) < BackupIntervalSeconds) {
        return;
    }

    const QFileInfo source(m_currentPath);
    const QString backupRoot = QDir(workspaceDirectory())
                                   .filePath(QStringLiteral(".zen-writer-backups/" )
                                             + documentKey(m_currentPath));
    QDir().mkpath(backupRoot);
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString backupName = QStringLiteral("%1-%2.%3")
                                   .arg(source.completeBaseName(), stamp, source.suffix());
    if (QFile::copy(m_currentPath, QDir(backupRoot).filePath(backupName))) {
        m_lastBackupAt = now;
    }

    QDir backupDirectory(backupRoot);
    const QFileInfoList backups = backupDirectory.entryInfoList(
        QDir::Files, QDir::Time);
    for (int index = BackupsToKeep; index < backups.size(); ++index) {
        QFile::remove(backups.at(index).absoluteFilePath());
    }
}

void MainWindow::saveCursorPosition()
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    m_settings.setValue(QStringLiteral("session/cursors/") + documentKey(m_currentPath),
                        m_editor->textCursor().position());
}

void MainWindow::onTextChanged()
{
    if (m_loading || m_currentPath.isEmpty()) {
        return;
    }
    m_dirty = true;
    m_saveTimer->start();
    updateStatistics(ZenWriter::wordCount(m_editor->toPlainText()));
}

void MainWindow::centerCurrentLine()
{
    if (m_loading || !m_settings.value(QStringLiteral("editor/typewriter"), true).toBool()) {
        return;
    }

    const QRect cursorRect = m_editor->cursorRect();
    QScrollBar* scroll = m_editor->verticalScrollBar();
    const int offset = cursorRect.center().y() - (m_editor->viewport()->height() / 2);
    scroll->setValue(scroll->value() + offset);
}

void MainWindow::updateStatistics(int currentWords)
{
    if (currentWords < 0) {
        currentWords = ZenWriter::wordCount(m_editor->toPlainText());
    }
    const int characters = m_editor->toPlainText().size();
    m_statisticsLabel->setText(
        QStringLiteral("Слов: %1 · Знаков: %2").arg(currentWords).arg(characters));

    const int goal = m_settings.value(QStringLiteral("progress/dailyGoal"), 500).toInt();
    m_goalLabel->setText(
        QStringLiteral("Сегодня: %1/%2").arg(dailyWordCount(currentWords)).arg(goal));
    if (!m_currentPath.isEmpty()) {
        m_documentLabel->setText(QFileInfo(m_currentPath).fileName());
    }
}

void MainWindow::ensureDailyBaseline(int currentWords)
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    const QString group = QStringLiteral("progress/days/%1/%2")
                              .arg(QDate::currentDate().toString(Qt::ISODate),
                                   documentKey(m_currentPath));
    m_settings.beginGroup(group);
    if (!m_settings.contains(QStringLiteral("baseline"))) {
        m_settings.setValue(QStringLiteral("baseline"), currentWords);
        m_settings.setValue(QStringLiteral("current"), currentWords);
    }
    m_settings.endGroup();
}

void MainWindow::persistDailyProgress(int currentWords)
{
    ensureDailyBaseline(currentWords);
    const QString group = QStringLiteral("progress/days/%1/%2")
                              .arg(QDate::currentDate().toString(Qt::ISODate),
                                   documentKey(m_currentPath));
    m_settings.beginGroup(group);
    m_settings.setValue(QStringLiteral("current"), currentWords);
    m_settings.endGroup();
}

int MainWindow::dailyWordCount(int currentWords) const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("progress/days/")
                        + QDate::currentDate().toString(Qt::ISODate));
    int total = 0;
    const QString currentKey = m_currentPath.isEmpty() ? QString() : documentKey(m_currentPath);
    for (const QString& key : settings.childGroups()) {
        settings.beginGroup(key);
        const int baseline = settings.value(QStringLiteral("baseline"), 0).toInt();
        const int value = key == currentKey
                              ? currentWords
                              : settings.value(QStringLiteral("current"), baseline).toInt();
        total += std::max(0, value - baseline);
        settings.endGroup();
    }
    settings.endGroup();
    return total;
}

void MainWindow::showFindReplace()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Поиск и замена"));
    auto* layout = new QFormLayout(&dialog);
    auto* findEdit = new QLineEdit(&dialog);
    auto* replaceEdit = new QLineEdit(&dialog);
    auto* caseSensitive = new QCheckBox(QStringLiteral("Учитывать регистр"), &dialog);
    layout->addRow(QStringLiteral("Найти:"), findEdit);
    layout->addRow(QStringLiteral("Заменить:"), replaceEdit);
    layout->addRow(caseSensitive);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* nextButton = buttons->addButton(QStringLiteral("Далее"), QDialogButtonBox::ActionRole);
    auto* replaceButton = buttons->addButton(QStringLiteral("Заменить"), QDialogButtonBox::ActionRole);
    auto* allButton = buttons->addButton(QStringLiteral("Заменить всё"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addRow(buttons);

    const auto flags = [caseSensitive] {
        return caseSensitive->isChecked() ? QTextDocument::FindCaseSensitively
                                          : QTextDocument::FindFlags();
    };
    const auto findNext = [this, findEdit, flags] {
        if (findEdit->text().isEmpty()) {
            return;
        }
        if (!m_editor->find(findEdit->text(), flags())) {
            QTextCursor cursor = m_editor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            m_editor->setTextCursor(cursor);
            m_editor->find(findEdit->text(), flags());
        }
    };
    connect(nextButton, &QPushButton::clicked, &dialog, findNext);
    connect(replaceButton, &QPushButton::clicked, &dialog,
            [this, replaceEdit, findNext] {
                QTextCursor cursor = m_editor->textCursor();
                if (cursor.hasSelection()) {
                    cursor.insertText(replaceEdit->text());
                }
                findNext();
            });
    connect(allButton, &QPushButton::clicked, &dialog,
            [this, findEdit, replaceEdit, flags] {
                if (findEdit->text().isEmpty()) {
                    return;
                }
                QTextCursor editCursor(m_editor->document());
                editCursor.beginEditBlock();
                QTextCursor match = m_editor->document()->find(findEdit->text(), 0, flags());
                int replacements = 0;
                while (!match.isNull()) {
                    match.insertText(replaceEdit->text());
                    ++replacements;
                    match = m_editor->document()->find(findEdit->text(), match, flags());
                }
                editCursor.endEditBlock();
                statusBar()->showMessage(
                    QStringLiteral("Заменено: %1").arg(replacements), 3000);
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    findEdit->setFocus();
    dialog.exec();
}

void MainWindow::showPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Настройки Zen Writer"));
    auto* layout = new QFormLayout(&dialog);

    auto* theme = new QComboBox(&dialog);
    theme->addItem(QStringLiteral("Матрица"), QStringLiteral("matrix"));
    theme->addItem(QStringLiteral("Тёмная"), QStringLiteral("dark"));
    theme->addItem(QStringLiteral("Светлая"), QStringLiteral("light"));
    theme->addItem(QStringLiteral("Сепия"), QStringLiteral("sepia"));
    theme->setCurrentIndex(std::max(
        0, theme->findData(m_settings.value(QStringLiteral("appearance/theme"),
                                             QStringLiteral("matrix")))));

    auto* font = new QFontComboBox(&dialog);
    QString defaultFont = QStringLiteral("Noto Serif");
    if (theme->currentData() == QStringLiteral("matrix")) {
        defaultFont = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }
    font->setCurrentFont(QFont(
        m_settings.value(QStringLiteral("appearance/font"), defaultFont).toString()));
    auto* fontSize = new QSpinBox(&dialog);
    fontSize->setRange(10, 48);
    fontSize->setValue(m_settings.value(QStringLiteral("appearance/fontSize"), 18).toInt());
    auto* editorWidth = new QSpinBox(&dialog);
    editorWidth->setRange(420, 1800);
    editorWidth->setSuffix(QStringLiteral(" px"));
    editorWidth->setValue(m_settings.value(QStringLiteral("appearance/editorWidth"), 920).toInt());
    auto* dailyGoal = new QSpinBox(&dialog);
    dailyGoal->setRange(0, 100000);
    dailyGoal->setSuffix(QStringLiteral(" слов"));
    dailyGoal->setValue(m_settings.value(QStringLiteral("progress/dailyGoal"), 500).toInt());
    auto* typewriter = new QCheckBox(QStringLiteral("Держать текущую строку по центру"), &dialog);
    typewriter->setChecked(m_settings.value(QStringLiteral("editor/typewriter"), true).toBool());
    auto* spellCheck = new QCheckBox(QStringLiteral("Русский и английский"), &dialog);
    spellCheck->setChecked(m_settings.value(QStringLiteral("editor/spellCheck"), true).toBool());
    auto* shortcutButton = new QPushButton(QStringLiteral("Настроить…"), &dialog);
    connect(shortcutButton, &QPushButton::clicked, &dialog,
            [this] { showShortcutPreferences(); });

    auto* backgroundRow = new QWidget(&dialog);
    auto* backgroundLayout = new QHBoxLayout(backgroundRow);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    auto* background = new QLineEdit(
        m_settings.value(QStringLiteral("appearance/background")).toString(), backgroundRow);
    auto* browse = new QPushButton(QStringLiteral("…"), backgroundRow);
    backgroundLayout->addWidget(background, 1);
    backgroundLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [this, background] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Фоновое изображение"), background->text(),
            QStringLiteral("Изображения (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!path.isEmpty()) {
            background->setText(path);
        }
    });

    layout->addRow(QStringLiteral("Тема:"), theme);
    layout->addRow(QStringLiteral("Шрифт:"), font);
    layout->addRow(QStringLiteral("Размер:"), fontSize);
    layout->addRow(QStringLiteral("Ширина текста:"), editorWidth);
    layout->addRow(QStringLiteral("Дневная цель:"), dailyGoal);
    layout->addRow(QStringLiteral("Фон:"), backgroundRow);
    layout->addRow(QStringLiteral("Печатная машинка:"), typewriter);
    layout->addRow(QStringLiteral("Орфография:"), spellCheck);
    layout->addRow(QStringLiteral("Горячие клавиши:"), shortcutButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_settings.setValue(QStringLiteral("appearance/theme"), theme->currentData());
    m_settings.setValue(QStringLiteral("appearance/font"), font->currentFont().family());
    m_settings.setValue(QStringLiteral("appearance/fontSize"), fontSize->value());
    m_settings.setValue(QStringLiteral("appearance/editorWidth"), editorWidth->value());
    m_settings.setValue(QStringLiteral("appearance/background"), background->text());
    m_settings.setValue(QStringLiteral("progress/dailyGoal"), dailyGoal->value());
    m_settings.setValue(QStringLiteral("editor/typewriter"), typewriter->isChecked());
    m_settings.setValue(QStringLiteral("editor/spellCheck"), spellCheck->isChecked());
    applyAppearance();
    updateStatistics();
}

void MainWindow::applyAppearance()
{
    const QString theme = m_settings.value(QStringLiteral("appearance/theme"),
                                           QStringLiteral("matrix")).toString();
    const bool matrix = theme == QStringLiteral("matrix");
    const bool dark = matrix || theme == QStringLiteral("dark");
    QColor windowColor;
    QColor editorColor;
    QColor textColor;
    QColor mutedColor;
    QString selectionColor;
    QString borderColor;
    if (matrix) {
        windowColor = QColor(QStringLiteral("#010402"));
        editorColor = QColor(0, 10, 3, 224);
        textColor = QColor(QStringLiteral("#8cff9a"));
        mutedColor = QColor(QStringLiteral("#38d760"));
        selectionColor = QStringLiteral("#135c2b");
        borderColor = QStringLiteral("#1f8f43");
    } else if (theme == QStringLiteral("light")) {
        windowColor = QColor(QStringLiteral("#e9ecef"));
        editorColor = QColor(255, 255, 255, 238);
        textColor = QColor(QStringLiteral("#22252a"));
        mutedColor = QColor(QStringLiteral("#5f6670"));
        selectionColor = QStringLiteral("#9fc7e8");
        borderColor = QStringLiteral("transparent");
    } else if (theme == QStringLiteral("sepia")) {
        windowColor = QColor(QStringLiteral("#cbbf9f"));
        editorColor = QColor(246, 238, 215, 238);
        textColor = QColor(QStringLiteral("#3d3527"));
        mutedColor = QColor(QStringLiteral("#6f6049"));
        selectionColor = QStringLiteral("#b9a878");
        borderColor = QStringLiteral("transparent");
    } else {
        windowColor = QColor(QStringLiteral("#16191d"));
        editorColor = QColor(23, 27, 32, 224);
        textColor = QColor(QStringLiteral("#e6e8eb"));
        mutedColor = QColor(QStringLiteral("#a7adb5"));
        selectionColor = QStringLiteral("#4f6f8f");
        borderColor = QStringLiteral("transparent");
    }

    const QString backgroundPath =
        m_settings.value(QStringLiteral("appearance/background")).toString();
    m_background->setBackground(matrix ? QString() : backgroundPath, windowColor);
    m_background->setMatrixEnabled(matrix);

    QString fontFamily = m_settings.value(QStringLiteral("appearance/font")).toString();
    if (fontFamily.isEmpty()) {
        fontFamily = matrix
            ? QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
            : QStringLiteral("Noto Serif");
    }
    QFont editorFont(fontFamily);
    editorFont.setPointSize(m_settings.value(QStringLiteral("appearance/fontSize"), 18).toInt());
    editorFont.setStyleHint(matrix ? QFont::Monospace : QFont::AnyStyle);
    m_editor->setFont(editorFont);
    m_editor->setMaximumWidth(
        m_settings.value(QStringLiteral("appearance/editorWidth"), 920).toInt());
    QString centralStyle = QStringLiteral(
        "QPlainTextEdit#writingEditor { background-color: rgba(%1,%2,%3,%4); "
        "color: %5; selection-background-color: %6; border: 1px solid %7; "
        "border-radius: 5px; padding: 36px; }"
        "QWidget#fileSidebar { background-color: rgba(%1,%2,%3,235); color: %5; "
        "border: 1px solid %7; border-radius: 5px; }"
        "QListWidget { background: transparent; color: %5; border: none; }"
        "QListWidget::item:selected { background: %6; }"
        "QPushButton { padding: 5px; }")
                               .arg(editorColor.red())
                               .arg(editorColor.green())
                               .arg(editorColor.blue())
                               .arg(editorColor.alpha())
                               .arg(textColor.name(), selectionColor, borderColor);
    if (matrix) {
        centralStyle += QStringLiteral(
            "QPushButton { color: #74ff8c; background: #061109; "
            "border: 1px solid #1f8f43; border-radius: 3px; }"
            "QPushButton:hover { background: #0d2814; }"
            "QScrollBar { background: #020703; }"
            "QScrollBar::handle { background: #17652f; }");
    }
    m_background->setStyleSheet(centralStyle);
    statusBar()->setStyleSheet(
        matrix
            ? QStringLiteral("color: %1; background: #010402; font-family: monospace; "
                             "letter-spacing: 1px;").arg(mutedColor.name())
            : QStringLiteral("color: %1;").arg(mutedColor.name()));
    setWindowTitle(matrix ? QStringLiteral("ZEN WRITER // MATRIX MODE")
                          : QStringLiteral("Zen Writer"));
    m_editor->setPlaceholderText(matrix ? QStringLiteral("> НАЧНИТЕ ВВОД…")
                                        : QStringLiteral("Начните писать…"));

    const bool spell = m_settings.value(QStringLiteral("editor/spellCheck"), true).toBool();
    const bool typewriter = m_settings.value(QStringLiteral("editor/typewriter"), true).toBool();
    m_highlighter->setDarkTheme(dark);
    m_highlighter->setMatrixTheme(matrix);
    m_highlighter->setSpellCheckEnabled(spell);
    m_spellCheckAction->setChecked(spell);
    m_typewriterAction->setChecked(typewriter);
}

void MainWindow::startWritingTimer()
{
    bool accepted = false;
    const int minutes = QInputDialog::getInt(
        this, QStringLiteral("Таймер"), QStringLiteral("Минуты:"),
        m_timerRemainingSeconds > 0 ? std::max(1, m_timerRemainingSeconds / 60) : 25,
        1, 600, 1, &accepted);
    if (!accepted) {
        return;
    }
    m_timerRemainingSeconds = minutes * 60;
    m_timerLabel->setText(readableTime(m_timerRemainingSeconds));
    m_writingTimer->start();
}

void MainWindow::updateWritingTimer()
{
    if (m_timerRemainingSeconds > 0) {
        --m_timerRemainingSeconds;
    }
    m_timerLabel->setText(readableTime(m_timerRemainingSeconds));
    if (m_timerRemainingSeconds > 0) {
        return;
    }

    m_writingTimer->stop();
    QApplication::beep();
    QMessageBox::information(this, QStringLiteral("Zen Writer"),
                             QStringLiteral("Время закончилось."));
}

void MainWindow::requestPowerOff()
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Выключение"),
        QStringLiteral("Сохранить текст и безопасно выключить Raspberry Pi?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!saveCurrentDocument(true)) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось сохранить текст. Выключение отменено."));
        return;
    }
    m_settings.sync();
#ifdef Q_OS_WIN
    const bool started = QProcess::startDetached(
        QStringLiteral("shutdown.exe"),
        {QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0")});
#else
    const bool started = QProcess::startDetached(
        QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
#endif
    if (!started) {
        QMessageBox::critical(this, QStringLiteral("Zen Writer"),
                              QStringLiteral("Не удалось запустить безопасное выключение."));
    }
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::openInitialFile(const QString& path)
{
    const QFileInfo info(path);
    if (info.exists() && info.isFile() && ZenWriter::isSupportedDocument(path)) {
        m_settings.setValue(QStringLiteral("files/workspace"), info.absolutePath());
        ensureWorkspace();
        openDocument(info.absoluteFilePath());
        return;
    }
    restoreSession();
}

void MainWindow::restoreSession()
{
    ensureWorkspace();
    const QString lastFile = m_settings.value(QStringLiteral("session/lastFile")).toString();
    if (QFileInfo::exists(lastFile)
        && QFileInfo(lastFile).absolutePath() == QFileInfo(workspaceDirectory()).absoluteFilePath()
        && openDocument(lastFile)) {
        return;
    }

    if (m_fileList->count() > 0) {
        openDocument(m_fileList->item(0)->data(Qt::UserRole).toString());
        return;
    }

    const QString firstPath = QDir(workspaceDirectory()).filePath(QStringLiteral("Первый текст.md"));
    QSaveFile file(firstPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.commit();
        refreshFileList();
        openDocument(firstPath);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!saveCurrentDocument(true) && m_dirty) {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Zen Writer"),
            QStringLiteral("Текст не удалось сохранить. Всё равно выйти?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    saveCursorPosition();
    m_settings.sync();
    event->accept();
}
