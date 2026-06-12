package main;
//  GOLEM QUEST  -  un mini RPG de consola escrito en MiniGo
//  PARA PROBAR ESTE MINI JUEGO, HAGA LA PESTAÑA LO MAS GRANDE POSIBLE

//  Como "jugar" (el lenguaje MiniGo no lee teclado, solo imprime):
//    1. Cambia el valor de 'semilla' aqui abajo (cualquier numero).
//    2. Pulsa RUN en el editor.
//    3. Cada semilla genera una aventura distinta: otros golpes,
//       criticos, esquivas, botin y subidas de nivel. Prueba a ver
//       con cual llegas vivo hasta el Golem final.

//  Hecho usando SOLO lo que permite la gramatica actual:
//  structs, arrays, slices (append/len), variables globales, funciones,
//  if/else, for, switch, print/println, aritmetica entera y bits.
//  El azar es un generador propio (LCG). Todo va corrido a la derecha.

type Combatiente struct {
    hp int;
    max int;
    atk int;
};

var semilla int = 7;        // CAMBIA ESTE NUMERO Y VUELVE A DARLE RUN (1 para perder)
var extra int = 72;         // CUANTO SE CORRE TODO A LA DERECHA (sube/baja a gusto)

var rng int = 7;            // estado interno del generador de azar
var heroe Combatiente;      // el protagonista (hp / max / atk)

var nivel int = 1;          // nivel actual del heroe
var exp int = 0;            // experiencia acumulada en el nivel
var oro int = 0;            // monedas recogidas
var pociones int = 1;       // pociones en la mochila
var pocionesUsadas int = 0; // contador para el informe final
var derrotados int = 0;     // enemigos vencidos
var turnos int = 0;         // turnos jugados en total
var registroOro []int;      // botin de cada batalla (slice dinamico)

// Generador pseudo-aleatorio (LCG pequeno, sin overflow)
func siguienteAzar() int {
    rng = (rng * 75 + 74) % 65537;
    if rng < 0 {
        rng = -rng;
    };
    return rng;
};

// Entero al azar en el rango [lo, alto] inclusive
func azarRango(lo int, alto int) int {
    var n int = siguienteAzar();
    return lo + n % (alto - lo + 1);
};

// Imprime n espacios sin salto de linea
func sangria(n int) {
    var i int = 0;
    for i < n {
        print(" ");
        i++;
    };
};

// Sangria estandar que corre cada linea a la derecha
func mas() {
    sangria(extra);
};

// Separador de guiones largos entre etapas (mas ancho que cada cuadro)
func separador() {
    println("");
    sangria(extra - 4);
    var i int = 0;
    for i < 50 {
        print("-");
        i++;
    };
    println("");
    println("");
};

// Dibuja una barra tipo [#####.....] valor, ya corrida
func barra(etiqueta int, vida int, maximo int) {
    mas(); sangria(8);
    if etiqueta == 1 {
        print("HEROE   ");
    } else {
        print("ENEMIGO ");
    };
    var llenos int = vida * 20 / maximo;
    if llenos < 0 {
        llenos = 0;
    };
    if llenos > 20 {
        llenos = 20;
    };
    print("[");
    var i int = 0;
    for i < 20 {
        if i < llenos {
            print("#");
        } else {
            print(".");
        };
        i++;
    };
    print("] ");
    println(vida);
};

// Portada del juego
func banner() {
    println("");
    mas(); println("+========================================+");
    mas(); println("|                                        |");
    mas(); println("|       G O L E M   Q U E S T            |");
    mas(); println("|                                        |");
    mas(); println("|      una mini-aventura en MiniGo       |");
    mas(); println("|     *   *   *   *   *   *   *   *      |");
    mas(); println("+========================================+");
    println("");
};

// Dibujo del heroe
func dibujarHeroe() {
    mas(); println("            ___");
    mas(); println("          ,(o o),        EL HEROE");
    mas(); println("          /  V  \\___     espada + escudo");
    mas(); println("         / |   | \\  )");
    mas(); println("        (  |[#]| )-'     sube de nivel al matar");
    mas(); println("           |___|");
    mas(); println("           /   \\");
    println("");
};

// Animacion: el heroe desciende a la cripta
func descender() {
    mas(); println("El heroe enciende su antorcha...");
    mas(); println("Descendiendo a la cripta .");
    mas(); println("Descendiendo a la cripta . .");
    mas(); println("Descendiendo a la cripta . . .");
    mas(); println("La oscuridad se cierra a su espalda.");
    println("");
};

// Sprite del enemigo segun su id
func sprite(id int) {
    switch id {
        case 1:
            mas(); println("      ___");
            mas(); println("     /. .\\          RATA GIGANTE");
            mas(); println("    =(_Y_)=         colmillos sucios");
            mas(); println("      `-'");
        case 2:
            mas(); println("    /\\   .   /\\      MURCIELAGO");
            mas(); println("   /  \\_/ \\_/  \\     alas membranosas");
            mas(); println("   \\___ vvv ___/");
        case 3:
            mas(); println("     .-====-.        ESQUELETO");
            mas(); println("    ( x    x )       huesos crujientes");
            mas(); println("     |  ||  |");
            mas(); println("     \\ ==== /");
        case 4:
            mas(); println("    ,#======#,       ORCO BRUTO");
            mas(); println("   ( O      O )      musculo y rabia");
            mas(); println("    )  ^^^^  (");
            mas(); println("   /__|    |__\\");
        case 5:
            mas(); println("  [##########]       GOLEM DE PIEDRA");
            mas(); println("  [# (0)(0) #]       el guardian final");
            mas(); println("  [#   ##   #]");
            mas(); println("  [##########]");
            mas(); println("   //      \\\\");
        default:
            mas(); println("     ( ???? )         algo en la sombra");
    };
};

// Grito de batalla, agresivo
func rugido(id int) {
    switch id {
        case 1:
            mas(); println(">> Aparece una RATA GIGANTE !      SKREEEEK!");
        case 2:
            mas(); println(">> Aparece un MURCIELAGO !         KRIIIIIH!");
        case 3:
            mas(); println(">> Aparece un ESQUELETO !          KRAK-KRAK-KRAK!");
        case 4:
            mas(); println(">> Aparece un ORCO BRUTO !         GROAAAARR!");
        case 5:
            mas(); println(">> Despierta el GOLEM DE PIEDRA !  GRRRROAAAAARRR!");
        default:
            mas(); println(">> Algo grune en la negrura...     GRRRRRR!");
    };
};

// Estallido al hacer un golpe critico
func impacto() {
    mas(); println("         \\   |   /");
    mas(); println("       --==  POW!  ==--");
    mas(); println("         /   |   \\");
};

// Animacion de subir de nivel
func brilloNivel() {
    mas(); println("          .  *  .");
    mas(); println("       *    LEVEL    *");
    mas(); println("          .  UP!  .");
};

func victoria() {
    println("");
    mas(); println("+========================================+");
    mas(); println("|       * * *  V I C T O R I A  * * *     |");
    mas(); println("|   Has limpiado la cripta por completo. |");
    mas(); println(`|           \o/    \o/    \o/             |`);
    mas(); println("+========================================+");
    mas(); println("    .----------------------------.");
    mas(); println("    |  $ $ $   T E S O R O   $ $ |");
    mas(); println("    |    oro, gemas y gloria     |");
    mas(); println("    '----------------------------'");
    println("");
};

func derrota() {
    println("");
    mas(); println("+========================================+");
    mas(); println("|       x x x  D E R R O T A  x x x       |");
    mas(); println("|   El heroe cae en la cripta del Golem. |");
    mas(); println("|             R . I . P .                |");
    mas(); println("+========================================+");
    mas(); println("       |    |    |    |    |    |");
    mas(); println("    ___|____|____|____|____|____|___");
    mas(); print("Has caido con la semilla maldita: "); println(semilla);
    println("");
};

// El oraculo revela algunas semillas que llevan a la perdicion
func oraculo() {
    mas(); println("El oraculo susurra desde las sombras:");
    mas(); println("<< estas semillas estan MALDITAS (pierdes): >>");
    mas(); println("   1, 2, 10, 11, 14, 17, 26, 32, 33, 35,");
    mas(); println("   36, 38, 41, 42, 44, 45, 48, 50, 57, 60");
    mas(); println("<< con las demas, la fortuna te acompana >>");
    println("");
};

// Informe final con estadisticas de la partida
func informe() {
    var totalBotin int = 0;
    var k int = 0;
    for k < len(registroOro) {
        totalBotin = totalBotin + registroOro[k];
        k++;
    };
    mas(); println("+----------- INFORME FINAL --------------+");
    mas(); print("  Semilla jugada    : "); println(semilla);
    mas(); print("  Nivel alcanzado   : "); println(nivel);
    mas(); print("  Enemigos vencidos : "); println(derrotados);
    mas(); print("  Oro acumulado     : "); println(oro);
    mas(); print("  Botin total(slice): "); println(totalBotin);
    mas(); print("  Batallas con botin: "); println(len(registroOro));
    mas(); print("  Pociones usadas   : "); println(pocionesUsadas);
    mas(); print("  Turnos totales    : "); println(turnos);
    mas(); println("+----------------------------------------+");
    println("");
};

// El heroe bebe una pocion si la necesita
func usarPocion() {
    var cura int = azarRango(7, 13);
    heroe.hp = heroe.hp + cura;
    if heroe.hp > heroe.max {
        heroe.hp = heroe.max;
    };
    pociones--;
    pocionesUsadas++;
    mas(); print("* El heroe bebe una pocion (+");
    print(cura);
    print(" vida). Restan: ");
    println(pociones);
};

// Animacion de un dado de combate
func tirarDado() int {
    var cara int = azarRango(1, 6);
    mas(); print("[ dado ] ");
    switch cara {
        case 1:
            print("( o     )");
        case 2:
            print("( o   o )");
        case 3:
            print("( o o o )");
        case 4:
            print("(o o o o)");
        case 5:
            print("(o:o:o:o)");
        default:
            print("(oooooo!)");
    };
    print("  ->  ");
    println(cara);
    return cara;
};

// Reparte botin tras vencer a un enemigo y gestiona el nivel
func botin() {
    derrotados++;
    var ganOro int = azarRango(3, 12);
    oro = oro + ganOro;
    registroOro = append(registroOro, ganOro);

    var ganExp int = azarRango(8, 15);
    exp = exp + ganExp;

    mas(); print("Botin: +");
    print(ganOro);
    print(" oro,  +");
    print(ganExp);
    print(" exp");

    var suerte int = azarRango(1, 100);
    if suerte > 68 {
        pociones++;
        print(",  +1 pocion");
    };
    println("");

    mas(); print("Experiencia: ");
    print(exp);
    print(" / ");
    println(nivel * 20);

    // Puede subir varios niveles de golpe si junto mucha experiencia
    for exp >= nivel * 20 {
        exp = exp - nivel * 20;
        nivel++;
        heroe.max = heroe.max + 7;
        heroe.atk = heroe.atk + 2;
        heroe.hp = heroe.max;
        println("");
        brilloNivel();
        mas(); print("Ahora eres nivel ");
        print(nivel);
        print("  (vida max ");
        print(heroe.max);
        print(", ataque ");
        print(heroe.atk);
        println(")");
    };
    println("");
};

// Un combate completo. Devuelve 1 si el heroe sobrevive, 0 si muere
func combate(id int, vidaEnemigo int, fuerzaEnemigo int) int {
    sprite(id);
    rugido(id);
    println("");
    var maxEnemigo int = vidaEnemigo;

    for vidaEnemigo > 0 {
        if heroe.hp <= 0 {
            return 0;
        };
        turnos++;

        mas(); println("------------------ turno ------------------");
        mas(); print("(Nv ");
        print(nivel);
        println(")");
        barra(1, heroe.hp, heroe.max);
        barra(2, vidaEnemigo, maxEnemigo);

        // Si la vida baja del 35% y hay pociones, el heroe se cura
        if pociones > 0 {
            if heroe.hp * 100 / heroe.max < 35 {
                usarPocion();
            };
        };

        // Ataque del heroe
        var dano int = azarRango(heroe.atk - 1, heroe.atk + 4);
        var critico int = azarRango(1, 100);
        if critico > 82 {
            dano = dano * 2;
            impacto();
            mas(); println(">> GOLPE CRITICO del heroe!");
        };
        vidaEnemigo = vidaEnemigo - dano;
        mas(); print("El heroe ataca y causa ");
        print(dano);
        println(" de dano.");

        if vidaEnemigo <= 0 {
            println("");
            mas(); println("El enemigo se desploma. RRGH... derrotado!");
            println("");
            return 1;
        };

        // Respuesta del enemigo
        var esquiva int = azarRango(1, 100);
        if esquiva > 88 {
            mas(); println("El heroe ESQUIVA el golpe con agilidad!");
        } else {
            var danoE int = azarRango(fuerzaEnemigo / 2 + 1, fuerzaEnemigo);
            var bloqueo int = azarRango(1, 100);
            if bloqueo > 82 {
                danoE = danoE / 2;
                mas(); println("El heroe BLOQUEA con el escudo (mitad de dano)!");
            };
            heroe.hp = heroe.hp - danoE;
            mas(); print("El enemigo embiste por ");
            print(danoE);
            println(" de dano.");
        };
        println("");
    };

    if heroe.hp <= 0 {
        return 0;
    };
    return 1;
};

func main() {
    rng = semilla;

    heroe.hp = 30;
    heroe.max = 30;
    heroe.atk = 6;

    banner();
    separador();
    dibujarHeroe();
    separador();
    descender();
    separador();
    oraculo();
    separador();

    mas(); print("Semilla de esta aventura: ");
    println(semilla);
    println("");
    mas(); println("El heroe lanza el dado de la fortuna...");
    tirarDado();
    separador();

    // Lista de enemigos (arrays recorridos con len)
    var ids [5]int;
    var hps [5]int;
    var atks [5]int;
    ids[0] = 1;  hps[0] = 12;  atks[0] = 6;
    ids[1] = 2;  hps[1] = 15;  atks[1] = 7;
    ids[2] = 3;  hps[2] = 19;  atks[2] = 9;
    ids[3] = 4;  hps[3] = 26;  atks[3] = 11;
    ids[4] = 5;  hps[4] = 40;  atks[4] = 15;

    var vivo int = 1;
    var i int = 0;
    for i < len(ids) {
        if ids[i] == 5 {
            mas(); println("El suelo TIEMBLA... algo ENORME despierta. GRRRR!");
            println("");
        };

        vivo = combate(ids[i], hps[i], atks[i]);
        if vivo == 0 {
            break;
        };

        botin();
        separador();

        i++;
    };

    if vivo == 0 {
        derrota();
    } else {
        victoria();
    };
    separador();

    informe();
};
