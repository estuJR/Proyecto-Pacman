#include <iostream> 
#include <string> 
#include <vector> 
#include <functional> 
#include <fstream>

#ifdef _WIN32 
#include <conio.h> 
#else 
#include <termios.h> 
#include <unistd.h> 
#include <sys/ioctl.h> 
#endif
namespace term {
inline void clear() { std::cout << "\x1b[2J\x1b[H"; }
inline std::string bold(const std::string& s) { return "\x1b[1m" + s + "\x1b[0m"; }
inline std::string dim(const std::string& s) { return "\x1b[2m" + s + "\x1b[0m"; }
inline std::string inv(const std::string& s) { return "\x1b[7m" + s + "\x1b[0m"; }

#ifdef _WIN32
inline int width() {
    return 80; // para Windows, simple
}
#else
#include <sys/ioctl.h>
inline int width() {
    winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return (w.ws_col > 0) ? w.ws_col : 80;
}
#endif

inline void println_center(const std::string& s) {
    int W = width();
    int pad = (W - (int)s.size()) / 2;
    if (pad < 0) pad = 0;
    std::cout << std::string(pad, ' ') << s << "\n";
}
} // namespace term

// ---------- Teclas ----------
namespace keys {enum Key { NONE=0, ENTER, UP, DOWN, LEFT, RIGHT, NUM1, NUM2, NUM3, NUM4, QUIT };

#ifndef _WIN32
struct RawGuard {
    termios old{};
    bool active=false;
    RawGuard() {
        if(!isatty(STDIN_FILENO)) return;
        tcgetattr(STDIN_FILENO,&old);
        termios raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]=0; raw.c_cc[VTIME]=1;
        tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw); active=true;
    }
    ~RawGuard() { if(active) tcsetattr(STDIN_FILENO,TCSAFLUSH,&old); }
};
#endif

inline Key read() {
#ifdef _WIN32
    if (!_kbhit()) return NONE;
    int c = _getch();
    if (c == 0 || c == 224) { 
        int s = _getch(); 
        // Detectar teclas de flecha
        if (s == 72) return UP;   // Flecha arriba
        if (s == 80) return DOWN;  // Flecha abajo
        if (s == 75) return LEFT;  // Flecha izquierda
        if (s == 77) return RIGHT; // Flecha derecha
        return NONE;
    }
    if (c == '\r') return ENTER;
        if (c == '1') return NUM1; 
        if (c == '2') return NUM2; 
        if (c == '3') return NUM3; 
        if (c == '4') return NUM4;
        if (c == 'q' || c == 'Q') return QUIT;
        return NONE;
    #else
        unsigned char buf[3]; ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) return NONE;
        if (n == 1) { 
            unsigned char c = buf[0];
            if (c == '\n' || c == '\r') return ENTER;
            if (c == '1') return NUM1; 
            if (c == '2') return NUM2; 
            if (c == '3') return NUM3; 
            if (c == '4') return NUM4;
            if (c == 'q' || c == 'Q') return QUIT;
            return NONE;
        } if (n == 3 && buf[0] == 0x1b && buf[1] == '[') {
                // Debug: Imprimir valores de teclas de flecha para asegurarse de que se detectan
                std::cout << "Código tecla: " << (int)buf[2] << std::endl;
                if (buf[2] == 'A') return UP;    // Flecha arriba
                if (buf[2] == 'B') return DOWN;  // Flecha abajo
                if (buf[2] == 'C') return RIGHT; // Flecha derecha
                if (buf[2] == 'D') return LEFT;  // Flecha izquierda
            }
            return NONE;
        #endif
        }
} // namespace keys

// ---------- Pantallas ----------
void screen_wait_anykey(const std::string& title, const std::vector<std::string>& lines){
    term::clear();
    term::println_center(term::bold(title)); std::cout<<"\n";
    for(auto &l: lines) term::println_center(l);
    std::cout<<"\n";
    term::println_center(term::dim("Presiona cualquier tecla para volver al menú..."));
#ifdef _WIN32
    _getch();
#else
    keys::RawGuard rg;
    while(keys::read()==keys::NONE);
#endif
}

void screen_instrucciones(){
    screen_wait_anykey("INSTRUCCIONES",{
        "Objetivo: Pac-Man recolecta todas las fichas evitando a los fantasmas.",
        "Controles P1 (Pac-Man): Flechas ↑ ↓ ← →",
        "Controles P2 (Fantasma): W A S D",
        "Cada entidad correrá en su propio hilo en la Fase 03."
    });
}

void screen_puntajes(){
    screen_wait_anykey("PUNTAJES",{
        "Aquí se mostrarán los puntajes destacados.",
        "Persistencia se implementará en la Fase 03."
    });
}

// ---------- Tablero inicial ----------
const char PACMAN='C', GHOST='F', WALL='#', TOKEN='.', POWER='P';
std::vector<std::string> maze={
  "##############################",
  "#C...........##............F#",
  "#.####.#####.##.#####.#######.#",
  "#......#..............#.......#",
  "###.###.#.##########.#.###.###",
  "#.......#.....P......#.......#",
  "#.#####.###.######.###.#####.#",
  "#F.........................F.#",
  "##############################"
};

int maze_height = maze.size();        // Número de filas en el mapa
int maze_width = maze[0].size();      // Número de columnas en el mapa (asumiendo que todas las filas tienen el mismo tamaño)


void screen_tablero() {
    term::clear();  // Limpiar la pantalla
    term::println_center("=== PAC-MAN - TABLERO ===\n");
    
    // Imprimir el mapa actualizado de maze_live
    for (auto& row : maze_live) {
        term::println_center(row);  // Mostrar cada fila del mapa actualizado
    }
    // Mostrar información del juego
        term::println_center("Puntos: 0 | Vidas: 3");
        term::println_center("Presiona cualquier tecla para volver al menú...");
    }

// ---------- Menú ----------
struct MenuItem { std::string label; std::function<void()> action; };

void draw_title_ascii(){
    term::println_center(term::bold(" ____            __  __              "));
    term::println_center(term::bold("|  _ \\ __ _  ___|  \\/  | __ _ _ __   "));
    term::println_center(term::bold("| |_) / _ |/ __| |\\/| |/ _ | '_ \\ "));
    term::println_center(term::bold("|  __/ (_| | (__| |  | | (_| | | | |"));
    term::println_center(term::bold("|_|   \\__,_|\\___|_|  |_|\\__,_|_| |_|"));
    term::println_center(term::dim("Proyecto 01 – Menú de inicio"));
}

void draw_menu(const std::vector<MenuItem>& items,int selected){
    term::clear(); draw_title_ascii(); std::cout<<"\n";
    for(int i=0;i<(int)items.size();i++){
        std::string line = std::to_string(i+1)+") "+items[i].label;
        if(i==selected) line = term::inv(" "+line+" ");
        term::println_center(line);
    }
    std::cout<<"\n";
    term::println_center(term::dim("Usa flechas ↑/↓ o teclas 1..4. Enter para seleccionar. 'q' para salir."));
}

// ---------- Lógica de juego ----------

// Estructura para el estado del juego
struct GameState {
    std::vector<std::string> maze_base;
    std::vector<std::string> maze_live;
    int pacmanLives = 3;
    int score = 0;
    int tokensRemaining = 0;
    bool powerMode = false;  // Power-up activado
    int powerTimer = 0;      // Temporizador del power-up

    int pacmanX = 0;         // Coordenada X de Pac-Man
    int pacmanY = 0;         // Coordenada Y de Pac-Man

    int ghost1X = 5;         // Coordenada X de Fantasma 1
    int ghost1Y = 5;         // Coordenada Y de Fantasma 1

    int ghost2X = 10;        // Coordenada X de Fantasma 2
    int ghost2Y = 10;        // Coordenada Y de Fantasma 2

    // Constructor para inicializar maze_base y maze_live
    GameState() {
        maze_base = {
            "##############################",
            "#C...........##............F#",
            "#.####.#####.##.#####.#######.#",
            "#......#..............#.......#",
            "###.###.#.##########.#.###.###",
            "#.......#.....P......#.......#",
            "#.#####.###.######.###.#####.#",
            "#F.........................F.#",
            "##############################"
        };
        
        maze_live = maze_base;  // Inicializa maze_live como una copia de maze_base
    }
};

enum GameMode { MODE_1, MODE_2, MODE_3 };  // Definir los tres modos de juego

GameMode selectedMode = MODE_1;  // Inicializar en el Modo 1 (velocidad media)

int ghostSpeed = 500000;  // Velocidad inicial para fantasmas

// Función para seleccionar el modo de juego
void select_game_mode() {
    term::clear();
    term::println_center("Seleccione el modo de juego:");
    term::println_center("1) Modo 1: Un jugador, velocidad media.");
    term::println_center("2) Modo 2: Un jugador, velocidad rápida.");
    term::println_center("3) Modo 3: Dos jugadores, uno controla Pac-Man y otro controla un fantasma.");
    
    keys::Key k = keys::NONE;
    do {
        k = keys::read();
    } while (k == keys::NONE);

    if (k == keys::NUM1) {
        selectedMode = MODE_1;
        ghostSpeed = 500000;  // 500 ms para el Modo 1 (velocidad media)
    }
    if (k == keys::NUM2) {
        selectedMode = MODE_2;
        ghostSpeed = 250000;  // 250 ms para el Modo 2 (más rápido)
    }
    if (k == keys::NUM3) {
        selectedMode = MODE_3;
        ghostSpeed = 500000;  // Velocidad media para los fantasmas NPC en Modo 3
    }

    // Ahora que seleccionamos el modo, continuar con la inicialización del juego.
}

// Inicializa el estado del juego
void init_game(GameState& s) {

    s.maze_live.clear();  // Asegúrate de limpiar maze_live antes de copiar
    std::copy(s.maze_base.begin(), s.maze_base.end(), std::back_inserter(s.maze_live));

    // Verifica el mapa después de inicializarlo
    std::cout << "Estado del mapa: \n";
    for (const auto& row : s.maze_live) {
    std::cout << row << "\n";
    }

    // Recalcular los tokens correctamente
    s.tokensRemaining = 0;
    for (auto& row : s.maze_live) {
        for (char c : row) {
            if (c == '.' || c == 'P') s.tokensRemaining++;  // Cuenta los tokens y power-ups correctamente.
        }
    }
    // Verifica el número de tokens después de contar
    std::cout << "Tokens restantes despues de contar: " << s.tokensRemaining << std::endl;

    // Reiniciar las variables del juego
    s.pacmanLives = 3;  // Restablece las vidas
    s.score = 0;        // Reinicia el puntaje
    s.powerMode = false;  // Desactiva el power-up
    s.powerTimer = 0;    // Resetea el temporizador del power-up
    // Verifica las variables de estado
    std::cout << "Vidas: " << s.pacmanLives << ", Puntos: " << s.score << ", Tokens restantes: " << s.tokensRemaining << std::endl;

    // Verifica el contenido de maze_base antes de la asignación
        std::cout << "Contenido de maze_base:\n";
        for (const auto& row : maze) {
            std::cout << row << "\n";  // Muestra maze_base
        }

        // Copia explícita de los valores de maze_base a maze_live
        s.maze_live.clear();  // Limpia cualquier valor anterior en maze_live
        std::copy(s.maze_base.begin(), s.maze_base.end(), std::back_inserter(s.maze_live));  // Copia explícita

        // Verifica el contenido de maze_live después de la asignación
        std::cout << "Contenido de maze_live después de la asignación:\n";
        for (const auto& row : s.maze_live) {
            std::cout << row << "\n";  // Muestra maze_live
        }


}

// Reinicia el juego pero mantiene el puntaje acumulado
void reset_game(GameState& s) {
    init_game(s);  // Reinicia todo el estado
}


void onPacmanEat(GameState& s, char cell) {
    if (cell == '.') { 
        s.score += 10; 
        s.tokensRemaining--; 
    }
    else if (cell == 'P') { 
        s.score += 50; 
        s.tokensRemaining--; 
        s.powerMode = true; 
        s.powerTimer = 10;  // Power-up dura 10 turnos
    }
    else if (cell == 'F' && !s.powerMode) { 
        // Si Pac-Man toca un fantasma y no está en modo power-up, pierde una vida
        s.pacmanLives--;
    }
    else if (cell == 'F' && s.powerMode) { 
        // Si Pac-Man toca un fantasma y está en modo power-up, el fantasma "muere"
        s.score += 200;  // Sumar puntos por comer al fantasma
        // Reiniciar el fantasma a una posición inicial
    }
}

// Comprobar si Pac-Man ha ganado (recolectó todos los tokens)
bool checkWin(const GameState& s) {
    return s.tokensRemaining == 0;
}

// Comprobar si Pac-Man ha perdido todas sus vidas
bool checkLose(const GameState& s) {
    return s.pacmanLives <= 0;
}


// Guardar puntaje en archivo scores.txt
void save_score(const GameState& s) {
    std::ofstream out("scores.txt", std::ios::app);
    out << "Jugador1," << s.score << "\n";  // Guardar nombre y puntaje
}

// Hilo de Pac-Man (movimiento y lógica de comer)
void* pacman_thread(void* arg) {
    GameState* state = (GameState*)arg;

    while (state->pacmanLives > 0 && state->tokensRemaining > 0) {
        keys::Key k = keys::read();  // Leer entrada de teclas para el movimiento de Pac-Man
        
        // Imprimir la tecla presionada para depuración
        std::cout << "Tecla presionada: " << k << std::endl;

        // Limpiar la posición anterior de Pac-Man (si estaba en alguna celda)
        state->maze_live[state->pacmanY][state->pacmanX] = '.';  // Asumiendo que '.' es el valor para los puntos

        // Mover Pac-Man según la tecla presionada
        if (k == keys::UP && state->pacmanY > 0 && state->maze_live[state->pacmanY - 1][state->pacmanX] != '#') {
            state->pacmanY--;  // Mover arriba si no hay pared
        }
        if (k == keys::DOWN && state->pacmanY < maze_height - 1 && state->maze_live[state->pacmanY + 1][state->pacmanX] != '#') {
            state->pacmanY++;  // Mover abajo si no hay pared
        }
        if (k == keys::LEFT && state->pacmanX > 0 && state->maze_live[state->pacmanY][state->pacmanX - 1] != '#') {
            state->pacmanX--;  // Mover izquierda si no hay pared
        }
        if (k == keys::RIGHT && state->pacmanX < maze_width - 1 && state->maze_live[state->pacmanY][state->pacmanX + 1] != '#') {
            state->pacmanX++;  // Mover derecha si no hay pared
        }

        // Actualizar la posición de Pac-Man con el carácter 'C'
        state->maze_live[state->pacmanY][state->pacmanX] = 'C';  // Pac-Man representado por 'C'

        // Imprimir las coordenadas de Pac-Man después de moverse
        std::cout << "Pac-Man está en: (" << state->pacmanX << ", " << state->pacmanY << ")\n";  // Muestra las coordenadas

        // Actualiza el tablero después de cada movimiento
        screen_tablero();  // Esto muestra el mapa con la nueva posición de Pac-Man

        // Verificar condiciones de victoria o derrota
        if (checkWin(*state)) {
            save_score(*state);  // Guardar el puntaje al ganar
            screen_wait_anykey("¡Has ganado!", {"¡Felicidades! Pac-Man ha recolectado todos los puntos."});
            break;  // Sale del ciclo si Pac-Man ganó
        } else if (checkLose(*state)) {
            screen_wait_anykey("Game Over", {"Has perdido todas tus vidas."});
            break;  // Sale del ciclo si Pac-Man perdió
        }

        usleep(ghostSpeed);  // Hilo de Pac-Man también depende de la velocidad de los fantasmas
    }

    return nullptr;
}


// Hilo de los fantasmas NPC (movimiento aleatorio o hacia Pac-Man)
void* ghost_npc_thread(void* arg) {
    GameState* state = (GameState*)arg;

    while (state->pacmanLives > 0 && state->tokensRemaining > 0) {
        // Movimiento aleatorio o hacia Pac-Man
        int direction = rand() % 4;  // Movimiento aleatorio de los fantasmas

        // Movimiento de los fantasmas (aleatorio o simple IA de persecución)
        if (direction == 0 && state->ghost2Y > 0) state->ghost2Y--;  // Mover arriba
        if (direction == 1 && state->ghost2Y < maze_height - 1) state->ghost2Y++;  // Mover abajo
        if (direction == 2 && state->ghost2X > 0) state->ghost2X--;  // Mover izquierda
        if (direction == 3 && state->ghost2X < maze_width - 1) state->ghost2X++;  // Mover derecha

        usleep(ghostSpeed);  // Retardo entre movimientos (según el modo)
    }

    return nullptr;
}


void* ghost_player_thread(void* arg) {
    GameState* state = (GameState*)arg;

    while (state->pacmanLives > 0 && state->tokensRemaining > 0) {
        keys::Key k = keys::read();  // Leer entrada de teclas para el movimiento del fantasma

        // Movimiento del fantasma controlado por el jugador
        if (k == keys::UP && state->ghost2Y > 0) state->ghost2Y--;  // Mover arriba
        if (k == keys::DOWN && state->ghost2Y < maze_height - 1) state->ghost2Y++;  // Mover abajo
        if (k == keys::LEFT && state->ghost2X > 0) state->ghost2X--;  // Mover izquierda
        if (k == keys::RIGHT && state->ghost2X < maze_width - 1) state->ghost2X++;  // Mover derecha

        usleep(ghostSpeed);  // Velocidad del fantasma controlado por el jugador
    }

    return nullptr;
}

// ---------- MAIN ----------
int main() {
    std::vector<MenuItem> items = {
        { "Iniciar partida", []() {
            select_game_mode();  // Permite elegir el modo de juego (Modo 1, 2 o 3)

            // Inicializamos el estado del juego
            GameState state;
            init_game(state);  // Inicializa el juego
            std::cout << "Después de la inicialización del juego:" << std::endl;
            std::cout << "Tokens restantes: " << state.tokensRemaining << std::endl;
            // Crear los hilos según el modo de juego
            pthread_t pacman, ghost1, ghost2, ghost3;

            // Modo 3: Dos jugadores
            if (selectedMode == MODE_3) {

                pthread_create(&pacman, NULL, pacman_thread, &state);  // Hilo de Pac-Man
                pthread_create(&ghost1, NULL, ghost_player_thread, &state);  // Hilo de fantasma controlado por jugador
                pthread_create(&ghost2, NULL, ghost_npc_thread, &state);  // Hilo de fantasma NPC 1
                pthread_create(&ghost3, NULL, ghost_npc_thread, &state);  // Hilo de fantasma NPC 2
            }
            // Modo 1 y 2: Un jugador
            else {
                pthread_create(&pacman, NULL, pacman_thread, &state);  // Hilo de Pac-Man
                pthread_create(&ghost1, NULL, ghost_npc_thread, &state);  // Hilo de fantasma NPC 1
                pthread_create(&ghost2, NULL, ghost_npc_thread, &state);  // Hilo de fantasma NPC 2
            }

            // Mantener la ejecución del juego mientras los hilos están activos
            while (state.pacmanLives > 0 && state.tokensRemaining > 0) {
                // Actualizar la pantalla (tablero)
                screen_tablero();  // Actualiza el tablero (agregar esta función para mostrar el mapa actualizado)

                // Continuar ejecutando los hilos
                usleep(ghostSpeed);  // Mantener la velocidad de los fantasmas
            }

            // Al finalizar la partida, podemos mostrar la opción de reiniciar o salir
            if (checkWin(state)) {
                screen_wait_anykey("¡Has ganado!", {"¡Felicidades! Pac-Man ha recolectado todos los puntos."});
            } else {
                screen_wait_anykey("Game Over", {"Has perdido todas tus vidas."});
            }
        }},
        { "Instrucciones", []() { screen_instrucciones(); } },
        { "Puntajes", []() { screen_puntajes(); } },
        { "Mostrar tablero", []() { screen_tablero(); } },
        { "Salir", []() { exit(0); } }
    };

    int selected = 0;
#ifndef _WIN32
    keys::RawGuard rg;
#endif

    // Mostrar el menú y esperar entrada del usuario
    while (true) {
        draw_menu(items, selected);
        
        // Esperar hasta que se presione una tecla válida
        keys::Key k = keys::NONE;
        do { k = keys::read(); } while(k == keys::NONE);
        
        if (k == keys::QUIT) break;
        if (k == keys::UP) selected = (selected - 1 + (int)items.size()) % (int)items.size();
        if (k == keys::DOWN) selected = (selected + 1) % (int)items.size();
        if (k == keys::NUM1) selected = 0;
        if (k == keys::NUM2) selected = 1;
        if (k == keys::NUM3) selected = 2;
        if (k == keys::NUM4) selected = 3;

        if (k == keys::ENTER) {
            if (selected == 4) break;
            items[selected].action();
        }
    }

    return 0;
}

