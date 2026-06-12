#include "highlight/MiniGoHighlighter.h"
#include "theme/Theme.h"

#include <QFont>

MiniGoHighlighter::MiniGoHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    buildRules();
}

void MiniGoHighlighter::rebuild() {
    buildRules();
    rehighlight();
}

void MiniGoHighlighter::buildRules() {
    rules_.clear();
    HighlightRule rule;

    // Keywords — morado
    QTextCharFormat kwFmt;
    kwFmt.setForeground(Theme::SynKeyword);
    kwFmt.setFontWeight(QFont::Bold);
    QStringList keywords = {
        "\\bpackage\\b", "\\bfunc\\b", "\\bvar\\b", "\\btype\\b", "\\bstruct\\b",
        "\\bif\\b", "\\belse\\b", "\\bfor\\b", "\\bswitch\\b", "\\bcase\\b",
        "\\bdefault\\b", "\\breturn\\b", "\\bbreak\\b", "\\bcontinue\\b",
        "\\bappend\\b", "\\blen\\b", "\\bcap\\b", "\\bprint\\b", "\\bprintln\\b"
    };
    for (const auto& kw : keywords) {
        rule.pattern = QRegularExpression(kw);
        rule.format  = kwFmt;
        rules_.push_back(rule);
    }

    // Tipos — cian de Go
    QTextCharFormat typeFmt;
    typeFmt.setForeground(Theme::SynType);
    QStringList types = {
        "\\bint\\b", "\\bfloat64\\b", "\\bbool\\b", "\\bstring\\b",
        "\\brune\\b", "\\bbyte\\b", "\\bint32\\b", "\\bint64\\b"
    };
    for (const auto& t : types) {
        rule.pattern = QRegularExpression(t);
        rule.format  = typeFmt;
        rules_.push_back(rule);
    }

    // Strings interpretadas — verde
    QTextCharFormat strFmt;
    strFmt.setForeground(Theme::SynString);
    rule.pattern = QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\"");
    rule.format  = strFmt;
    rules_.push_back(rule);

    // Raw strings — mismo color
    rule.pattern = QRegularExpression("`[^`]*`");
    rule.format  = strFmt;
    rules_.push_back(rule);

    // Rune literals
    rule.pattern = QRegularExpression("'(?:[^'\\\\]|\\\\.)'");
    rule.format  = strFmt;
    rules_.push_back(rule);

    // Números — naranja
    QTextCharFormat numFmt;
    numFmt.setForeground(Theme::SynNumber);
    rule.pattern = QRegularExpression("\\b\\d+\\.?\\d*([eE][+-]?\\d+)?\\b");
    rule.format  = numFmt;
    rules_.push_back(rule);

    // Comentarios de línea — gris azulado itálico
    QTextCharFormat commentFmt;
    commentFmt.setForeground(Theme::SynComment);
    commentFmt.setFontItalic(true);
    rule.pattern = QRegularExpression("//[^\n]*");
    rule.format  = commentFmt;
    rules_.push_back(rule);

    // Comentarios de bloque
    blockCommentFormat_ = commentFmt;
    blockCommentStart_  = QRegularExpression("/\\*");
    blockCommentEnd_    = QRegularExpression("\\*/");
}

void MiniGoHighlighter::highlightBlock(const QString& text) {
    for (const auto& rule : rules_) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }

    // Comentarios de bloque multi-línea
    setCurrentBlockState(0);
    int start = 0;
    if (previousBlockState() != 1)
        start = text.indexOf(blockCommentStart_);

    while (start >= 0) {
        auto endMatch = blockCommentEnd_.match(text, start);
        int end, len;
        if (endMatch.hasMatch()) {
            end = endMatch.capturedEnd();
            len = end - start;
            setCurrentBlockState(0);
        } else {
            setCurrentBlockState(1);
            len = text.length() - start;
        }
        setFormat(start, len, blockCommentFormat_);
        start = text.indexOf(blockCommentStart_, start + len);
    }
}
