#include "antlr4-runtime.h"
#include "MiniGoLexer.h"
#include "MiniGoParser.h"
#include "typechecker/TypeChecker.h"
#include "encoder/Encoder.h"
#include "errors/MiniGoErrorListener.h"

#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace antlr4;


// main
int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Uso: minigoc <archivo.go>\n";
        return 1;
    }

    string filename = argv[1];

    // 1. Lectura del archivo fuente
    ANTLRFileStream input;
    try {
        input.loadFromFile(filename);
    } catch (const exception& e) {
        cerr << "Error al abrir '" << filename << "': " << e.what() << "\n";
        return 1;
    }

    // 2. Lexer
    MiniGoLexer lexer(&input);
    CommonTokenStream tokens(&lexer);

    // 3. Parser
    MiniGoParser parser(&tokens);

    // 4. Error listener profesional (separa scanner / parser, limita cascada)
    MiniGoSyntaxErrorListener errListener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errListener);
    parser.removeErrorListeners();
    parser.addErrorListener(&errListener);

    // Estrategia de recuperación de errores de ANTLR4 (DefaultErrorStrategy):
    // usa follow-sets de la gramática para sincronizar tras un error. La calidad
    // de los mensajes (línea/columna, "did you mean?") la aporta el listener.
    parser.setErrorHandler(std::make_shared<MiniGoErrorStrategy>());

    // 5. Parse → AST
    tree::ParseTree* tree = parser.root();

    if (errListener.hasErrors()) {
        for (const auto& e : errListener.getErrors())
            cout << e << "\n";
        cout << "Compilation failed!\n";
        return 1;
    }

    // 6. TypeChecker
    TypeChecker tc;
    tc.visit(tree);

    if (tc.hasErrors()) {
        for (const auto& e : tc.getErrors())
            cout << e << "\n";
        cout << "Compilation failed!\n";
        return 1;
    }

    // 7. Encoder — LLVM IR + objeto x86
    Encoder encoder(tc.getReferenceMap(), tc.getDeclSymbolMap());
    encoder.visit(tree);
    if (!encoder.finalize()) {
        cout << "CODEGEN ERROR: generated IR failed verification:\n"
             << encoder.getVerifyError() << "\n";
        cout << "Compilation failed!\n";
        return 1;
    }

    cout << "Compilation successful!\n";
    return 0;
}
