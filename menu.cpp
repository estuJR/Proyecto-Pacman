#include <iostream>
#include <string>
#include <vector>
#include <functional>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

// ---------- Consola ----------
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
namespace keys {
enum Key { NONE=0, ENTER, UP, DOWN, NUM1, NUM2, NUM3, NUM4, QUIT };

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
    if(!_kbhit()) return NONE;
    int c = _getch();
    if(c==0 || c==224){ int s=_getch(); if(s==72) return UP; if(s==80) return DOWN; return NONE;}
    if(c=='\r') return ENTER;
    if(c=='1') return NUM1; if(c=='2') return NUM2; if(c=='3') return NUM3; if(c=='4') return NUM4;
    if(c=='q'||c=='Q') return QUIT;
    return NONE;
#else
    unsigned char buf[3]; ssize_t n = ::read(STDIN_FILENO,buf,sizeof(buf));
    if(n<=0) return NONE;
    if(n==1){ unsigned char c=buf[0];
        if(c=='\n'||c=='\r') return ENTER;
        if(c=='1') return NUM1; if(c=='2') return NUM2; if(c=='3') return NUM3; if(c=='4') return NUM4;
        if(c=='q'||c=='Q') return QUIT; return NONE;
    }
    if(n==3 && buf[0]==0x1b && buf[1]=='['){
        if(buf[2]=='A') return UP; if(buf[2]=='B') return DOWN;
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
  "####################",
  "#C.....#......F...#",
  "#.####.#.####.#.###",
  "#......#......#...#",
  "###.#.#####.#.###.#",
  "#...#...P...#.....#",
  "#.###.#####.###.#.#",
  "#F...............#",
  "####################"
};

void screen_tablero(){
    term::clear();
    term::println_center("=== PAC-MAN - TABLERO INICIAL ===\n");
    for(auto &l: maze) term::println_center(l);
    std::cout<<"\n";
    term::println_center("Puntos: 0 | Vidas: 3");
    term::println_center("Presiona cualquier tecla para volver al menú...");
#ifdef _WIN32
    _getch();
#else
    keys::RawGuard rg;
    while(keys::read()==keys::NONE);
#endif
}

// ---------- Menú ----------
struct MenuItem { std::string label; std::function<void()> action; };

void draw_title_ascii(){
    term::println_center(term::bold(" ____            __  __              "));
    term::println_center(term::bold("|  _ \\ __ _  ___|  \\/  | __ _ _ __   "));
    term::println_center(term::bold("| |_) / _` |/ __| |\\/| |/ _` | '_ \\ "));
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

// ---------- MAIN ----------
int main(){
    std::vector<MenuItem> items={
        { "Iniciar partida (stub)", [](){ screen_wait_anykey("INICIAR PARTIDA",{"Fase 03 activará lógica y hilos."}); } },
        { "Instrucciones", [](){ screen_instrucciones(); } },
        { "Puntajes", [](){ screen_puntajes(); } },
        { "Mostrar tablero", [](){ screen_tablero(); } },
        { "Salir", [](){ } }
    };

    int selected=0;
#ifndef _WIN32
    keys::RawGuard rg;
#endif

    while(true){
        draw_menu(items,selected);

        // Bloqueante: espera UNA tecla válida
        keys::Key k = keys::NONE;
        do { k=keys::read(); } while(k==keys::NONE);

        if(k==keys::QUIT) break;
        if(k==keys::UP) selected=(selected-1+(int)items.size())%(int)items.size();
        if(k==keys::DOWN) selected=(selected+1)%(int)items.size();
        if(k==keys::NUM1) selected=0;
        if(k==keys::NUM2) selected=1;
        if(k==keys::NUM3) selected=2;
        if(k==keys::NUM4) selected=3;

        if(k==keys::ENTER){
            if(selected==4) break;
            items[selected].action();
        }
    }

    term::clear();
    term::println_center("Gracias por usar el menú. ¡Hasta pronto!");
    return 0;
}
