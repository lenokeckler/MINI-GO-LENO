#include "Editor.h"
#include "CodeEditor.h"
#include "highlight/MiniGoHighlighter.h"
#include "highlight/IRHighlighter.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QListWidget>
#include <QTextBlock>
#include <QScrollBar>
#include <QFile>
#include <QTextStream>
#include <QTemporaryFile>
#include <QDir>
#include <QRegularExpression>
#include <QFontMetrics>
#include <QTextCursor>
#include <QStandardPaths>
#include <QFileInfo>
#include <QShortcut>
#include <QClipboard>
#include <QInputDialog>
#include <QToolBar>
#include <QToolButton>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QSettings>
#include <QMimeData>
#include <QUrl>
#include <QTimer>

// Compilador pipeline (incluimos aquí para integrarlo con Qt)
// Qt define 'emit' como macro vacío; lo quitamos antes de incluir ANTLR4
// porque Lexer.h tiene un método llamado emit() que entraría en conflicto.
#undef emit
#include "antlr4-runtime.h"
#include "MiniGoLexer.h"
#include "MiniGoParser.h"
#include "typechecker/TypeChecker.h"
#include "encoder/Encoder.h"
#include "errors/MiniGoErrorListener.h"

using namespace std;

// ============================================================
// Hojas de estilo derivadas del tema activo.
// Centralizadas para que setupUI (al construir) y applyTheme (al
// cambiar de tema) usen exactamente las mismas reglas.
// ============================================================
namespace {

const char* kMono = "'Cascadia Code','Consolas',monospace";

QString qssWindow() {
    return QString(
        "QMainWindow, QWidget { background:%1; }"
        "QMenuBar { background:%2; color:%3; }"
        "QMenuBar::item { padding:4px 10px; background:transparent; }"
        "QMenuBar::item:selected { background:%4; color:%5; }"
        "QMenu { background:%2; color:%3; border:1px solid %6; }"
        "QMenu::item:selected { background:%7; color:%5; }"
        "QMenu::separator { background:%6; height:1px; margin:4px 8px; }")
        .arg(Theme::hex(Theme::BgDeep),  Theme::hex(Theme::BgPanel),
             Theme::hex(Theme::TextSec), Theme::hex(Theme::BgHover),
             Theme::hex(Theme::TextPri), Theme::hex(Theme::Border),
             Theme::hex(Theme::MenuSel));
}

QString qssToolbar() {
    return QString(
        "QToolBar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "           stop:0 %1, stop:1 %2);"
        "           border-bottom:1px solid %3; spacing:6px; padding:4px; }"
        "QToolButton { color:%4; padding:4px 10px; border-radius:6px; font-weight:600; }"
        "QToolButton:hover { background: rgba(127,127,127,0.18); }"
        "QToolButton:pressed { background: rgba(0,0,0,0.20); }"
        "QToolBar::separator { background:%3; width:1px; margin:4px 6px; }")
        .arg(Theme::hex(Theme::BgSurface), Theme::hex(Theme::BgPanel),
             Theme::hex(Theme::Border),    Theme::hex(Theme::TextPri));
}

QString qssSplitter() {
    return QString("QSplitter::handle { background:%1; }"
                   "QSplitter::handle:hover { background:%2; }")
        .arg(Theme::hex(Theme::Border), Theme::hex(Theme::Go));
}

QString qssTabs() {
    return QString(
        "QTabWidget::pane { border:none; border-top:1px solid %1; background:%2; }"
        "QTabBar { background:%3; }"
        "QTabBar::tab { background:transparent; color:%4; padding:6px 16px;"
        "               font-family:%7; font-size:12px;"
        "               border-bottom:2px solid transparent; }"
        "QTabBar::tab:hover { color:%5; }"
        "QTabBar::tab:selected { color:%6; border-bottom:2px solid %6; }")
        .arg(Theme::hex(Theme::Border), Theme::hex(Theme::BgPanel),
             Theme::hex(Theme::BgSurface), Theme::hex(Theme::TextSec),
             Theme::hex(Theme::TextPri),  Theme::hex(Theme::Go),
             QString(kMono));
}

QString qssOutBtn() {
    return QString(
        "QPushButton { background:%1; color:%2; border:1px solid %3;"
        "              padding:2px 10px; border-radius:5px; font-size:11px; margin:2px; }"
        "QPushButton:hover { background:%4; color:%5; border-color:%5; }")
        .arg(Theme::hex(Theme::BgSurface), Theme::hex(Theme::TextSec),
             Theme::hex(Theme::Border),    Theme::hex(Theme::BgHover),
             Theme::hex(Theme::Go));
}

QString qssErrorList() {
    return QString(
        "QListWidget { background:%1; color:%2;"
        "              font-family:%4; font-size:11px;"
        "              border:none; outline:0; }"
        "QListWidget::item { padding:2px 4px; }"
        "QListWidget::item:selected { background:%3; }")
        .arg(Theme::hex(Theme::BgPanel), Theme::hex(Theme::TextPri),
             Theme::hex(Theme::BgHover), QString(kMono));
}

QString qssProgramOutput() {
    return QString("background:%1; color:%2; border:none;")
        .arg(Theme::hex(Theme::BgDeep), Theme::hex(Theme::TextPri));
}

QString qssIrOutput() {
    return QString("background:%1; color:%2; border:none;")
        .arg(Theme::hex(Theme::BgEditor), Theme::hex(Theme::TextPri));
}

QString qssStatusContainer() {
    return QString("background:%1;").arg(Theme::hex(Theme::BgPanel));
}

QString qssCursorLabel() {
    return QString(
        "padding:3px 12px; color:%1; background:%2; border-left:1px solid %3;"
        "font-family:%4; font-size:11px;")
        .arg(Theme::hex(Theme::TextSec), Theme::hex(Theme::BgPanel),
             Theme::hex(Theme::Border),  QString(kMono));
}

QString qssFindBar() {
    return QString("background:%1; border-top:1px solid %2;")
        .arg(Theme::hex(Theme::BgPanel), Theme::hex(Theme::Border));
}

QString qssFindInput() {
    return QString(
        "QLineEdit { background:%1; color:%2; border:1px solid %3;"
        "            border-radius:5px; padding:3px 8px; }"
        "QLineEdit:focus { border-color:%4; }")
        .arg(Theme::hex(Theme::BgSurface), Theme::hex(Theme::TextPri),
             Theme::hex(Theme::Border),    Theme::hex(Theme::Go));
}

QString qssFindBtn() {
    return QString(
        "QPushButton { background:%1; color:%2; border:1px solid %3;"
        "              border-radius:5px; padding:1px 10px; }"
        "QPushButton:hover { background:%4; color:%5; border-color:%5; }")
        .arg(Theme::hex(Theme::BgSurface), Theme::hex(Theme::TextSec),
             Theme::hex(Theme::Border),    Theme::hex(Theme::BgHover),
             Theme::hex(Theme::Go));
}

QString qssFindLabel() {
    return QString("color:%1;").arg(Theme::hex(Theme::TextSec));
}

QString qssFindCount() {
    return QString("color:%1; font-size:11px;").arg(Theme::hex(Theme::TextMuted));
}

} // namespace

// make3DIcon — genera iconos con aspecto glossy/3D (degradado + brillo + glifo)
static QIcon make3DIcon(const QString& kind, QColor base, int size = 30) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF tile(2, 2, size - 4, size - 4);
    // cuerpo con degradado vertical
    QLinearGradient g(tile.topLeft(), tile.bottomLeft());
    g.setColorAt(0.0, base.lighter(135));
    g.setColorAt(1.0, base.darker(120));
    p.setPen(QPen(base.darker(150), 1));
    p.setBrush(g);
    p.drawRoundedRect(tile, 7, 7);
    // brillo superior (efecto glossy)
    QLinearGradient gloss(tile.topLeft(), QPointF(tile.left(), tile.center().y()));
    gloss.setColorAt(0.0, QColor(255, 255, 255, 110));
    gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(gloss);
    p.drawRoundedRect(QRectF(tile.left() + 1.5, tile.top() + 1.5,
                             tile.width() - 3, tile.height() / 2 - 1), 6, 6);

    // glifo blanco
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    QPointF c = tile.center();
    double R = tile.width() * 0.30;

    if (kind == "run") {
        QPainterPath tri;
        tri.moveTo(c.x() - R * 0.8, c.y() - R);
        tri.lineTo(c.x() + R, c.y());
        tri.lineTo(c.x() - R * 0.8, c.y() + R);
        tri.closeSubpath();
        p.drawPath(tri);
    } else if (kind == "compile") {
        // engranaje: dientes + anillo + centro
        p.save();
        p.translate(c);
        for (int i = 0; i < 8; ++i) {
            p.drawRect(QRectF(-1.6, -R - 1.5, 3.2, 4.0));
            p.rotate(45);
        }
        p.restore();
        QPen ring(Qt::white, 2.6); p.setPen(ring); p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, R * 0.78, R * 0.78);
        p.setPen(Qt::NoPen); p.setBrush(Qt::white);
        p.drawEllipse(c, R * 0.28, R * 0.28);
    } else if (kind == "new") {
        QRectF page(c.x() - R * 0.7, c.y() - R, R * 1.4, R * 2);
        p.drawRoundedRect(page, 2, 2);
        // esquina doblada
        p.setBrush(base.darker(115));
        QPainterPath fold;
        fold.moveTo(page.right() - R * 0.5, page.top());
        fold.lineTo(page.right(), page.top() + R * 0.5);
        fold.lineTo(page.right() - R * 0.5, page.top() + R * 0.5);
        fold.closeSubpath();
        p.drawPath(fold);
    } else if (kind == "open") {
        // carpeta
        QRectF body(c.x() - R, c.y() - R * 0.4, R * 2, R * 1.5);
        QRectF tab (c.x() - R, c.y() - R * 0.85, R * 0.9, R * 0.6);
        p.drawRoundedRect(tab, 2, 2);
        p.drawRoundedRect(body, 2, 2);
    } else if (kind == "save") {
        // flecha hacia abajo + bandeja (estilo "guardar/descargar")
        double w = R * 0.5;
        p.drawRect(QRectF(c.x() - w / 2, c.y() - R, w, R * 1.1));
        QPainterPath arr;
        arr.moveTo(c.x() - R * 0.75, c.y() + R * 0.0);
        arr.lineTo(c.x() + R * 0.75, c.y() + R * 0.0);
        arr.lineTo(c.x(),            c.y() + R * 0.9);
        arr.closeSubpath();
        p.drawPath(arr);
        p.drawRect(QRectF(c.x() - R, c.y() + R * 1.05, R * 2, R * 0.35));
    } else if (kind == "stop") {
        // cuadrado de "detener"
        QRectF sq(c.x() - R * 0.8, c.y() - R * 0.8, R * 1.6, R * 1.6);
        p.drawRoundedRect(sq, 2, 2);
    }
    p.end();
    return QIcon(pm);
}


// MainWindow
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Cargar el tema guardado antes de construir la UI.
    QSettings s("ITCR", "MiniGoEditor");
    int themeIdx = s.value("theme", int(Theme::Id::Classic)).toInt();
    currentTheme_ = static_cast<Theme::Id>(qBound(0, themeIdx, int(Theme::Id::Go)));
    Theme::apply(currentTheme_);

    setupUI();
    setupMenuBar();
    setWindowTitle("MiniGo Compiler — Editor");
    resize(1200, 800);
    setAcceptDrops(true);   // abrir archivos arrastrándolos

    // Restaurar tamaño de ventana y tamaño de fuente de la sesión anterior
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    int pts = s.value("editorFontPt", 0).toInt();
    if (pts > 0) {
        QFont f = editor_->font();
        f.setPointSize(pts);
        editor_->setFont(f);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    QString path = urls.first().toLocalFile();
    if (!path.isEmpty() && confirmDiscard()) openPath(path);
}

void MainWindow::setupUI() {
    auto* central = new QWidget(this);
    auto* vLayout = new QVBoxLayout(central);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);

    // ── Editor + find bar ─────────────────────────────────────
    auto* editorContainer = new QWidget(central);
    auto* editorLayout    = new QVBoxLayout(editorContainer);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    splitter_ = new QSplitter(Qt::Vertical, central);
    splitter_->setHandleWidth(4);
    splitter_->setStyleSheet(qssSplitter());

    editor_ = new CodeEditor(splitter_);
    mgHl_ = new MiniGoHighlighter(editor_->document());
    editor_->setPlaceholderText("// Write or open a MiniGo file here...");

    setupFindBar();
    editorLayout->addWidget(editor_);
    editorLayout->addWidget(findBar_);
    splitter_->addWidget(editorContainer);

    // ── Output tabs ───────────────────────────────────────────
    outputTabs_ = new QTabWidget(splitter_);
    outputTabs_->setStyleSheet(qssTabs());

    // Tab 0 — Errors (with "Copy All" button in header)
    auto* errorsWidget = new QWidget();
    auto* errorsLayout = new QVBoxLayout(errorsWidget);
    errorsLayout->setContentsMargins(0, 0, 0, 0);
    errorsLayout->setSpacing(0);

    // Small toolbar above the error list
    auto* errToolbar = new QWidget();
    auto* errToolbarLayout = new QHBoxLayout(errToolbar);
    errToolbarLayout->setContentsMargins(4, 2, 4, 2);
    errToolbarLayout->setSpacing(4);

    auto* copyAllBtn = new QPushButton("📋  Copy All");
    copyAllBtn->setToolTip("Copy all errors to clipboard");
    copyAllBtn->setStyleSheet(qssOutBtn());
    copyAllBtn->setFixedHeight(22);
    outBtns_ << copyAllBtn;
    errToolbarLayout->addWidget(copyAllBtn);
    errToolbarLayout->addStretch();

    errorsLayout->addWidget(errToolbar);

    errorList_ = new QListWidget();
    errorList_->setStyleSheet(qssErrorList());
    errorsLayout->addWidget(errorList_);
    outputTabs_->addTab(errorsWidget, "⚠  Errors");

    // Tab 1 — Program output (texto enriquecido a color)
    programOutput_ = new QTextEdit();
    programOutput_->setReadOnly(true);
    programOutput_->setFont(QFont("Cascadia Code", 11));
    programOutput_->setStyleSheet(qssProgramOutput());
    programOutput_->setPlaceholderText("Program output will appear here after compilation...");
    outputTabs_->addTab(programOutput_, "▶  Output");

    // Tab 2 — LLVM IR (con resaltado de sintaxis)
    irOutput_ = new QPlainTextEdit();
    irOutput_->setReadOnly(true);
    irOutput_->setFont(QFont("Cascadia Code", 10));
    irOutput_->setStyleSheet(qssIrOutput());
    irOutput_->setPlaceholderText("Generated LLVM IR will appear here after compilation...");
    irHl_ = new IRHighlighter(irOutput_->document());
    outputTabs_->addTab(irOutput_, "⚙  LLVM IR");

    // Esquina derecha: colapsar/expandir panel + limpiar
    auto* cornerWidget  = new QWidget();
    auto* cornerLayout  = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);

    auto* collapseBtn = new QPushButton("▾");
    collapseBtn->setToolTip("Collapse / expand the output panel");
    collapseBtn->setStyleSheet(qssOutBtn());
    outBtns_ << collapseBtn;
    connect(collapseBtn, &QPushButton::clicked, this, [this, collapseBtn]() {
        toggleOutputPanel();
        collapseBtn->setText(outputCollapsed_ ? "▴" : "▾");
    });

    auto* clearBtn = new QPushButton("🧹  Clear");
    clearBtn->setToolTip("Clear output panels");
    clearBtn->setStyleSheet(qssOutBtn());
    outBtns_ << clearBtn;
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearErrors);

    cornerLayout->addWidget(collapseBtn);
    cornerLayout->addWidget(clearBtn);
    outputTabs_->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    splitter_->addWidget(outputTabs_);
    splitter_->setStretchFactor(0, 4);
    splitter_->setStretchFactor(1, 1);
    vLayout->addWidget(splitter_);

    // ── Status bar: dot + message + cursor position ───────────
    statusBarWidget_ = new QWidget(central);
    statusBarWidget_->setStyleSheet(qssStatusContainer());
    auto* statusLayout = new QHBoxLayout(statusBarWidget_);
    statusLayout->setContentsMargins(10, 0, 0, 0);
    statusLayout->setSpacing(8);

    // Punto de estado (cambia de color y pulsa al correr)
    statusDot_ = new QLabel(statusBarWidget_);
    statusDot_->setFixedSize(9, 9);
    statusDot_->setStyleSheet(
        QString("background:%1; border-radius:4px;").arg(Theme::hex(Theme::TextMuted)));
    statusLayout->addWidget(statusDot_);

    statusLabel_ = new QLabel("Ready.", statusBarWidget_);
    statusLayout->addWidget(statusLabel_, 1);

    cursorPosLabel_ = new QLabel("Ln 1, Col 1", statusBarWidget_);
    cursorPosLabel_->setStyleSheet(qssCursorLabel());
    cursorPosLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cursorPosLabel_->setFixedWidth(120);
    statusLayout->addWidget(cursorPosLabel_);
    vLayout->addWidget(statusBarWidget_);

    // Temporizador para el pulso del punto de estado mientras corre el programa
    pulseTimer_ = new QTimer(this);
    pulseTimer_->setInterval(450);
    connect(pulseTimer_, &QTimer::timeout, this, [this]() {
        pulseOn_ = !pulseOn_;
        QColor c = pulseOn_ ? Theme::Orange : QColor(Theme::Orange).darker(220);
        statusDot_->setStyleSheet(
            QString("background:%1; border-radius:4px;").arg(c.name()));
    });

    setCentralWidget(central);

    // Estilo global de la ventana (incluye menús).
    setStyleSheet(qssWindow());

    // Estado inicial con el estilo del tema.
    setStatus("Ready.", 0);

    // ── Signal connections ────────────────────────────────────
    connect(errorList_, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onErrorClicked);

    // Right-click context menu on error list
    errorList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(errorList_, &QListWidget::customContextMenuRequested,
            this, &MainWindow::showErrorContextMenu);

    // Ctrl+C on error list → copy selected
    auto* copyOne = new QShortcut(QKeySequence::Copy, errorList_);
    copyOne->setContext(Qt::WidgetShortcut);
    connect(copyOne, &QShortcut::activated, this, [this]() {
        auto* item = errorList_->currentItem();
        if (item) QApplication::clipboard()->setText(item->text());
    });

    // "Copy All" button click
    connect(copyAllBtn, &QPushButton::clicked, this, [this]() {
        QStringList lines;
        for (int i = 0; i < errorList_->count(); i++)
            lines << errorList_->item(i)->text();
        if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join("\n"));
    });

    // Title update on file modification
    connect(editor_->document(), &QTextDocument::modificationChanged,
            this, [this](bool) { updateTitle(); });

    // Cursor position update
    connect(editor_, &QPlainTextEdit::cursorPositionChanged,
            this, &MainWindow::updateCursorPosition);

    // Error navigation shortcuts
    auto* nextErr = new QShortcut(QKeySequence(Qt::Key_F2), this);
    connect(nextErr, &QShortcut::activated, this, &MainWindow::goToNextError);
    auto* prevErr = new QShortcut(QKeySequence(Qt::ShiftModifier | Qt::Key_F2), this);
    connect(prevErr, &QShortcut::activated, this, &MainWindow::goToPrevError);

    // Editor shortcuts
    auto* findShortcut    = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, &MainWindow::findText);
    auto* replaceShortcut = new QShortcut(QKeySequence::Replace, this);
    connect(replaceShortcut, &QShortcut::activated, this, &MainWindow::replaceText);
    auto* commentShortcut = new QShortcut(QKeySequence("Ctrl+/"), this);
    connect(commentShortcut, &QShortcut::activated, this, &MainWindow::toggleComment);
    auto* gotoShortcut    = new QShortcut(QKeySequence("Ctrl+G"), this);
    connect(gotoShortcut, &QShortcut::activated, this, &MainWindow::goToLine);

    // Zoom de fuente del editor (Ctrl + / Ctrl - además de Ctrl+rueda)
    auto* zoomInSc  = new QShortcut(QKeySequence::ZoomIn, this);
    connect(zoomInSc,  &QShortcut::activated, this, [this]{ editor_->zoomIn(1); });
    auto* zoomInSc2 = new QShortcut(QKeySequence("Ctrl+="), this);
    connect(zoomInSc2, &QShortcut::activated, this, [this]{ editor_->zoomIn(1); });
    auto* zoomOutSc = new QShortcut(QKeySequence::ZoomOut, this);
    connect(zoomOutSc, &QShortcut::activated, this, [this]{ editor_->zoomOut(1); });

    setupToolBar();
    updateTitle();
    updateCursorPosition();
}

void MainWindow::setupMenuBar() {
    // ── File menu ─────────────────────────────────────────────
    auto* fileMenu = menuBar()->addMenu("&File");

    fileMenu->addAction("&New",  QKeySequence::New,  this, &MainWindow::newFile)
            ->setStatusTip("Create a new file (Ctrl+N)");
    fileMenu->addAction("&Open...", QKeySequence::Open, this, &MainWindow::openFile)
            ->setStatusTip("Open an existing file (Ctrl+O)");
    fileMenu->addSeparator();
    fileMenu->addAction("&Save",    QKeySequence::Save,   this, &MainWindow::saveFile)
            ->setStatusTip("Save the current file (Ctrl+S)");
    fileMenu->addAction("Save &As...", QKeySequence::SaveAs, this, &MainWindow::saveFileAs)
            ->setStatusTip("Save to a new location (Ctrl+Shift+S)");
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, qApp, &QApplication::quit)
            ->setStatusTip("Close the editor");

    // ── Edit menu ─────────────────────────────────────────────
    auto* editMenu = menuBar()->addMenu("&Edit");

    editMenu->addAction("&Undo",  QKeySequence::Undo,  editor_, &QPlainTextEdit::undo);
    editMenu->addAction("&Redo",  QKeySequence::Redo,  editor_, &QPlainTextEdit::redo);
    editMenu->addSeparator();
    editMenu->addAction("Cu&t",   QKeySequence::Cut,   editor_, &QPlainTextEdit::cut);
    editMenu->addAction("&Copy",  QKeySequence::Copy,  editor_, &QPlainTextEdit::copy);
    editMenu->addAction("&Paste", QKeySequence::Paste, editor_, &QPlainTextEdit::paste);
    editMenu->addAction("Select &All", QKeySequence::SelectAll, editor_, &QPlainTextEdit::selectAll);
    editMenu->addSeparator();
    editMenu->addAction("&Find...", QKeySequence::Find, this, &MainWindow::findText)
            ->setStatusTip("Find text in the editor (Ctrl+F)");
    editMenu->addAction("&Replace...", QKeySequence::Replace, this, &MainWindow::replaceText)
            ->setStatusTip("Find and replace text (Ctrl+H)");
    editMenu->addAction("&Go to Line...", QKeySequence("Ctrl+G"), this, &MainWindow::goToLine)
            ->setStatusTip("Jump to a specific line number (Ctrl+G)");
    editMenu->addSeparator();
    editMenu->addAction("&Toggle Comment", QKeySequence("Ctrl+/"), this, &MainWindow::toggleComment)
            ->setStatusTip("Comment or uncomment selected lines (Ctrl+/)");

    // ── Build menu ────────────────────────────────────────────
    auto* buildMenu = menuBar()->addMenu("&Build");
    buildMenu->addAction("&Compile",   QKeySequence("Ctrl+Shift+B"),
                         this, &MainWindow::compileOnly)
             ->setStatusTip("Compile the current file (Ctrl+Shift+B)");
    buildMenu->addAction("Compile && &Run", QKeySequence("Ctrl+B"),
                         this, &MainWindow::compileAndRun)
             ->setStatusTip("Compile and run the current file (Ctrl+B)");
    buildMenu->addAction("&Stop", QKeySequence("Ctrl+."), this, &MainWindow::stopRun)
             ->setStatusTip("Stop the running program (Ctrl+.)");

    // ── View menu ─────────────────────────────────────────────
    auto* viewMenu = menuBar()->addMenu("&View");
    auto* wrapAct = viewMenu->addAction("&Word Wrap", this, &MainWindow::toggleWordWrap);
    wrapAct->setCheckable(true);
    wrapAct->setChecked(false);
    viewMenu->addAction("Zoom &In",  QKeySequence::ZoomIn,  this, [this]{ editor_->zoomIn(1); });
    viewMenu->addAction("Zoom &Out", QKeySequence::ZoomOut, this, [this]{ editor_->zoomOut(1); });

    // Submenú de temas
    viewMenu->addSeparator();
    auto* themeMenu = viewMenu->addMenu("&Theme");
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (Theme::Id id : Theme::all()) {
        auto* act = themeMenu->addAction(Theme::name(id));
        act->setCheckable(true);
        act->setChecked(id == currentTheme_);
        themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, id]{ setTheme(id); });
    }

    // ── Help menu ─────────────────────────────────────────────
    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About MiniGo Editor", this, &MainWindow::showAbout);
}

// ============================================================
// Temas
// ============================================================

void MainWindow::setTheme(Theme::Id id) {
    currentTheme_ = id;
    Theme::apply(id);
    applyTheme();
    QSettings s("ITCR", "MiniGoEditor");
    s.setValue("theme", int(id));
    setStatus("Theme: " + Theme::name(id), 0);
}

// Re-aplica todas las hojas de estilo al tema activo (cambio en vivo).
void MainWindow::applyTheme() {
    setStyleSheet(qssWindow());
    if (toolBar_)         toolBar_->setStyleSheet(qssToolbar());
    if (splitter_)        splitter_->setStyleSheet(qssSplitter());
    if (outputTabs_)      outputTabs_->setStyleSheet(qssTabs());
    if (errorList_)       errorList_->setStyleSheet(qssErrorList());
    if (programOutput_)   programOutput_->setStyleSheet(qssProgramOutput());
    if (irOutput_)        irOutput_->setStyleSheet(qssIrOutput());
    if (statusBarWidget_) statusBarWidget_->setStyleSheet(qssStatusContainer());
    if (cursorPosLabel_)  cursorPosLabel_->setStyleSheet(qssCursorLabel());
    if (findBar_)         findBar_->setStyleSheet(qssFindBar());
    if (findInput_)       findInput_->setStyleSheet(qssFindInput());
    if (replaceInput_)    replaceInput_->setStyleSheet(qssFindInput());
    if (findCountLabel_)  findCountLabel_->setStyleSheet(qssFindCount());
    for (auto* b : outBtns_)    if (b) b->setStyleSheet(qssOutBtn());
    for (auto* b : findBtns_)   if (b) b->setStyleSheet(qssFindBtn());
    for (auto* l : findLabels_) if (l) l->setStyleSheet(qssFindLabel());

    // Editor + resaltadores de sintaxis
    if (editor_) editor_->applyTheme();
    if (mgHl_)   mgHl_->rebuild();
    if (irHl_)   irHl_->rebuild();

    // Barra de estado (label + punto) al tema actual
    setStatus(statusLabel_ ? statusLabel_->text() : QString(), lastStatusKind_);
}

// ============================================================
// Toolbar + Find Bar setup
// ============================================================

void MainWindow::setupToolBar() {
    toolBar_ = addToolBar("Main Toolbar");
    toolBar_->setMovable(false);
    toolBar_->setIconSize(QSize(26, 26));
    toolBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar_->setStyleSheet(qssToolbar());

    // Archivo
    toolBar_->addAction(make3DIcon("new",  QColor(90, 100, 115)),
                        "New",  this, &MainWindow::newFile)->setToolTip("New file (Ctrl+N)");
    toolBar_->addAction(make3DIcon("open", QColor(217, 147, 10)),
                        "Open", this, &MainWindow::openFile)->setToolTip("Open file (Ctrl+O)");
    toolBar_->addAction(make3DIcon("save", QColor(24, 169, 153)),
                        "Save", this, &MainWindow::saveFile)->setToolTip("Save file (Ctrl+S)");

    toolBar_->addSeparator();

    // Build (Compile azul, Run verde)
    auto* compileAct = toolBar_->addAction(
        make3DIcon("compile", QColor(31, 111, 235)),
        "Compile", this, &MainWindow::compileOnly);
    compileAct->setToolTip("Compile only (Ctrl+Shift+B)");

    auto* runAct = toolBar_->addAction(
        make3DIcon("run", QColor(46, 160, 67)),
        "Compile && Run", this, &MainWindow::compileAndRun);
    runAct->setToolTip("Compile and run (Ctrl+B)");

    stopAct_ = toolBar_->addAction(
        make3DIcon("stop", QColor(200, 60, 55)),
        "Stop", this, &MainWindow::stopRun);
    stopAct_->setToolTip("Stop the running program (Ctrl+.)");
    stopAct_->setEnabled(false);
}

void MainWindow::setupFindBar() {
    findBar_ = new QWidget();
    findBar_->setStyleSheet(qssFindBar());
    findBar_->setVisible(false);

    auto* outer = new QVBoxLayout(findBar_);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(4);

    auto mkInput = [&](const char* ph) {
        auto* in = new QLineEdit(findBar_);
        in->setPlaceholderText(ph);
        in->setStyleSheet(qssFindInput());
        in->setFixedWidth(220);
        return in;
    };
    auto mkBtn = [&](const QString& text, const char* tip) {
        auto* btn = new QPushButton(text, findBar_);
        btn->setFixedHeight(22);
        btn->setToolTip(tip);
        btn->setStyleSheet(qssFindBtn());
        findBtns_ << btn;
        return btn;
    };
    auto mkLabel = [&](const char* t) {
        auto* l = new QLabel(t, findBar_);
        l->setStyleSheet(qssFindLabel());
        l->setFixedWidth(58);
        findLabels_ << l;
        return l;
    };

    // ── Fila 1: Find ──
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(4);
    row1->addWidget(mkLabel("Find:"));
    findInput_ = mkInput("Search text...");
    row1->addWidget(findInput_);
    auto* prevBtn  = mkBtn("▲ Prev", "Find previous");
    auto* nextBtn  = mkBtn("▼ Next", "Find next (Enter)");
    auto* closeBtn = mkBtn("✕",       "Close (Esc)");
    row1->addWidget(prevBtn);
    row1->addWidget(nextBtn);
    findCountLabel_ = new QLabel("", findBar_);
    findCountLabel_->setStyleSheet(qssFindCount());
    row1->addWidget(findCountLabel_);
    row1->addStretch();
    row1->addWidget(closeBtn);
    outer->addLayout(row1);

    // ── Fila 2: Replace ──
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(4);
    row2->addWidget(mkLabel("Replace:"));
    replaceInput_ = mkInput("Replace with...");
    row2->addWidget(replaceInput_);
    auto* replBtn    = mkBtn("Replace",     "Replace current match");
    auto* replAllBtn = mkBtn("Replace All", "Replace all matches");
    row2->addWidget(replBtn);
    row2->addWidget(replAllBtn);
    row2->addStretch();
    outer->addLayout(row2);

    connect(findInput_,  &QLineEdit::returnPressed, this, &MainWindow::findNext);
    connect(findInput_,  &QLineEdit::textChanged,   this, &MainWindow::findNext);
    connect(replaceInput_, &QLineEdit::returnPressed, this, &MainWindow::replaceNext);
    connect(nextBtn,     &QPushButton::clicked,     this, &MainWindow::findNext);
    connect(prevBtn,     &QPushButton::clicked,     this, &MainWindow::findPrev);
    connect(replBtn,     &QPushButton::clicked,     this, &MainWindow::replaceNext);
    connect(replAllBtn,  &QPushButton::clicked,     this, &MainWindow::replaceAll);
    connect(closeBtn,    &QPushButton::clicked,     this, &MainWindow::closeFindBar);

    // Esc cierra la barra
    for (QLineEdit* in : { findInput_, replaceInput_ }) {
        auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), in);
        esc->setContext(Qt::WidgetShortcut);
        connect(esc, &QShortcut::activated, this, &MainWindow::closeFindBar);
    }
}

// ============================================================
// File operations (in English)
// ============================================================

bool MainWindow::confirmDiscard() {
    if (!editor_->document()->isModified()) return true;
    auto btn = QMessageBox::question(
        this, "Unsaved Changes",
        "The file has unsaved changes.\nDo you want to save them before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (btn == QMessageBox::Cancel) return false;
    if (btn == QMessageBox::Save)   saveFile();
    return true;
}

void MainWindow::updateTitle() {
    QString title = "MiniGo Editor — ";
    title += currentFile_.isEmpty() ? "Untitled" : QFileInfo(currentFile_).fileName();
    if (editor_->document()->isModified()) title += " *";
    setWindowTitle(title);
}

void MainWindow::newFile() {
    if (!confirmDiscard()) return;
    editor_->clear();
    currentFile_.clear();
    editor_->document()->setModified(false);
    clearErrors();
    updateTitle();
    setStatus("New file", 0);
}

void MainWindow::openFile() {
    if (!confirmDiscard()) return;
    QString path = QFileDialog::getOpenFileName(
        this, "Open MiniGo File",
        currentFile_.isEmpty() ? "" : QFileInfo(currentFile_).absolutePath(),
        "MiniGo Files (*.go *.mg);;All Files (*)");
    if (path.isEmpty()) return;
    openPath(path);
}

void MainWindow::openPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Could not open the file.");
        return;
    }
    editor_->setPlainText(QTextStream(&f).readAll());
    currentFile_ = path;
    editor_->document()->setModified(false);
    clearErrors();
    updateTitle();
    setStatus("Opened: " + currentFile_, 0);
}

void MainWindow::saveFile() {
    if (currentFile_.isEmpty()) { saveFileAs(); return; }
    QFile f(currentFile_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Could not save the file.");
        return;
    }
    QTextStream(&f) << editor_->toPlainText();
    editor_->document()->setModified(false);
    updateTitle();
    setStatus("Saved: " + currentFile_, 1);
}

void MainWindow::saveFileAs() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save As...",
        currentFile_.isEmpty() ? "" : currentFile_,
        "MiniGo Files (*.go *.mg);;All Files (*)");
    if (path.isEmpty()) return;

    currentFile_ = path;
    QFile f(currentFile_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Could not save the file.");
        return;
    }
    QTextStream(&f) << editor_->toPlainText();
    editor_->document()->setModified(false);
    updateTitle();
    setStatus("Saved as: " + currentFile_, 1);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscard()) { event->ignore(); return; }
    if (runProc_) { runProc_->kill(); runProc_->waitForFinished(1000); }
    QSettings s("ITCR", "MiniGoEditor");
    s.setValue("geometry", saveGeometry());
    s.setValue("editorFontPt", editor_->font().pointSize());
    event->accept();
}

// ============================================================
// Build operations
// ============================================================

void MainWindow::compileOnly()   { compile(false); }
void MainWindow::compileAndRun() { compile(true);  }

// Barra de estado con color: 0=info (neutro), 1=ok (verde), 2=error (rojo)
void MainWindow::setStatus(const QString& text, int kind) {
    lastStatusKind_ = kind;
    QColor fg  = Theme::TextSec, dot = Theme::Go;
    if      (kind == 1) { fg = Theme::Green; dot = Theme::Green; }
    else if (kind == 2) { fg = Theme::Red;   dot = Theme::Red;   }
    statusLabel_->setStyleSheet(QString(
        "padding:3px 6px; color:%1; background:%2; font-weight:600;"
        "font-family:'Cascadia Code','Consolas',monospace; font-size:11px;")
        .arg(fg.name(), Theme::hex(Theme::BgPanel)));
    statusLabel_->setText(text);

    // El punto refleja el estado salvo que esté pulsando (programa corriendo)
    if (statusDot_ && !(pulseTimer_ && pulseTimer_->isActive()))
        statusDot_->setStyleSheet(
            QString("background:%1; border-radius:4px;").arg(dot.name()));
}

// Colapsa el panel de salida a la altura de los tabs y lo restaura.
void MainWindow::toggleOutputPanel() {
    QList<int> sizes = splitter_->sizes();
    if (sizes.size() < 2) return;
    const int total = sizes[0] + sizes[1];

    if (!outputCollapsed_) {
        savedOutputSize_ = sizes[1];
        const int minBottom = 34;   // deja visible solo la barra de tabs
        splitter_->setSizes({ total - minBottom, minBottom });
        outputCollapsed_ = true;
    } else {
        int bottom = savedOutputSize_ > 0 ? savedOutputSize_ : total / 4;
        splitter_->setSizes({ total - bottom, bottom });
        outputCollapsed_ = false;
    }
}

void MainWindow::compile(bool runAfter) {
    clearErrors();
    setStatus("⏳  Compiling…", 0);

    // Guardar contenido en archivo temporal
    QTemporaryFile tmp(QDir::tempPath() + "/minigo_XXXXXX.go");
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        setStatus("Error: could not create temporary file.", 2);
        return;
    }
    QTextStream(&tmp) << editor_->toPlainText();
    tmp.flush();
    string filePath = tmp.fileName().toStdString();

    // ---- Pipeline del compilador ----
    vector<string> errors;
    string lastIR;
    try {
        antlr4::ANTLRFileStream input;
        input.loadFromFile(filePath);

        MiniGoLexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        MiniGoParser parser(&tokens);

        // Error listener profesional: APIs directas de ANTLR4, separa scanner/parser
        MiniGoSyntaxErrorListener errListener;
        lexer.removeErrorListeners();
        lexer.addErrorListener(&errListener);
        parser.removeErrorListeners();
        parser.addErrorListener(&errListener);

        // Estrategia de recuperación de ANTLR4 (DefaultErrorStrategy); la calidad
        // de los mensajes la aporta el listener (línea/columna, "did you mean?").
        parser.setErrorHandler(std::make_shared<MiniGoErrorStrategy>());

        antlr4::tree::ParseTree* tree = parser.root();

        if (errListener.hasErrors()) {
            errors = errListener.getErrors(); // filtrado: scanner suprime parser cascade
        } else {
            TypeChecker tc;
            tc.visit(tree);
            if (tc.hasErrors()) {
                errors = tc.getErrors();
            } else {
                Encoder enc(tc.getReferenceMap(), tc.getDeclSymbolMap());
                enc.visit(tree);
                bool ok = enc.finalize();
                lastIR = enc.getIR();
                if (!ok)
                    errors.push_back("CODEGEN ERROR: generated IR failed verification: "
                                     + enc.getVerifyError());
            }
        }
    } catch (exception& e) {
        errors.push_back(string("INTERNAL ERROR: ") + e.what());
    }

    if (errors.empty()) {
        showIR(QString::fromStdString(lastIR));

        // Escribir el IR al path fijo sin depender del CWD del Encoder.
        // (finalize() escribe al CWD que puede diferir del applicationDirPath)
        QString irPath = QDir(QCoreApplication::applicationDirPath()).filePath("output.ll");
        {
            QFile irFile(irPath);
            if (irFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                irFile.write(QByteArray::fromStdString(lastIR));
                irFile.close();
            }
        }

        if (runAfter) {
            runAndShowOutput(irPath);
        } else {
            setStatus("✅  Compilation successful — IR generated.", 1);
            programOutput_->setHtml(QString(
                "<div style='font-family:Consolas,monospace;color:%1;font-weight:bold;'>"
                "✓ Compilation successful — IR generated (output.ll / output.o)."
                "</div>").arg(Theme::hex(Theme::Green)));
            outputTabs_->setCurrentIndex(2); // show IR tab
        }
    } else {
        showErrors(errors); // showErrors updates the status bar (red)
    }
}

void MainWindow::showIR(const QString& ir) {
    irOutput_->setPlainText(ir);
}

// ============================================================
// Cursor position, Find, Comment, Go To Line
// ============================================================

void MainWindow::updateCursorPosition() {
    QTextCursor c = editor_->textCursor();
    cursorPosLabel_->setText(
        QString("Ln %1, Col %2")
            .arg(c.blockNumber() + 1)
            .arg(c.columnNumber() + 1));
}

void MainWindow::findText() {
    findBar_->setVisible(true);
    findInput_->setFocus();
    findInput_->selectAll();
    if (!findInput_->text().isEmpty()) editor_->setSearchMatches(findInput_->text());
}

void MainWindow::replaceText() {
    findBar_->setVisible(true);
    replaceInput_->setFocus();
    replaceInput_->selectAll();
    if (!findInput_->text().isEmpty()) editor_->setSearchMatches(findInput_->text());
}

// Reemplaza la coincidencia seleccionada (si coincide) y salta a la siguiente.
void MainWindow::replaceNext() {
    QString term = findInput_->text();
    if (term.isEmpty()) return;
    QTextCursor cur = editor_->textCursor();
    if (cur.hasSelection() && cur.selectedText() == term) {
        cur.insertText(replaceInput_->text());
        editor_->setTextCursor(cur);
    }
    findNext();   // ir a la siguiente y resaltar
}

void MainWindow::replaceAll() {
    QString term = findInput_->text();
    if (term.isEmpty()) return;
    QString repl = replaceInput_->text();

    QTextDocument* doc = editor_->document();
    QTextCursor cur(doc);
    cur.beginEditBlock();
    int count = 0;
    QTextCursor f = doc->find(term, 0);
    while (!f.isNull()) {
        f.insertText(repl);
        count++;
        f = doc->find(term, f);
    }
    cur.endEditBlock();

    editor_->setSearchMatches(findInput_->text());
    findCountLabel_->setText(count > 0 ? QString("%1 replaced").arg(count) : "No matches");
    setStatus(QString("Replaced %1 occurrence(s).").arg(count), count > 0 ? 1 : 0);
}

void MainWindow::findNext() {
    QString term = findInput_->text();
    if (term.isEmpty()) { findCountLabel_->setText(""); editor_->clearSearchMatches(); return; }

    editor_->setSearchMatches(term);   // resaltar todas las coincidencias

    QTextDocument* doc = editor_->document();
    QTextCursor cur = editor_->textCursor();
    QTextCursor found = doc->find(term, cur);
    if (found.isNull()) found = doc->find(term); // wrap around
    if (!found.isNull()) { editor_->setTextCursor(found); editor_->setActiveMatch(found); }

    // Count total matches
    int count = 0;
    QTextCursor c = doc->find(term);
    while (!c.isNull()) { count++; c = doc->find(term, c); }
    findCountLabel_->setText(count > 0 ? QString("%1 match(es)").arg(count) : "No matches");
}

void MainWindow::findPrev() {
    QString term = findInput_->text();
    if (term.isEmpty()) return;

    QTextDocument* doc = editor_->document();
    QTextCursor cur = editor_->textCursor();
    cur.movePosition(QTextCursor::Left); // step back to avoid re-finding same
    QTextCursor found = doc->find(term, cur, QTextDocument::FindBackward);
    if (found.isNull())
        found = doc->find(term, QTextCursor(doc), QTextDocument::FindBackward);
    if (!found.isNull()) { editor_->setTextCursor(found); editor_->setActiveMatch(found); }
}

void MainWindow::closeFindBar() {
    findBar_->setVisible(false);
    findInput_->clear();
    findCountLabel_->setText("");
    editor_->clearSearchMatches();
    editor_->setFocus();
}

void MainWindow::toggleComment() {
    QTextCursor cursor = editor_->textCursor();
    int startBlock = cursor.document()->findBlock(cursor.selectionStart()).blockNumber();
    int endBlock   = cursor.document()->findBlock(cursor.selectionEnd()).blockNumber();

    // Detect if all selected lines already start with "//"
    bool allCommented = true;
    QTextCursor c(editor_->document());
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, startBlock);
    for (int i = startBlock; i <= endBlock; i++) {
        QString line = c.block().text().trimmed();
        if (!line.startsWith("//")) { allCommented = false; break; }
        c.movePosition(QTextCursor::NextBlock);
    }

    // Apply toggle
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, startBlock);
    cursor.beginEditBlock();
    for (int i = startBlock; i <= endBlock; i++) {
        QTextCursor lc(editor_->document());
        lc.setPosition(c.block().position());
        lc.movePosition(QTextCursor::StartOfLine);
        if (allCommented) {
            lc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            if (lc.selectedText() == "//") lc.removeSelectedText();
        } else {
            lc.insertText("//");
        }
        c.movePosition(QTextCursor::NextBlock);
    }
    cursor.endEditBlock();
}

void MainWindow::goToLine() {
    bool ok;
    int line = QInputDialog::getInt(
        this, "Go to Line",
        QString("Line number (1 – %1):").arg(editor_->document()->blockCount()),
        editor_->textCursor().blockNumber() + 1,
        1, editor_->document()->blockCount(), 1, &ok);
    if (!ok) return;

    QTextCursor cursor(editor_->document());
    cursor.movePosition(QTextCursor::Start);
    if (line > 1)
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
    editor_->setTextCursor(cursor);
    editor_->centerCursor();
    editor_->setFocus();
}

// ============================================================
// Program execution
// ============================================================

void MainWindow::runAndShowOutput(const QString& irFile) {
    if (!QFile::exists(irFile)) {
        programOutput_->setPlainText("[ERROR] IR file not found: " + irFile);
        setStatus("⚠  IR file not found.", 2);
        outputTabs_->setCurrentIndex(1);
        return;
    }

    // Buscar lli: prioridad MSYS2 (mismo entorno que generó el IR) → PATH → otros
    QString lliPath;
    QString msys2ExtraPath; // DLLs adicionales para lli de MSYS2

    // 1. MSYS2 UCRT64 (compilador en mismo entorno)
    if (QFile::exists("C:/msys64/ucrt64/bin/lli.exe")) {
        lliPath      = "C:/msys64/ucrt64/bin/lli.exe";
        msys2ExtraPath = "C:/msys64/ucrt64/bin";
    }
    // 2. MSYS2 MinGW64
    else if (QFile::exists("C:/msys64/mingw64/bin/lli.exe")) {
        lliPath      = "C:/msys64/mingw64/bin/lli.exe";
        msys2ExtraPath = "C:/msys64/mingw64/bin";
    }
    // 3. Buscar en PATH del sistema
    else {
        lliPath = QStandardPaths::findExecutable("lli");
        if (lliPath.isEmpty())
            lliPath = QStandardPaths::findExecutable("lli.exe");
    }
    // 4. Ubicaciones conocidas de LLVM nativo
    if (lliPath.isEmpty()) {
        for (const char* c : { "C:/Program Files/LLVM/bin/lli.exe",
                                "C:/Program Files (x86)/LLVM/bin/lli.exe",
                                "C:/LLVM/bin/lli.exe" }) {
            if (QFile::exists(c)) { lliPath = c; break; }
        }
    }

    if (lliPath.isEmpty()) {
        programOutput_->setPlainText(
            "[ERROR] 'lli.exe' (LLVM IR interpreter) not found.\n"
            "Install MSYS2 with the 'llvm' package (ucrt64/mingw64), or install LLVM\n"
            "and make sure its bin directory is in the system PATH.");
        setStatus("⚠  'lli.exe' not found to run the program.", 2);
        outputTabs_->setCurrentIndex(1);
        return;
    }

    // Matar cualquier ejecución previa
    if (runProc_) { runProc_->kill(); runProc_->deleteLater(); runProc_ = nullptr; }

    runLli_ = lliPath;
    runIr_  = irFile;

    runProc_ = new QProcess(this);
    runProc_->setProgram(lliPath);
    runProc_->setArguments({irFile});

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!msys2ExtraPath.isEmpty())
        env.insert("PATH", msys2ExtraPath + ";" + env.value("PATH"));
    runProc_->setProcessEnvironment(env);

    connect(runProc_, &QProcess::finished, this, &MainWindow::onRunFinished);

    programOutput_->setHtml(QString(
        "<div style='font-family:Consolas,monospace;color:%1;font-weight:bold;'>▶ Running…</div>")
        .arg(Theme::hex(Theme::Go)));
    outputTabs_->setCurrentIndex(1);
    setStatus("▶  Running…", 0);
    if (stopAct_) stopAct_->setEnabled(true);
    if (pulseTimer_) { pulseOn_ = false; pulseTimer_->start(); }   // punto pulsante

    runProc_->start(QIODevice::ReadOnly);

    // Salvaguarda: terminar el proceso si excede 15 s
    QTimer::singleShot(15000, this, [this]() {
        if (runProc_ && runProc_->state() != QProcess::NotRunning)
            runProc_->kill();
    });
}

// Se invoca cuando lli termina (asíncrono): arma la salida a color.
void MainWindow::onRunFinished(int exitCode, QProcess::ExitStatus status) {
    if (!runProc_) return;
    if (pulseTimer_) pulseTimer_->stop();   // detener el pulso del punto de estado

    QString output = QString::fromLocal8Bit(runProc_->readAllStandardOutput());
    QString errOut = QString::fromLocal8Bit(runProc_->readAllStandardError());
    bool crashed = (status == QProcess::CrashExit);
    bool ok = !crashed && exitCode == 0;

    auto esc = [](const QString& s) { return s.toHtmlEscaped(); };
    QString html = "<div style='font-family:Consolas,\"Courier New\",monospace; font-size:11pt;'>";
    // El éxito se reporta en la barra de estado; aquí solo encabezamos los
    // casos de crash o error (con su separador) para no duplicar el mensaje.
    const QString border = Theme::hex(Theme::Border);
    if (crashed) {
        html += QString("<div style='color:%1;font-weight:bold;'>⏹ Run stopped.</div>")
                    .arg(Theme::hex(Theme::Orange));
        html += QString("<div style='border-top:1px solid %1;margin:5px 0;'></div>").arg(border);
    } else if (!ok) {
        html += QString("<div style='color:%1;font-weight:bold;'>"
                        "✗ The program exited with an error (exit %2)</div>")
                    .arg(Theme::hex(Theme::Red)).arg(exitCode);
        html += QString("<div style='border-top:1px solid %1;margin:5px 0;'></div>").arg(border);
    }

    auto block = [](QString s, const QString& color) -> QString {
        s.replace("\r\n", "\n"); s.remove('\r');
        while (s.endsWith('\n')) s.chop(1);
        if (s.isEmpty()) return QString();
        s = s.toHtmlEscaped(); s.replace("\n", "<br>");
        return QString("<div style='color:%1;white-space:pre-wrap;'>%2</div>").arg(color, s);
    };
    QString body = block(output, Theme::hex(Theme::TextPri)) + block(errOut, Theme::hex(Theme::Red));
    if (body.isEmpty())
        body = QString("<div style='color:%1;'>(no output)</div>").arg(Theme::hex(Theme::TextMuted));
    html += body;
    html += QString("<div style='color:%1;margin-top:8px;font-size:9pt;'>"
                    "lli: %2<br>IR: %3</div>")
                .arg(Theme::hex(Theme::TextMuted), esc(runLli_), esc(runIr_));
    html += "</div>";

    programOutput_->setHtml(html);
    setStatus(crashed ? "⏹  Run stopped."
              : (ok ? "✅  Program executed successfully."
                    : QString("⚠  The program exited with code %1.").arg(exitCode)),
              crashed ? 0 : (ok ? 1 : 2));
    outputTabs_->setCurrentIndex(1);
    if (stopAct_) stopAct_->setEnabled(false);

    runProc_->deleteLater();
    runProc_ = nullptr;
}

void MainWindow::stopRun() {
    if (runProc_ && runProc_->state() != QProcess::NotRunning)
        runProc_->kill();   // dispara onRunFinished con CrashExit
}

void MainWindow::toggleWordWrap() {
    bool wrapOn = (editor_->lineWrapMode() != QPlainTextEdit::NoWrap);
    editor_->setLineWrapMode(wrapOn ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About MiniGo Editor",
        "<b>MiniGo Compiler — Editor</b><br>"
        "Compiladores e Intérpretes · ITCR San Carlos · I-2026<br><br>"
        "Author: Magdaleno Gómez Díaz<br>"
        "Backend: ANTLR4 + LLVM (x86-64) &nbsp;·&nbsp; GUI: Qt6");
}

void MainWindow::showErrors(const vector<string>& errors) {
    outputTabs_->setCurrentIndex(0);

    // Paleta de colores y metadatos por tipo de error
    struct ErrorStyle {
        QColor   listBg;    // fondo del ítem en la lista
        QColor   listFg;    // texto del ítem
        QColor   lineBg;    // resaltado de línea en el editor
        QStyle::StandardPixmap icon;
    };
    auto getStyle = [](const QString& text) -> ErrorStyle {
        if (text.startsWith("SCANNER"))
            return { QColor(80, 50, 0),  QColor(255, 190, 80),
                     QColor(100, 60, 0), QStyle::SP_MessageBoxWarning  };
        if (text.startsWith("PARSER"))
            return { QColor(90, 15, 15), QColor(255, 110, 110),
                     QColor(110, 20, 20),QStyle::SP_MessageBoxCritical };
        if (text.startsWith("TYPE"))
            return { QColor(60, 15, 70), QColor(210, 130, 230),
                     QColor(70, 20, 80), QStyle::SP_MessageBoxCritical };
        if (text.startsWith("SCOPE"))
            return { QColor(10, 45, 85), QColor(100, 190, 255),
                     QColor(15, 55, 100),QStyle::SP_MessageBoxInformation };
        // INTERNAL o desconocido
        return { QColor(50, 50, 50), QColor(200, 200, 200),
                 QColor(60, 60, 60),  QStyle::SP_MessageBoxCritical };
    };

    // Regex para extraer línea y columna
    QRegularExpression re(R"(line: (\d+), column: (\d+))");

    int scannerCnt = 0, parserCnt = 0, typeCnt = 0, scopeCnt = 0;

    for (const auto& err : errors) {
        QString qErr = QString::fromStdString(err);
        auto s = getStyle(qErr);

        auto* item = new QListWidgetItem(qErr, errorList_);
        item->setBackground(s.listBg);
        item->setForeground(s.listFg);
        item->setIcon(style()->standardIcon(s.icon));

        // Count by type
        if      (qErr.startsWith("SCANNER")) scannerCnt++;
        else if (qErr.startsWith("PARSER"))  parserCnt++;
        else if (qErr.startsWith("TYPE"))    typeCnt++;
        else if (qErr.startsWith("SCOPE"))   scopeCnt++;

        // Extract line/column for precise navigation
        auto m = re.match(qErr);
        if (m.hasMatch()) {
            int line = m.captured(1).toInt();
            int col  = m.captured(2).toInt();
            item->setData(Qt::UserRole,     line);
            item->setData(Qt::UserRole + 1, col);
            // Fondo de línea translúcido: se mezcla con el editor, así el
            // resaltado es legible tanto en temas oscuros como claros.
            QColor lineBg = s.lineBg;
            lineBg.setAlpha(110);
            editor_->highlightErrorLine(line, lineBg);
        }
    }

    // Update tab title with error count
    outputTabs_->setTabText(0, QString("⚠  Errors (%1)").arg(errors.size()));

    // Build status bar breakdown
    QStringList parts;
    if (scannerCnt) parts << QString("%1 lexical").arg(scannerCnt);
    if (parserCnt)  parts << QString("%1 syntax").arg(parserCnt);
    if (typeCnt)    parts << QString("%1 type").arg(typeCnt);
    if (scopeCnt)   parts << QString("%1 scope").arg(scopeCnt);

    QString status = QString("❌  %1 error(s)").arg(errors.size());
    if (!parts.isEmpty()) status += "  —  " + parts.join(", ");
    setStatus(status, 2);
}

void MainWindow::onErrorClicked(QListWidgetItem* item) {
    if (!item) return;
    QVariant lineData = item->data(Qt::UserRole);
    if (!lineData.isValid()) return;

    int line = lineData.toInt();
    int col  = item->data(Qt::UserRole + 1).toInt(); // columna exacta del error

    QTextCursor cursor(editor_->document());
    cursor.movePosition(QTextCursor::Start);
    if (line > 1)
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
    if (col > 1)
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                            qMin(col - 1, cursor.block().length() - 1));
    editor_->setTextCursor(cursor);
    editor_->centerCursor();
    editor_->setFocus();
}

void MainWindow::goToNextError() {
    int count = errorList_->count();
    if (count == 0) return;
    int next = (errorList_->currentRow() + 1) % count;
    errorList_->setCurrentRow(next);
    onErrorClicked(errorList_->item(next));
}

void MainWindow::goToPrevError() {
    int count = errorList_->count();
    if (count == 0) return;
    int cur  = errorList_->currentRow();
    int prev = (cur <= 0) ? count - 1 : cur - 1;
    errorList_->setCurrentRow(prev);
    onErrorClicked(errorList_->item(prev));
}

void MainWindow::showErrorContextMenu(const QPoint& pos) {
    if (errorList_->count() == 0) return;

    QMenu menu(this);
    menu.setStyleSheet(QString(
        "QMenu { background:%1; color:%2; border:1px solid %3; }"
        "QMenu::item { padding:4px 18px; }"
        "QMenu::item:selected { background:%5; color:%4; }"
        "QMenu::separator { background:%3; height:1px; margin:4px 8px; }")
        .arg(Theme::hex(Theme::BgPanel), Theme::hex(Theme::TextSec),
             Theme::hex(Theme::Border),  Theme::hex(Theme::TextPri),
             Theme::hex(Theme::MenuSel)));

    auto* actCopyOne = menu.addAction("Copy selected error\tCtrl+C");
    actCopyOne->setEnabled(errorList_->currentItem() != nullptr);
    connect(actCopyOne, &QAction::triggered, this, [this]() {
        auto* item = errorList_->currentItem();
        if (item) QApplication::clipboard()->setText(item->text());
    });

    auto* actCopyAll = menu.addAction(
        QString("Copy all errors (%1)").arg(errorList_->count()));
    connect(actCopyAll, &QAction::triggered, this, [this]() {
        QStringList lines;
        for (int i = 0; i < errorList_->count(); i++)
            lines << errorList_->item(i)->text();
        QApplication::clipboard()->setText(lines.join("\n"));
    });

    menu.addSeparator();

    auto* actClear = menu.addAction("Clear error panel");
    connect(actClear, &QAction::triggered, this, &MainWindow::clearErrors);

    menu.exec(errorList_->viewport()->mapToGlobal(pos));
}

// ============================================================
// clearErrors — reset all output panels and tab titles
// ============================================================

void MainWindow::clearErrors() {
    errorList_->clear();
    irOutput_->clear();
    programOutput_->clear();
    editor_->clearErrorHighlights();
    outputTabs_->setTabText(0, "⚠  Errors");   // reset count in tab title
}

// ============================================================
// main — application entry point
// ============================================================
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("MiniGo Compiler");
    app.setStyle("Fusion");

    MainWindow window;
    window.show();
    return app.exec();
}
