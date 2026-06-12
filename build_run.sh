#!/bin/bash
export MSYSTEM=UCRT64
source /etc/profile
PROJ="/c/Users/lenok/Desktop/TEC Semestre I/Compiladores/1. PROYECTO/MiniGoCompiler/minigo"
cd "$PROJ/build"
cmake --build . -j4 2>&1 | grep -v "^\[" | grep -v "^<command-line>"
echo "--- EJECUTANDO test_vars.go ---"
"$PROJ/build/minigoc.exe" "$PROJ/tests/test_vars.go" 2>&1
