#pragma once

#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextCursor>
#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QList>

class QCompleter;
class QStandardItemModel;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QKeyEvent;
class QMouseEvent;
class QEvent;

// ============================================================
// LineNumberArea — margen de números de línea + flechas de plegado.
// ============================================================

class CodeEditor;

class LineNumberArea : public QWidget {
    Q_OBJECT
public:
    explicit LineNumberArea(CodeEditor* editor);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;   // clic en flecha de plegado
    void mouseMoveEvent(QMouseEvent* event) override;     // resaltado al pasar el mouse
    void leaveEvent(QEvent* event) override;
private:
    CodeEditor* editor_;
};

// ============================================================
// CodeEditor — QPlainTextEdit con números de línea y resaltado.
// ============================================================

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int  lineNumberAreaWidth() const;
    int  foldMarkerWidth() const;                     // ancho reservado para las flechas ▾/▸

    // Re-aplica colores del tema activo (paleta, fondo, popup de autocompletado).
    void applyTheme();

    // Highlight a line with a colored background to mark errors
    void highlightErrorLine(int line, QColor color = QColor(120, 30, 30));
    void clearErrorHighlights();

    // ── Code folding (plegar/desplegar funciones y bloques { }) ──
    void toggleFold(int blockNumber);                 // pliega/despliega en esa línea
    bool isFoldStart(const QTextBlock& block,
                     int* endBlockOut = nullptr) const;  // ¿la línea abre un bloque plegable?
    bool isFolded(const QTextBlock& block) const;     // ¿está plegada ahora?
    void setFoldHover(int blockNumber);               // -1 = ninguna (resaltado de mouse)
    int  foldHover() const { return foldHover_; }
    int  blockNumberAtY(int y) const;                 // qué línea está bajo la coordenada y

    // Resaltar todas las coincidencias de búsqueda
    void setSearchMatches(const QString& term);
    void clearSearchMatches();
    void setActiveMatch(const QTextCursor& cur);      // resalta la coincidencia activa distinto

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;     // Ctrl + rueda = zoom
    void keyPressEvent(QKeyEvent* event) override;    // auto-indent + auto-cerrar pares

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void onCursorMoved();                             // línea actual + brackets
    void updateLineNumberArea(const QRect& rect, int dy);
    void insertCompletion(const QString& completion);

private:
    void refreshExtraSelections();                    // combina todas las capas
    void matchBrackets();
    void updateCompleter(bool force);                 // muestra/actualiza el popup
    void rebuildCompletionModel();                    // keywords + identificadores del doc
    void applyCompleterStyle();                       // estilo del popup según el tema

    // ── Folding interno ──
    void recomputeFoldRanges();                        // recalcula apertura→cierre de bloques
    void applyFolds();                                 // aplica visibilidad según estado plegado

    QHash<int, int> foldRanges_;                       // bloque de apertura -> bloque de cierre
    int             foldHover_   = -1;                 // línea con la flecha resaltada
    bool            applyingFolds_ = false;            // evita reentrancia

    LineNumberArea* lineNumberArea_;
    QCompleter*        completer_       = nullptr;
    QStandardItemModel* completionModel_ = nullptr;
    QStringList        keywordList_;                   // palabras reservadas
    QStringList        builtinList_;                   // print/println/append/len/cap
    QStringList        typeList_;                      // tipos primitivos + true/false
    QList<QTextEdit::ExtraSelection> errorSel_;        // líneas con error
    QList<QTextEdit::ExtraSelection> searchSel_;       // coincidencias de búsqueda
    QList<QTextEdit::ExtraSelection> activeMatchSel_;  // coincidencia activa
    QList<QTextEdit::ExtraSelection> bracketSel_;      // paréntesis pareja
    QHash<int, QColor>               errorMarkers_;    // línea (1-based) -> color del punto en gutter

    // Recordados para poder regenerar los overlays al cambiar de tema.
    QString      lastSearchTerm_;
    QTextCursor  lastActiveMatch_;
};
