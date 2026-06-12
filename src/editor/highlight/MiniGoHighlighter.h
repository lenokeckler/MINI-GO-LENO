#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <vector>

// ============================================================
// MiniGoHighlighter — resaltado de sintaxis para MiniGo.
// Los colores se toman de Theme, por lo que rebuild() permite
// recolorear cuando cambia el tema.
// ============================================================

class MiniGoHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MiniGoHighlighter(QTextDocument* parent = nullptr);

    // Reconstruye reglas con los colores del tema activo y re-resalta.
    void rebuild();

protected:
    void highlightBlock(const QString& text) override;

private:
    void buildRules();

    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };
    std::vector<HighlightRule> rules_;
    QRegularExpression         blockCommentStart_;
    QRegularExpression         blockCommentEnd_;
    QTextCharFormat            blockCommentFormat_;
};
