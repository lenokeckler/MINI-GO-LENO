#pragma once

#include <QColor>
#include <QString>
#include <QList>

// ============================================================
// Theme — paleta de la interfaz, conmutable en tiempo de ejecución.
//
// Los colores son variables globales MUTABLES: cada vez que se aplica
// un tema (Theme::apply) se reasignan en sitio, de modo que cualquier
// codigo que ya lee "Theme::BgPanel" recibe automaticamente el color
// del tema activo. Los estilos (setStyleSheet) hay que RE-aplicarlos
// para que el cambio sea visible en vivo (ver MainWindow::applyTheme).
// ============================================================

namespace Theme {

enum class Id { Classic, Dark, Light, Go };

// ── Colores activos (se reasignan al cambiar de tema) ──
// Fondos
extern QColor BgDeep;       // ventana / barras
extern QColor BgPanel;      // paneles
extern QColor BgSurface;    // superficies (tabs, inputs)
extern QColor BgHover;      // hover
extern QColor BgEditor;     // fondo del editor
extern QColor Gutter;       // margen de numeros
extern QColor CurrentLine;  // linea activa
extern QColor Border;
// Texto
extern QColor TextPri;
extern QColor TextSec;
extern QColor TextMuted;
// Acentos
extern QColor Go;           // color de marca (cian de Go en el tema clasico)
extern QColor Green;
extern QColor Red;
extern QColor Orange;
extern QColor Yellow;
extern QColor Purple;
// Selecciones / menus
extern QColor Selection;    // seleccion de texto en el editor
extern QColor MenuSel;      // item de menu resaltado
// Overlays efimeros del editor (fondo + texto)
extern QColor SearchBg;     // coincidencias de busqueda
extern QColor SearchFg;
extern QColor ActiveBg;     // coincidencia activa (la enfocada)
extern QColor ActiveFg;
extern QColor BracketBg;    // pareja de parentesis/llave
extern QColor BracketFg;
// Sintaxis (MiniGo + IR)
extern QColor SynKeyword;
extern QColor SynType;
extern QColor SynString;
extern QColor SynNumber;
extern QColor SynComment;

// Aplica un tema reasignando todos los colores de arriba.
void apply(Id id);

// Tema activo.
Id current();

// Nombre legible para menus.
QString name(Id id);

// Lista de temas disponibles, en orden de presentacion.
QList<Id> all();

// Helper para hojas de estilo (formato #rrggbb).
inline QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

} // namespace Theme
