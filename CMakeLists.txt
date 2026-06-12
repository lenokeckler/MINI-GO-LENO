cmake_minimum_required(VERSION 3.20)
project(MiniGoCompiler C CXX)

set(CMAKE_PREFIX_PATH "C:/msys64/ucrt64")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# ANTLR4 C++ Runtime (instalado via pacman en UCRT64)
# ============================================================

find_package(antlr4-runtime REQUIRED)

# Normalizar el nombre de la variable de includes según lo que exporte el paquete
if(TARGET antlr4-runtime)
    get_target_property(_a4_incs antlr4-runtime INTERFACE_INCLUDE_DIRECTORIES)
    if(_a4_incs)
        set(ANTLR4_INCLUDE_DIRS "${_a4_incs}")
    endif()
endif()
if(NOT ANTLR4_INCLUDE_DIRS)
    set(ANTLR4_INCLUDE_DIRS "C:/msys64/ucrt64/include/antlr4-runtime")
endif()
set(ANTLR4_LIB antlr4-runtime)

message(STATUS "ANTLR4 includes: ${ANTLR4_INCLUDE_DIRS}")

# ============================================================
# Generación de fuentes ANTLR4 (requiere Java + antlr4 JAR)
# ============================================================

find_program(JAVA_EXECUTABLE java
    HINTS
        "$ENV{JAVA_HOME}/bin"
        "C:/Program Files/Common Files/Oracle/Java/javapath"
        "C:/Program Files/Java/jdk-21/bin"
        "$ENV{PATH}"
    DOC "Java runtime para ejecutar el JAR de ANTLR4"
)
if(JAVA_EXECUTABLE)
    message(STATUS "Java encontrado: ${JAVA_EXECUTABLE}")
else()
    message(WARNING "Java no encontrado — la generación de ANTLR4 fallará")
    set(JAVA_EXECUTABLE "java")
endif()

if(NOT ANTLR4_JAR)
    set(ANTLR4_JAR "${CMAKE_SOURCE_DIR}/antlr-4.13.2-complete.jar")
endif()

set(ANTLR4_GEN_DIR ${CMAKE_BINARY_DIR}/antlr4_generated)
file(MAKE_DIRECTORY ${ANTLR4_GEN_DIR})

set(ANTLR4_GENERATED_SOURCES
    ${ANTLR4_GEN_DIR}/MiniGoLexer.cpp
    ${ANTLR4_GEN_DIR}/MiniGoParser.cpp
)
set(ANTLR4_GENERATED_HEADERS
    ${ANTLR4_GEN_DIR}/MiniGoLexer.h
    ${ANTLR4_GEN_DIR}/MiniGoParser.h
    ${ANTLR4_GEN_DIR}/MiniGoBaseVisitor.h
    ${ANTLR4_GEN_DIR}/MiniGoVisitor.h
)

if(EXISTS "${ANTLR4_JAR}")
    add_custom_command(
        OUTPUT  ${ANTLR4_GENERATED_SOURCES} ${ANTLR4_GENERATED_HEADERS}
        COMMAND "${JAVA_EXECUTABLE}" -jar "${ANTLR4_JAR}"
                    -Dlanguage=Cpp
                    -visitor
                    -no-listener
                    -o ${ANTLR4_GEN_DIR}
                    ${CMAKE_SOURCE_DIR}/grammar/MiniGo.g4
        DEPENDS ${CMAKE_SOURCE_DIR}/grammar/MiniGo.g4
        COMMENT "Generando fuentes ANTLR4 C++ desde MiniGo.g4"
        VERBATIM
    )
else()
    message(WARNING "No se encontró el JAR de ANTLR4 en: ${ANTLR4_JAR}")
    foreach(f ${ANTLR4_GENERATED_SOURCES} ${ANTLR4_GENERATED_HEADERS})
        if(NOT EXISTS ${f})
            file(WRITE ${f} "// placeholder\n")
        endif()
    endforeach()
endif()

add_custom_target(antlr4_generate_target
    DEPENDS ${ANTLR4_GENERATED_SOURCES}
)

# ============================================================
# LLVM
# ============================================================

find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM encontrado: ${LLVM_PACKAGE_VERSION}")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(LLVM_LIBS
    core support native irreader
    codegen mc target
)

# ============================================================
# Fuentes del compilador
# ============================================================

set(COMPILER_SRCS
    src/typechecker/TypeChecker.cpp
    src/encoder/Encoder.cpp
    ${ANTLR4_GENERATED_SOURCES}
)

set(COMPILER_INCLUDES
    ${ANTLR4_INCLUDE_DIRS}
    ${ANTLR4_GEN_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

# ============================================================
# Ejecutable CLI: minigoc
# ============================================================

add_executable(minigoc src/main.cpp ${COMPILER_SRCS})
add_dependencies(minigoc antlr4_generate_target)
target_include_directories(minigoc PRIVATE ${COMPILER_INCLUDES})
target_link_libraries(minigoc PRIVATE ${ANTLR4_LIB} ${LLVM_LIBS})

# ============================================================
# Qt6 — Editor gráfico (GUI)
# ============================================================

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(EDITOR_SRCS
    src/editor/Editor.cpp
    src/editor/CodeEditor.cpp
    src/editor/highlight/MiniGoHighlighter.cpp
    src/editor/highlight/IRHighlighter.cpp
    src/editor/theme/Theme.cpp
)

add_executable(minigo_editor ${EDITOR_SRCS} ${COMPILER_SRCS})
add_dependencies(minigo_editor antlr4_generate_target)

set_target_properties(minigo_editor PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE TRUE
)

target_include_directories(minigo_editor PRIVATE ${COMPILER_INCLUDES} ${CMAKE_SOURCE_DIR}/src/editor)
target_link_libraries(minigo_editor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${ANTLR4_LIB}
    ${LLVM_LIBS}
)

# ============================================================
# Tests
# ============================================================

enable_testing()

set(TEST_FILES
    tests/01_vars.go
    tests/02_operators.go
    tests/03_control.go
    tests/04_functions.go
    tests/05_arrays.go
    tests/06_slices.go
    tests/07_strings.go
    tests/08_structs.go
    tests/09_types.go
    tests/10_new_features.go
    tests/13_errors.go
)

foreach(TEST_FILE ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    if(EXISTS ${CMAKE_SOURCE_DIR}/${TEST_FILE})
        add_test(NAME ${TEST_NAME}
                 COMMAND minigoc ${CMAKE_SOURCE_DIR}/${TEST_FILE}
                 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    endif()
endforeach()
