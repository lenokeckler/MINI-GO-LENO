#include "CodeEditor.h"
#include "theme/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QCompleter>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QRegularExpression>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QSet>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>

// FoldData — estado de plegado guardado en cada bloque de texto.
// Vive con el bloque aunque se inserten/borren líneas arriba, por eso
// el plegado sobrevive a la edición.
class FoldData : public QTextBlockUserData {
public:
    bool folded = false;
};

// CompletionDelegate — dibuja cada sugerencia con un badge 3D por categoría
// Categoría (Qt::UserRole): 0=keyword 1=builtin 2=type 3=func 4=var
class CompletionDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(qMax(s.height(), 28));
        return s;
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        p->save();
        const QRect r = opt.rect;
        const int cat = idx.data(Qt::UserRole).toInt();
        static const char* labels[]  = { "keyword", "builtin", "type", "func", "var" };
        const int ci = qBound(0, cat, 4);

        // fondo (selección con degradado)
        if (opt.state & QStyle::State_Selected) {
            QLinearGradient sg(r.topLeft(), r.bottomLeft());
            sg.setColorAt(0, Theme::Selection.lighter(120));
            sg.setColorAt(1, Theme::Selection.darker(120));
            p->fillRect(r, sg);
        } else {
            p->fillRect(r, Theme::BgSurface);
        }

        // nombre de la sugerencia (sin badge, para una vista más limpia)
        const int leftPad = 12;
        QFont nf = opt.font; nf.setBold(false);
        p->setFont(nf);
        p->setPen(Theme::TextPri);
        QRect nameR(r.left() + leftPad, r.top(),
                    r.width() - leftPad - 74, r.height());
        QString name = idx.data(Qt::DisplayRole).toString();
        p->drawText(nameR, Qt::AlignVCenter | Qt::AlignLeft,
                    opt.fontMetrics.elidedText(name, Qt::ElideRight, nameR.width()));

        // etiqueta de categoría (derecha)
        QFont sf = opt.font; sf.setPointSizeF(8);
        p->setFont(sf);
        p->setPen((opt.state & QStyle::State_Selected) ? Theme::TextPri : Theme::TextSec);
        QRect catR(r.right() - 70, r.top(), 64, r.height());
        p->drawText(catR, Qt::AlignVCenter | Qt::AlignRight, QString::fromUtf8(labels[ci]));

        p->restore();
    }
};


// LineNumberArea
LineNumberArea::LineNumberArea(CodeEditor* editor)
    : QWidget(editor), editor_(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(editor_->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    editor_->lineNumberAreaPaintEvent(event);
}

// Clic sobre la columna de flechas → plegar/desplegar esa línea.
void LineNumberArea::mousePressEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    const int foldLeft = width() - editor_->foldMarkerWidth();
    if (pos.x() >= foldLeft) {
        int blk = editor_->blockNumberAtY(pos.y());
        if (blk >= 0) { editor_->toggleFold(blk); return; }
    }
    QWidget::mousePressEvent(event);
}

// Resaltar la flecha bajo el cursor y mostrar mano sobre la columna de plegado.
void LineNumberArea::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    const int foldLeft = width() - editor_->foldMarkerWidth();
    if (pos.x() >= foldLeft) {
        editor_->setFoldHover(editor_->blockNumberAtY(pos.y()));
        setCursor(Qt::PointingHandCursor);
    } else {
        editor_->setFoldHover(-1);
        unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void LineNumberArea::leaveEvent(QEvent* event) {
    editor_->setFoldHover(-1);
    QWidget::leaveEvent(event);
}


// CodeEditor
CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberArea(this);
    lineNumberArea_->setMouseTracking(true);   // para el hover de las flechas de plegado

    // Fuente monoespaciada moderna (Windows 11 trae Cascadia Code / Consolas)
    QFont font;
    font.setFamilies({ "Cascadia Code", "Cascadia Mono", "JetBrains Mono",
                       "Consolas", "Courier New" });
    font.setPointSize(12);
    font.setFixedPitch(true);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setCursorWidth(2);

    // Ancho del tabulador = 4 espacios (para los '\t' de archivos abiertos).
    setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(' '));

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &CodeEditor::onCursorMoved);
    // Recalcular plegado cuando cambia el texto (el estado vive en cada bloque)
    connect(this->document(), &QTextDocument::contentsChanged,
            this, &CodeEditor::applyFolds);

    // ── Autocompletado ──
    keywordList_ = { "package","func","var","type","struct","if","else","for",
                     "switch","case","default","return","break","continue" };
    builtinList_ = { "print","println","append","len","cap" };
    typeList_    = { "int","float64","float32","bool","string","rune","byte",
                     "int32","int64","uint","uint32","uint64","true","false" };

    completionModel_ = new QStandardItemModel(this);
    completer_ = new QCompleter(completionModel_, this);
    completer_->setWidget(this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setWrapAround(false);
    completer_->popup()->setItemDelegate(new CompletionDelegate(completer_->popup()));
    connect(completer_, qOverload<const QString&>(&QCompleter::activated),
            this, &CodeEditor::insertCompletion);
    rebuildCompletionModel();

    applyTheme();                 // fondo, paleta y estilo del popup según el tema
    updateLineNumberAreaWidth(0);
    onCursorMoved();
}

// Re-aplica los colores del tema activo a este editor.
void CodeEditor::applyTheme() {
    QPalette p = palette();
    p.setColor(QPalette::Base,            Theme::BgEditor);
    p.setColor(QPalette::Text,            Theme::TextPri);
    p.setColor(QPalette::Highlight,       Theme::Selection);
    p.setColor(QPalette::HighlightedText, Theme::TextPri);
    setPalette(p);
    setStyleSheet(QString(
        "QPlainTextEdit { background:%1; color:%4; border:none;"
        "  selection-background-color:%2; selection-color:%3; }")
        .arg(Theme::hex(Theme::BgEditor), Theme::hex(Theme::Selection),
             Theme::hex(Theme::TextPri), Theme::hex(Theme::TextPri)));

    applyCompleterStyle();

    // Regenerar los overlays efímeros con los colores del tema nuevo
    // (sus formatos quedan "horneados" al crearse, hay que rehacerlos).
    matchBrackets();
    if (!lastSearchTerm_.isEmpty()) setSearchMatches(lastSearchTerm_);
    setActiveMatch(lastActiveMatch_);

    refreshExtraSelections();     // la línea actual usa Theme::CurrentLine
    viewport()->update();
    lineNumberArea_->update();
}

void CodeEditor::applyCompleterStyle() {
    if (!completer_) return;
    completer_->popup()->setStyleSheet(QString(
        "QListView { background:%1; border:1px solid %2; outline:0; padding:3px; }"
        "QScrollBar:vertical { background:%1; width:10px; margin:0; }"
        "QScrollBar::handle:vertical { background:%3; border-radius:4px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
        .arg(Theme::hex(Theme::BgSurface), Theme::hex(Theme::Border),
             Theme::hex(Theme::TextMuted)));
}

// Ctrl + rueda del ratón = zoom de fuente
void CodeEditor::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) zoomIn(1);
        else                              zoomOut(1);
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

// Autocompletado (Tab) + auto-indentación (Enter) + auto-cerrado de pares
void CodeEditor::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();

    // 1) Popup de autocompletado visible: aceptar (Tab/Enter) / navegar / cerrar
    if (completer_ && completer_->popup()->isVisible()) {
        switch (key) {
            case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Tab: {
                QString sel = completer_->popup()->currentIndex().data().toString();
                if (sel.isEmpty()) sel = completer_->currentCompletion();
                completer_->popup()->hide();
                if (!sel.isEmpty()) insertCompletion(sel);
                event->accept();
                return;
            }
            case Qt::Key_Escape:
                completer_->popup()->hide();
                event->accept();
                return;
            case Qt::Key_Up: case Qt::Key_Down:
            case Qt::Key_PageUp: case Qt::Key_PageDown:
                event->ignore();   // que el popup navegue
                return;
            default: break;
        }
    }

    // 2) Tab: si hay una palabra empezada, abrir el popup; si no, insertar
    //    4 espacios (tabs suaves), consistente con la auto-indentación.
    if (key == Qt::Key_Tab && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor tc = textCursor();
        QString left = tc.block().text().left(tc.positionInBlock());
        static const QRegularExpression tail("[A-Za-z_][A-Za-z0-9_]*$");
        if (tail.match(left).hasMatch()) {
            updateCompleter(true);
            event->accept();
            return;
        }
        tc.insertText("    ");
        setTextCursor(tc);
        event->accept();
        return;
    }

    // 3) Enter: auto-indentación
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        QTextCursor cur = textCursor();
        QString lineText = cur.block().text();
        const int posInBlock = cur.positionInBlock();
        QString indent;
        for (QChar c : lineText) { if (c == ' ' || c == '\t') indent += c; else break; }

        // 3a) Enter entre '{' y '}': expandir a bloque multilínea.
        //     De  "{|};"  pasa a:
        //         {
        //             |
        //         };
        const QChar before = posInBlock > 0 ? lineText.at(posInBlock - 1) : QChar();
        const QChar after  = posInBlock < lineText.size() ? lineText.at(posInBlock) : QChar();
        if (before == '{' && after == '}') {
            cur.insertText("\n" + indent + "    " + "\n" + indent);
            cur.movePosition(QTextCursor::Up);
            cur.movePosition(QTextCursor::EndOfLine);
            setTextCursor(cur);
            return;
        }

        bool extra = lineText.trimmed().endsWith('{');
        QPlainTextEdit::keyPressEvent(event);
        cur = textCursor();
        cur.insertText(indent + (extra ? "    " : ""));
        setTextCursor(cur);
        return;
    }

    // 4) Auto-cerrado de pares
    static const QString opens  = "([{";
    static const QString closes = ")]}";
    QString t = event->text();
    if (t.size() == 1) {
        QChar ch = t.at(0);
        int oi = opens.indexOf(ch);
        // Saltar sobre un cierre ya presente (incluye el ';' auto-insertado
        // tras una llave) en vez de duplicarlo.
        if (closes.contains(ch) || ch == '"' || ch == '`' || ch == '\'' || ch == ';') {
            QTextCursor cur = textCursor();
            QString after = cur.block().text().mid(cur.positionInBlock(), 1);
            if (after == t && !cur.hasSelection()) {
                cur.movePosition(QTextCursor::Right);
                setTextCursor(cur);
                return;
            }
        }
        if (oi >= 0 || ch == '"' || ch == '`') {
            QChar close = (oi >= 0) ? closes.at(oi) : ch;
            // En MiniGo las llaves de bloque terminan en ';' → autocompletar "};".
            QString closeStr = (ch == '{') ? QStringLiteral("};") : QString(close);
            QTextCursor cur = textCursor();
            if (cur.hasSelection()) {
                QString sel = cur.selectedText();
                cur.insertText(ch + sel + closeStr);
            } else {
                cur.insertText(QString(ch) + closeStr);
                cur.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor,
                                 closeStr.length());
                setTextCursor(cur);
            }
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(event);

    // 5) Al completar la palabra 'else' justo tras un '};' (el ';' que se
    //    auto-insertó con la llave), quitar ese ';'. En MiniGo  '} else'
    //    no lleva ';', así que borrarlo siempre es correcto.
    if (!event->text().isEmpty() && event->text().at(0).isLetter()) {
        QTextCursor c = textCursor();
        const QString left = c.block().text().left(c.positionInBlock());
        static const QRegularExpression elseRe("\\};[ \\t]*else$");
        auto m = elseRe.match(left);
        if (m.hasMatch()) {
            QTextCursor del(document());
            del.setPosition(c.block().position() + m.capturedStart() + 1); // el ';'
            del.deleteChar();
        }
    }

    // 6) Auto-mostrar el popup al escribir texto normal
    if (!event->text().isEmpty() && event->text().at(0).isPrint())
        updateCompleter(false);
}

// Reconstruye el modelo con categorías: keywords/builtins/tipos + identificadores del doc
void CodeEditor::rebuildCompletionModel() {
    completionModel_->clear();
    QSet<QString> added;
    auto add = [&](const QString& w, int cat) {
        if (w.isEmpty() || added.contains(w)) return;
        added.insert(w);
        auto* item = new QStandardItem(w);
        item->setData(cat, Qt::UserRole);
        item->setEditable(false);
        completionModel_->appendRow(item);
    };

    for (const QString& k : keywordList_) add(k, 0);
    for (const QString& b : builtinList_) add(b, 1);
    for (const QString& t : typeList_)    add(t, 2);

    const QString text = toPlainText();

    // nombres de funciones declaradas:  func NAME(
    QSet<QString> funcNames;
    static const QRegularExpression funcRe("\\bfunc\\s+([A-Za-z_][A-Za-z0-9_]*)");
    auto fit = funcRe.globalMatch(text);
    while (fit.hasNext()) funcNames.insert(fit.next().captured(1));

    // resto de identificadores del documento (var/otros)
    static const QRegularExpression idRe("[A-Za-z_][A-Za-z0-9_]*");
    auto it = idRe.globalMatch(text);
    while (it.hasNext()) {
        QString w = it.next().captured();
        if (w.length() < 3 || added.contains(w)) continue;
        add(w, funcNames.contains(w) ? 3 : 4);
    }
}

// Muestra/actualiza el popup según la palabra bajo el cursor
void CodeEditor::updateCompleter(bool force) {
    if (!completer_) return;
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    QString prefix = tc.selectedText();

    if (!force && prefix.length() < 2) { completer_->popup()->hide(); return; }

    rebuildCompletionModel();

    if (prefix != completer_->completionPrefix()) {
        completer_->setCompletionPrefix(prefix);
        completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
    }
    int n = completer_->completionCount();
    if (n == 0 || (n == 1 && completer_->currentCompletion() == prefix)) {
        completer_->popup()->hide();
        return;
    }
    QRect cr = cursorRect();
    int w = completer_->popup()->sizeHintForColumn(0) +
            completer_->popup()->verticalScrollBar()->sizeHint().width() + 150;
    cr.setWidth(qMax(w, 280));   // espacio para el badge y la etiqueta de categoría
    completer_->complete(cr);
}

// Inserta la sugerencia elegida reemplazando la palabra bajo el cursor
void CodeEditor::insertCompletion(const QString& completion) {
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    tc.insertText(completion);
    setTextCursor(tc);
}

int CodeEditor::foldMarkerWidth() const {
    return 16;   // columna para las flechas ▾/▸ de plegado
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; digits++; }
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits
             + foldMarkerWidth();
}

// ============================================================
// Code folding
// ============================================================

// Recalcula el mapa apertura→cierre de todos los bloques { } del documento,
// ignorando llaves dentro de strings y comentarios.
void CodeEditor::recomputeFoldRanges() {
    foldRanges_.clear();
    const QString text = toPlainText();

    QList<int> openStack;          // pila de números de bloque con '{' abierto
    int blockNo = 0;
    enum { Normal, LineCmt, BlockCmt, Str, Raw, Chr } st = Normal;

    for (int i = 0; i < text.size(); ++i) {
        QChar c = text.at(i);
        QChar n = (i + 1 < text.size()) ? text.at(i + 1) : QChar();

        if (c == '\n') {
            if (st == LineCmt) st = Normal;
            blockNo++;
            continue;
        }
        switch (st) {
            case LineCmt: break;
            case BlockCmt: if (c == '*' && n == '/') { st = Normal; ++i; } break;
            case Str: if (c == '\\') ++i; else if (c == '"')  st = Normal; break;
            case Raw: if (c == '`')  st = Normal; break;
            case Chr: if (c == '\\') ++i; else if (c == '\'') st = Normal; break;
            case Normal:
                if      (c == '/' && n == '/') { st = LineCmt; ++i; }
                else if (c == '/' && n == '*') { st = BlockCmt; ++i; }
                else if (c == '"')  st = Str;
                else if (c == '`')  st = Raw;
                else if (c == '\'') st = Chr;
                else if (c == '{')  openStack.push_back(blockNo);
                else if (c == '}' && !openStack.isEmpty()) {
                    int open = openStack.takeLast();
                    if (blockNo > open) foldRanges_.insert(open, blockNo); // solo si abarca >1 línea
                }
                break;
        }
    }
}

bool CodeEditor::isFoldStart(const QTextBlock& block, int* endBlockOut) const {
    auto it = foldRanges_.constFind(block.blockNumber());
    if (it == foldRanges_.constEnd()) return false;
    if (endBlockOut) *endBlockOut = it.value();
    return true;
}

bool CodeEditor::isFolded(const QTextBlock& block) const {
    auto* fd = static_cast<FoldData*>(block.userData());
    return fd && fd->folded;
}

void CodeEditor::toggleFold(int blockNumber) {
    QTextBlock b = document()->findBlockByNumber(blockNumber);
    if (!b.isValid() || !isFoldStart(b)) return;
    auto* fd = static_cast<FoldData*>(b.userData());
    if (!fd) { fd = new FoldData(); b.setUserData(fd); }
    fd->folded = !fd->folded;
    applyFolds();
}

// Aplica visibilidad: un bloque es visible salvo que esté dentro de un
// bloque plegado. Recalcula desde cero para soportar plegados anidados.
void CodeEditor::applyFolds() {
    if (applyingFolds_) return;
    applyingFolds_ = true;

    recomputeFoldRanges();

    QTextDocument* doc = document();
    // Todos visibles de entrada
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        b.setVisible(true);

    // Ocultar el interior de cada bloque plegado (start+1 .. end)
    for (auto it = foldRanges_.constBegin(); it != foldRanges_.constEnd(); ++it) {
        QTextBlock start = doc->findBlockByNumber(it.key());
        if (!isFolded(start)) continue;
        QTextBlock b = start.next();
        int endNo = it.value();
        while (b.isValid() && b.blockNumber() <= endNo) {
            b.setVisible(false);
            b = b.next();
        }
    }

    // Forzar relayout: marcar dirty para que QPlainTextDocumentLayout
    // recalcule alturas y la barra de desplazamiento.
    doc->markContentsDirty(0, doc->characterCount());
    updateLineNumberAreaWidth(0);
    viewport()->update();
    lineNumberArea_->update();

    applyingFolds_ = false;
}

void CodeEditor::setFoldHover(int blockNumber) {
    if (foldHover_ == blockNumber) return;
    foldHover_ = blockNumber;
    lineNumberArea_->update();
}

// Devuelve el número de bloque visible bajo la coordenada y (o -1).
int CodeEditor::blockNumberAtY(int y) const {
    QTextBlock block = firstVisibleBlock();
    int blockNumber  = block.blockNumber();
    int top    = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid()) {
        if (block.isVisible() && y >= top && y <= bottom)
            return blockNumber;
        block  = block.next();
        top    = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        blockNumber++;
        if (top > height()) break;
    }
    return -1;
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy)
        lineNumberArea_->scroll(0, dy);
    else
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(),
                                       lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::onCursorMoved() {
    matchBrackets();
    refreshExtraSelections();
}

// Combina todas las capas de resaltado en un solo setExtraSelections.
void CodeEditor::refreshExtraSelections() {
    QList<QTextEdit::ExtraSelection> all;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(Theme::CurrentLine);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        all.append(sel);
    }
    all += errorSel_;
    all += searchSel_;
    all += activeMatchSel_;   // sobre las demás coincidencias
    all += bracketSel_;
    setExtraSelections(all);
}

void CodeEditor::setActiveMatch(const QTextCursor& cur) {
    lastActiveMatch_ = cur;
    activeMatchSel_.clear();
    if (!cur.isNull() && cur.hasSelection()) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(Theme::ActiveBg);   // coincidencia activa (enfocada)
        s.format.setForeground(Theme::ActiveFg);
        s.cursor = cur;
        activeMatchSel_.append(s);
    }
    refreshExtraSelections();
}

// Resalta el paréntesis/llave/corchete pareja del que está junto al cursor.
void CodeEditor::matchBrackets() {
    bracketSel_.clear();
    static const QString opens  = "({[";
    static const QString closes = ")}]";
    const QString text = document()->toPlainText();
    int pos = textCursor().position();

    auto tryAt = [&](int p) -> bool {
        if (p < 0 || p >= text.size()) return false;
        QChar c = text.at(p);
        int oi = opens.indexOf(c), ci = closes.indexOf(c);
        int dir = 0, target = -1;
        if (oi >= 0)      { dir = 1;  target = closes.at(oi).unicode(); }
        else if (ci >= 0) { dir = -1; target = opens.at(ci).unicode(); }
        else return false;
        QChar match(target);
        int depth = 0;
        for (int i = p; i >= 0 && i < text.size(); i += dir) {
            QChar ch = text.at(i);
            if (ch == c) depth++;
            else if (ch == match) { depth--; if (depth == 0) {
                for (int q : {p, i}) {
                    QTextEdit::ExtraSelection s;
                    s.format.setBackground(Theme::BracketBg);
                    s.format.setForeground(Theme::BracketFg);
                    QTextCursor cur(document());
                    cur.setPosition(q); cur.setPosition(q + 1, QTextCursor::KeepAnchor);
                    s.cursor = cur;
                    bracketSel_.append(s);
                }
                return true;
            }}
        }
        return false;
    };
    // probar el carácter en el cursor y el anterior
    if (!tryAt(pos)) tryAt(pos - 1);
}

void CodeEditor::setSearchMatches(const QString& term) {
    lastSearchTerm_ = term;
    searchSel_.clear();
    if (!term.isEmpty()) {
        QTextDocument* d = document();
        QTextCursor c(d);
        while (true) {
            c = d->find(term, c);
            if (c.isNull()) break;
            QTextEdit::ExtraSelection s;
            s.format.setBackground(Theme::SearchBg);
            s.format.setForeground(Theme::SearchFg);
            s.cursor = c;
            searchSel_.append(s);
        }
    }
    refreshExtraSelections();
}

void CodeEditor::clearSearchMatches() {
    lastSearchTerm_.clear();
    lastActiveMatch_ = QTextCursor();
    searchSel_.clear();
    activeMatchSel_.clear();
    refreshExtraSelections();
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), Theme::Gutter);

    const int foldW    = foldMarkerWidth();
    const int numRight = lineNumberArea_->width() - foldW;   // zona de números
    const int curLine  = textCursor().blockNumber();

    QTextBlock block = firstVisibleBlock();
    int blockNumber  = block.blockNumber();
    int top    = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int h = fontMetrics().height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            // Marcador de error: punto de color en el margen izquierdo
            auto mk = errorMarkers_.find(blockNumber + 1);
            if (mk != errorMarkers_.end()) {
                int d = qMax(6, h / 2);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(Qt::NoPen);
                painter.setBrush(mk.value());
                painter.drawEllipse(3, top + (h - d) / 2, d, d);
                painter.setRenderHint(QPainter::Antialiasing, false);
            }

            // Número de línea (la línea activa resalta en cian)
            const bool active = (blockNumber == curLine);
            painter.setPen(active ? Theme::Go : Theme::TextMuted);
            painter.drawText(0, top, numRight - 4, h, Qt::AlignRight,
                             QString::number(blockNumber + 1));

            // Flecha de plegado si el bloque abre una región { }
            if (isFoldStart(block)) {
                const bool folded  = isFolded(block);
                const bool hovered = (blockNumber == foldHover_);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(Qt::NoPen);
                painter.setBrush(hovered ? Theme::Go
                                         : (folded ? Theme::Orange : Theme::TextSec));
                QPointF c(numRight + foldW / 2.0, top + h / 2.0);
                double s = 3.4;   // medio-tamaño del triángulo
                QPainterPath tri;
                if (folded) {                         // ▸ apunta a la derecha
                    tri.moveTo(c.x() - s, c.y() - s);
                    tri.lineTo(c.x() + s, c.y());
                    tri.lineTo(c.x() - s, c.y() + s);
                } else {                              // ▾ apunta hacia abajo
                    tri.moveTo(c.x() - s, c.y() - s);
                    tri.lineTo(c.x() + s, c.y() - s);
                    tri.lineTo(c.x(),     c.y() + s);
                }
                tri.closeSubpath();
                painter.drawPath(tri);
                painter.setRenderHint(QPainter::Antialiasing, false);
            }
        }
        block  = block.next();
        top    = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        blockNumber++;
    }
}

void CodeEditor::highlightErrorLine(int line, QColor color) {
    QTextEdit::ExtraSelection sel;
    sel.format.setBackground(color);
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);

    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
    sel.cursor = cursor;
    errorSel_.append(sel);

    // Marcador en el gutter (versión más viva, opaca, del color de la línea).
    // Se ignora el alpha del fondo para que el punto siempre sea bien visible.
    QColor mark(color.red(), color.green(), color.blue());
    errorMarkers_.insert(line, mark.lighter(230));

    refreshExtraSelections();
    lineNumberArea_->update();
}

void CodeEditor::clearErrorHighlights() {
    errorSel_.clear();
    errorMarkers_.clear();
    refreshExtraSelections();
    lineNumberArea_->update();
}
