#include "highlight/IRHighlighter.h"
#include "theme/Theme.h"

#include <QFont>

IRHighlighter::IRHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    buildRules();
}

void IRHighlighter::rebuild() {
    buildRules();
    rehighlight();
}

void IRHighlighter::buildRules() {
    rules_.clear();
    Rule r;
    // Instrucciones / keywords del IR
    QTextCharFormat kw; kw.setForeground(Theme::SynKeyword); kw.setFontWeight(QFont::Bold);
    QStringList kws = {
        "define","declare","ret","br","call","store","load","alloca","getelementptr",
        "icmp","fcmp","phi","add","fadd","sub","fsub","mul","fmul","sdiv","fdiv","srem",
        "frem","and","or","xor","shl","ashr","lshr","select","sext","trunc","zext",
        "bitcast","sitofp","fptosi","insertvalue","extractvalue","global","constant",
        "private","unnamed_addr","external","label","eq","ne","slt","sle","sgt","sge",
        "inbounds","nsw","nuw","to","align","source_filename"
    };
    for (const auto& k : kws) {
        r.pattern = QRegularExpression("\\b" + k + "\\b"); r.format = kw; rules_.push_back(r);
    }
    // Tipos
    QTextCharFormat ty; ty.setForeground(Theme::SynType);
    r.pattern = QRegularExpression("\\b(i1|i8|i16|i32|i64|double|float|void|ptr)\\b");
    r.format = ty; rules_.push_back(r);
    // Identificadores globales @nombre
    QTextCharFormat gl; gl.setForeground(Theme::Yellow);
    r.pattern = QRegularExpression("@[A-Za-z0-9_.]+"); r.format = gl; rules_.push_back(r);
    // Identificadores locales %nombre
    QTextCharFormat lo; lo.setForeground(Theme::Go);
    r.pattern = QRegularExpression("%[A-Za-z0-9_.]+"); r.format = lo; rules_.push_back(r);
    // Números
    QTextCharFormat nm; nm.setForeground(Theme::SynNumber);
    r.pattern = QRegularExpression("\\b-?\\d+(\\.\\d+)?(e[+-]?\\d+)?\\b"); r.format = nm; rules_.push_back(r);
    // Strings
    QTextCharFormat st; st.setForeground(Theme::SynString);
    r.pattern = QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\""); r.format = st; rules_.push_back(r);
    // Comentarios ; ...
    QTextCharFormat cm; cm.setForeground(Theme::SynComment); cm.setFontItalic(true);
    r.pattern = QRegularExpression(";[^\n]*"); r.format = cm; rules_.push_back(r);
}

void IRHighlighter::highlightBlock(const QString& text) {
    for (const auto& r : rules_) {
        auto it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), r.format);
        }
    }
}
