#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <vector>

// ============================================================
// IRHighlighter — resaltado de sintaxis para LLVM IR.
// Toma sus colores de Theme; rebuild() recolorea al cambiar de tema.
// ============================================================

class IRHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit IRHighlighter(QTextDocument* parent = nullptr);

    void rebuild();

protected:
    void highlightBlock(const QString& text) override;

private:
    void buildRules();

    struct Rule { QRegularExpression pattern; QTextCharFormat format; };
    std::vector<Rule> rules_;
};
