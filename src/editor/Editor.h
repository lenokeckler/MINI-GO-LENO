#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QList>
#include <vector>
#include <string>

#include "theme/Theme.h"

class CodeEditor;
class MiniGoHighlighter;
class IRHighlighter;

class QToolBar;
class QLineEdit;
class QLabel;
class QSplitter;
class QTabWidget;
class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QPlainTextEdit;
class QPushButton;
class QAction;
class QTimer;
class QActionGroup;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;

// ============================================================
// MainWindow — ventana principal del editor.
// ============================================================

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    // File operations
    void newFile();
    void openFile();
    void saveFile();
    void saveFileAs();

    // Build operations
    void compileOnly();
    void compileAndRun();

    // Edit operations
    void findText();
    void replaceText();          // abre la barra enfocando "Replace"
    void findNext();
    void findPrev();
    void replaceNext();
    void replaceAll();
    void closeFindBar();
    void toggleComment();
    void goToLine();
    void toggleWordWrap();

    // Build
    void stopRun();
    void onRunFinished(int exitCode, QProcess::ExitStatus status);
    void toggleOutputPanel();        // colapsa/expande el panel de salida inferior

    // Help
    void showAbout();

    // Error panel
    void onErrorClicked(QListWidgetItem* item);
    void goToNextError();
    void goToPrevError();
    void showErrorContextMenu(const QPoint& pos);

    // Status
    void updateCursorPosition();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupFindBar();

    void showErrors(const std::vector<std::string>& errors);
    void showIR(const QString& ir);
    void runAndShowOutput(const QString& irFile);
    void clearErrors();
    void updateTitle();
    bool confirmDiscard();

    // ── Temas ─────────────────────────────────────────────────
    void setTheme(Theme::Id id);   // cambia el tema y re-estiliza en vivo
    void applyTheme();             // re-aplica los estilos al tema activo

    // Barra de estado con color según resultado (0=info, 1=ok, 2=error)
    void setStatus(const QString& text, int kind = 0);

    // Internal compile implementation
    void compile(bool runAfter);

    // Abrir un archivo desde una ruta (usado por Open y drag & drop)
    void openPath(const QString& path);

    // ── Widgets ───────────────────────────────────────────────
    CodeEditor*      editor_;
    QToolBar*        toolBar_;

    MiniGoHighlighter* mgHl_ = nullptr;   // resaltado MiniGo (se recolorea al re-temar)
    IRHighlighter*     irHl_ = nullptr;   // resaltado del IR

    // Find / Replace bar (hidden by default)
    QWidget*         findBar_;
    QLineEdit*       findInput_;
    QLineEdit*       replaceInput_;
    QLabel*          findCountLabel_;
    QList<QPushButton*> findBtns_;         // botones de la barra de búsqueda
    QList<QLabel*>      findLabels_;       // etiquetas "Find:" / "Replace:"

    // Botones de los paneles de salida (estilo "outBtn")
    QList<QPushButton*> outBtns_;

    // Build / run
    QProcess*        runProc_  = nullptr;   // proceso lli (asíncrono)
    QAction*         stopAct_  = nullptr;   // botón Detener
    QString          runLli_, runIr_;       // datos para el reporte al finalizar

    // Output tabs
    QSplitter*       splitter_ = nullptr;   // editor (arriba) / salida (abajo)
    QTabWidget*      outputTabs_;
    QListWidget*     errorList_;
    QTextEdit*       programOutput_;   // salida del programa (texto enriquecido a color)
    QPlainTextEdit*  irOutput_;
    int              savedOutputSize_ = 0;  // alto recordado del panel al colapsar
    bool             outputCollapsed_ = false;

    // Status bar components
    QWidget*         statusBarWidget_ = nullptr;  // contenedor de la barra de estado
    QLabel*          statusDot_   = nullptr;  // indicador de estado (cian/verde/rojo, pulsa al correr)
    QLabel*          statusLabel_;     // compilation messages
    QLabel*          cursorPosLabel_;  // "Ln X, Col Y"
    QTimer*          pulseTimer_  = nullptr;  // anima el punto mientras corre el programa
    bool             pulseOn_     = false;
    int              lastStatusKind_ = 0;     // último tipo de estado (para re-temar)

    // State
    QString          currentFile_;
    Theme::Id        currentTheme_ = Theme::Id::Classic;
};
