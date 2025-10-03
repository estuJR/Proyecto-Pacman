#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <cstdlib>
#include <ctime>
#include <cmath>

// ============================================================================
// Terminal UI
// ============================================================================
namespace term {
inline void clear(){ std::cout << "\x1b[2J\x1b[H"; }
inline std::string bold(const std::string& s){ return "\x1b[1m"+s+"\x1b[0m"; }
inline std::string dim (const std::string& s){ return "\x1b[2m"+s+"\x1b[0m"; }
inline std::string inv (const std::string& s){ return "\x1b[7m"+s+"\x1b[0m"; }

inline int width(){
    winsize w{}; ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return (w.ws_col>0)? w.ws_col : 80;
}
inline void println_center(const std::string& s){
    int W=width(); int pad=(W-(int)s.size())/2;
    if(pad<0) pad=0;
    std::cout << std::string(pad,' ') << s << "\n";
}
} // namespace term

// ============================================================================
// Teclado (raw no bloqueante)
// ============================================================================
namespace keys {
enum Key { NONE=0, ENTER, UP, DOWN, LEFT, RIGHT, QUIT, W, A, S, D, NUM1, NUM2, NUM3, NUM4 };

struct RawGuard {
    termios old{}; bool active=false;
    RawGuard(){
        if(!isatty(STDIN_FILENO)) return;
        tcgetattr(STDIN_FILENO,&old);
        termios raw=old;
        raw.c_lflag &= ~(ICANON|ECHO);
        raw.c_cc[VMIN]=0; raw.c_cc[VTIME]=1;
        tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw);
        active=true;
    }
    ~RawGuard(){ if(active) tcsetattr(STDIN_FILENO,TCSAFLUSH,&old); }
};

inline Key read(){
    unsigned char buf[3]; ssize_t n=::read(STDIN_FILENO,buf,sizeof(buf));
    if(n<=0) return NONE;
    if(n==1){
        unsigned char c=buf[0];
        if(c=='\n' || c=='\r') return ENTER;
        if(c=='q' || c=='Q')   return QUIT;
        if(c=='w' || c=='W')   return W;
        if(c=='a' || c=='A')   return A;
        if(c=='s' || c=='S')   return S;
        if(c=='d' || c=='D')   return D;
        if(c=='1') return NUM1;
        if(c=='2') return NUM2;
        if(c=='3') return NUM3;
        if(c=='4') return NUM4;
        return NONE;
    }
    if(n==3 && buf[0]==0x1b && buf[1]=='['){
        if(buf[2]=='A') return UP;
        if(buf[2]=='B') return DOWN;
        if(buf[2]=='C') return RIGHT;
        if(buf[2]=='D') return LEFT;
    }
    return NONE;
}
} // namespace keys

// ============================================================================
// Pantallas simples
// ============================================================================
void screen_wait_anykey(const std::string& title, const std::vector<std::string>& lines){
    term::clear();
    term::println_center(term::bold(title)); std::cout << "\n";
    for(const auto& l: lines) term::println_center(l);
    std::cout << "\n";
    term::println_center(term::dim("Presiona cualquier tecla para volver..."));
    keys::RawGuard rg; while(keys::read()==keys::NONE);
}
void screen_instrucciones(){
    screen_wait_anykey("INSTRUCCIONES",{
        "Objetivo: Pac-Man recolecta todas las fichas y evita fantasmas.",
        "Controles Pac-Man: Flechas ← ↑ → ↓",
        "Modo 3: Blinky (rojo) con WASD",
        "Power-up (P): Pac-Man puede comer fantasmas (huyen).",
        "Durante la partida presiona 'q' para volver al menú."
    });
}
void screen_puntajes(){
    screen_wait_anykey("PUNTAJES",{
        "Los puntajes se agregan a scores.txt.",
        "Abre ese archivo para ver tu historial."
    });
}

// ============================================================================
// Estado del juego
// ============================================================================
static const char WALL='#', TOKEN='.', POWER='P', EMPTY=' ', DOOR='-';
static const int DIRS[4][2]={{0,-1},{-1,0},{0,1},{1,0}}; // U L D R

struct GameState {
    // Mapas
    std::vector<std::string> maze_base;
    std::vector<std::string> maze_live;
    int H=0, W=0;

    // Pac-Man
    int px=1, py=1, pdx=1, pdy=0;
    int lives=3;
    int score=0;

    // Comestibles
    int tokens=0;
    bool power=false;
    int powerTimer=0;

    // Control
    bool stop=false;

    // Fantasmas
    struct Ghost{
        std::string name;
        int x=1,y=1,dx=0,dy=0;
        int color=31;       // 31 rojo, 35 rosa, 36 cian, 33 naranja
        bool inHouse=true;
        int release=0;      // ticks para salir
    };
    Ghost blinky, pinky, inky, clyde;

    // Casa
    int houseX=0, houseY=0, doorY=0, doorX1=0, doorX2=0;

    GameState(){
        // Mapa estilo clásico
        maze_base = {
"############################",
"#............##............#",
"#.####.#####.##.#####.####.#",
"#P####.#####.##.#####.####P#",
"#.####.#####.##.#####.####.#",
"#..........................#",
"#.####.##.########.##.####.#",
"#......##....##....##......#",
"######.##### ## #####.######",
"     #.##### ## #####.#     ",
"     #.##          ##.#     ",
"     #.## ###--### ##.#     ",
"######.## #      # ##.######",
"      .   #      #   .      ",
"######.## #      # ##.######",
"     #.## ######## ##.#     ",
"     #.##          ##.#     ",
"     #.## ######## ##.#     ",
"######.## ######## ##.######",
"#............##............#",
"#.####.#####.##.#####.####.#",
"#P...#................#...P#",
"####.#.##.########.##.#.####",
"#......##....##....##......#",
"#.##########.##.##########.#",
"#..........................#",
"############################"
        };
        maze_live = maze_base;
        H=(int)maze_base.size(); W=(int)maze_base[0].size();

        // Rellenar espacios en LIVE como puntos
        for(int y=0;y<H;y++) for(int x=0;x<W;x++)
            if(maze_live[y][x]==' ') maze_live[y][x]='.';

        // Contar comestibles
        tokens=0;
        for(auto &row: maze_live) for(char c: row) if(c=='.'||c=='P') tokens++;

        // Posiciones iniciales
        px=1; py=1; pdx=1; pdy=0;

        // Detectar puerta/casa
        for(int y=0;y<H;y++){
            for(int x=0;x<W;x++){
                if(maze_base[y][x]==DOOR){
                    doorY=y;
                    int a=x,b=x;
                    while(a>0 && maze_base[y][a-1]==DOOR) a--;
                    while(b+1<W && maze_base[y][b+1]==DOOR) b++;
                    doorX1=a; doorX2=b; houseX=(a+b)/2; houseY=y+1;
                }
            }
        }

        // Fantasmas con salida escalonada
        blinky={"Blinky",houseX,houseY,0,0,31,true,10};
        pinky ={"Pinky", houseX,houseY,0,0,35,true,30};
        inky  ={"Inky",  houseX,houseY,0,0,36,true,50};
        clyde ={"Clyde", houseX,houseY,0,0,33,true,70};

        power=false; powerTimer=0; stop=false;
    }

    inline void wrap(int &x,int &y) const { if(x<0)x=W-1; if(x>=W)x=0; if(y<0)y=H-1; if(y>=H)y=0; }
    inline bool is_wall(int x,int y) const { return maze_base[y][x]==WALL; }
    inline bool is_door(int x,int y) const { return maze_base[y][x]==DOOR; }
    inline bool solid_for_pacman(int x,int y) const { if(is_wall(x,y)) return true; if(is_door(x,y)) return true; return false; }
    inline bool solid_for_ghost (int x,int y) const { if(is_wall(x,y)) return true; /* puerta sí */ return false; }
};

// ============================================================================
// Render (AHORA prioriza maze_live, e imprime ' ' si live==' ')
// ============================================================================
static inline std::string color_cell(char c){
    switch(c){
        case '#': return "\033[34m#\033[0m"; // pared azul
        case '.': return "\033[37m.\033[0m"; // punto
        case 'P': return "\033[32mP\033[0m"; // power
        case '-': return "\033[36m-\033[0m"; // puerta
        case ' ': return " ";               // vacío (punto/power comido)  <-- CLAVE
        default:  return std::string(1,c);
    }
}
static inline std::string ghost_symbol(const GameState::Ghost& g,bool frightened){
    if(frightened) return "\033[34mF\033[0m";             // azul en frightened
    return "\033["+std::to_string(g.color)+"mF\033[0m";   // color propio
}
static inline std::string ghost_at(const GameState& s,int x,int y){
    auto chk=[&](const GameState::Ghost& g){
        if(!g.inHouse && g.x==x && g.y==y) return ghost_symbol(g, s.power);
        return std::string();
    };
    std::string r=chk(s.blinky); if(!r.empty()) return r;
    r=chk(s.pinky);  if(!r.empty()) return r;
    r=chk(s.inky);   if(!r.empty()) return r;
    r=chk(s.clyde);  return r;
}
static inline void draw_hud(const GameState& s){
    std::string hud="Puntos: "+std::to_string(s.score)+" | Vidas: "+std::to_string(s.lives);
    if(s.power) hud += " | POWER!";
    term::println_center(hud);
}
static void render(const GameState& s){
    term::clear();
    term::println_center(term::bold("=== PAC-MAN ==="));
    std::cout << "\n";
    int margin=(term::width()-s.W)/2; if(margin<0) margin=0;

    for(int y=0;y<s.H;y++){
        std::string line;
        for(int x=0;x<s.W;x++){
            if(x==s.px && y==s.py){
                line += "\033[33mC\033[0m";
            } else {
                std::string gh=ghost_at(s,x,y);
                if(!gh.empty()){
                    line += gh;
                } else {
                    // PRIORIDAD: live primero (., P, ' ')
                    char live = s.maze_live[y][x];
                    if(live=='.' || live=='P' || live==' '){
                        line += color_cell(live);       // <-- imprimimos ' ' si fue comido
                    } else {
                        line += color_cell(s.maze_base[y][x]); // paredes/puerta/otros
                    }
                }
            }
        }
        if(margin) std::cout << std::string(margin,' ');
        std::cout << line << "\n";
    }
    std::cout << "\n"; draw_hud(s); std::cout.flush();
}

// ============================================================================
// Pac-Man: comer celdas y moverse
// ============================================================================
static inline void eat_cell(GameState& s,int x,int y){
    char &c = s.maze_live[y][x];
    if(c==TOKEN){ s.score+=10; s.tokens--; c=EMPTY; }      // ← deja ' '
    else if(c==POWER){ s.score+=50; s.tokens--; c=EMPTY;   // ← deja ' '
                       s.power=true; s.powerTimer=220; }
}
static inline void move_pacman(GameState& s,int dx,int dy){
    int nx=s.px+dx, ny=s.py+dy; s.wrap(nx,ny);
    if(!s.solid_for_pacman(nx,ny)){
        s.px=nx; s.py=ny; s.pdx=dx; s.pdy=dy;
        eat_cell(s,nx,ny);
    }
}

// ============================================================================
// IA Fantasmas (targets y movimiento)
// ============================================================================
static inline int dist2(int ax,int ay,int bx,int by){ int dx=ax-bx, dy=ay-by; return dx*dx+dy*dy; }

static inline void compute_target(const GameState& s,
                                  const GameState::Ghost& blinky,
                                  const GameState::Ghost& me,
                                  int &tx, int &ty)
{
    if(s.power){
        // Huyen: esquina alejada de Pac-Man
        int cands[4][2]={{1,1},{s.W-2,1},{1,s.H-2},{s.W-2,s.H-2}};
        int best=-1e9; tx=1; ty=1;
        for(auto &c : cands){
            int d=-dist2(c[0],c[1], s.px,s.py);
            if(d>best){ best=d; tx=c[0]; ty=c[1]; }
        }
        return;
    }
    if(me.name=="Blinky"){ tx=s.px; ty=s.py; return; }
    if(me.name=="Pinky"){  tx=s.px + s.pdx*4; ty=s.py + s.pdy*4;
                           tx=std::max(0,std::min(s.W-1,tx));
                           ty=std::max(0,std::min(s.H-1,ty)); return; }
    if(me.name=="Inky"){
        int ix=s.px + 2*s.pdx, iy=s.py + 2*s.pdy;
        tx = 2*ix - blinky.x; ty = 2*iy - blinky.y;
        tx=std::max(0,std::min(s.W-1,tx));
        ty=std::max(0,std::min(s.H-1,ty)); return;
    }
    if(me.name=="Clyde"){
    int d=dist2(me.x,me.y,s.px,s.py);
    if(d>64){ 
        tx=s.px; ty=s.py;   // usa correctamente py
    } else { 
        tx=1; ty=s.H-2; 
    }
    return;
}
    tx=s.px; ty=s.py;
}

static inline bool can_move_ghost(const GameState& s, const GameState::Ghost& g, int ndx, int ndy){
    int nx=g.x+ndx, ny=g.y+ndy;
    if(ny<0||ny>=s.H||nx<0||nx>=s.W) return false;
    if(s.solid_for_ghost(nx,ny)) return false;
    return true;
}

static inline void step_towards(GameState& s, GameState::Ghost& g, int tx, int ty){
    int best=1e9, bdx=0, bdy=0;
    for(auto &d: DIRS){
        int ndx=d[0], ndy=d[1];
        if(ndx==-g.dx && ndy==-g.dy) continue; // evitar reversa inmediata
        if(!can_move_ghost(s,g,ndx,ndy)) continue;
        int nx=g.x+ndx, ny=g.y+ndy; s.wrap(nx,ny);
        int dd=dist2(nx,ny,tx,ty);
        if(dd<best){ best=dd; bdx=ndx; bdy=ndy; }
    }
    if(best==1e9){
        // Reversa si es la única
        if(can_move_ghost(s,g,-g.dx,-g.dy)){ g.x-=g.dx; g.y-=g.dy; g.dx=-g.dx; g.dy=-g.dy; return; }
        // O cualquier válida
        for(auto &d: DIRS){ if(can_move_ghost(s,g,d[0],d[1])){ g.x+=d[0]; g.y+=d[1]; g.dx=d[0]; g.dy=d[1]; return; } }
        return;
    }
    g.x+=bdx; g.y+=bdy; g.dx=bdx; g.dy=bdy;
}

static inline void ghost_tick_ai(GameState& s, GameState::Ghost& g){
    // Salida de la casa: subir a la puerta y salir
    if(g.inHouse){
        if(g.release>0){ g.release--; return; }
        if(g.y > s.doorY){
            int ny=g.y-1; if(!s.solid_for_ghost(g.x,ny)){ g.y--; return; }
        }
        g.inHouse=false; g.dx=1; g.dy=0; // ya fuera
    }
    int tx=0, ty=0; compute_target(s,s.blinky,g,tx,ty);
    step_towards(s,g,tx,ty);
}

// ============================================================================
// Colisiones y fin de power
// ============================================================================
static inline void handle_collisions(GameState& s){
    auto touch=[&](GameState::Ghost& g){
        if(g.inHouse) return;
        if(s.px==g.x && s.py==g.y){
            if(s.power){
                s.score+=200;
                g.inHouse=true; g.x=s.houseX; g.y=s.houseY; g.dx=g.dy=0; g.release=60;
            }else{
                s.lives--;
                s.px=1; s.py=1; s.pdx=1; s.pdy=0;
            }
        }
    };
    touch(s.blinky); touch(s.pinky); touch(s.inky); touch(s.clyde);
    if(s.power){ s.powerTimer--; if(s.powerTimer<=0) s.power=false; }
}

// ============================================================================
// Game loop con modos (modo 3: Blinky humano por WASD)
// ============================================================================
enum GameMode { MODE_1=0, MODE_2=1, MODE_3=2 };
static int TICK_US=90000;

static void game_loop(GameState& s, GameMode mode){
    keys::RawGuard rg;
    bool blinky_human = (mode==MODE_3);
    if(mode==MODE_1) TICK_US=90000;
    if(mode==MODE_2) TICK_US=60000;
    if(mode==MODE_3) TICK_US=90000;

    while(!s.stop && s.lives>0 && s.tokens>0){
        // Entrada
        keys::Key k=keys::read();
        if(k==keys::QUIT){ s.stop=true; break; }
        if(k==keys::LEFT ) move_pacman(s,-1,0);
        if(k==keys::RIGHT) move_pacman(s, 1,0);
        if(k==keys::UP   ) move_pacman(s, 0,-1);
        if(k==keys::DOWN ) move_pacman(s, 0, 1);

        // Control humano de Blinky (modo 3)
        if(blinky_human && !s.blinky.inHouse){
            int ddx=0, ddy=0;
            if(k==keys::A) ddx=-1;
            else if(k==keys::D) ddx=1;
            else if(k==keys::W) ddy=-1;
            else if(k==keys::S) ddy=1;
            if(ddx||ddy){
                int nx=s.blinky.x+ddx, ny=s.blinky.y+ddy; s.wrap(nx,ny);
                if(!s.solid_for_ghost(nx,ny)){ s.blinky.x=nx; s.blinky.y=ny; s.blinky.dx=ddx; s.blinky.dy=ddy; }
            }
        }

        // IA para fantasmas (si Blinky es humano, no moverlo por IA)
        if(!blinky_human) ghost_tick_ai(s,s.blinky);
        ghost_tick_ai(s,s.pinky);
        ghost_tick_ai(s,s.inky);
        ghost_tick_ai(s,s.clyde);

        handle_collisions(s);
        render(s);
        usleep(TICK_US);
    }

    // Resultado
    term::println_center("");
    if(s.tokens==0) term::println_center(term::bold("¡Has ganado!"));
    else if(s.lives<=0) term::println_center(term::bold("Game Over"));
    else if(s.stop) term::println_center(term::bold("Has regresado al menú"));

    term::println_center("Puntaje final: "+std::to_string(s.score));
    std::ofstream out("scores.txt", std::ios::app);
    out << "Jugador1," << s.score << "\n";
}

// ============================================================================
// Menú principal
// ============================================================================
struct MenuItem { std::string label; std::function<void()> action; };

void draw_title_ascii(){
  term::println_center(term::bold(" __            _  _              "));
  term::println_center(term::bold("|  _ \\ _ _  _|  \\/  | _ _ _ __   "));
  term::println_center(term::bold("| |) / _` |/ _| |\\/| |/ ` | ' \\  "));
  term::println_center(term::bold("|  _/ (| | (_| |  | | (| | | | | "));
  term::println_center(term::bold("||   \\,|\\_||  ||\\_,|| || "));
  term::println_center(term::dim ("Proyecto Pac-Man – Fase 3 (Terminal)"));
}
void draw_menu(const std::vector<MenuItem>& items,int selected){
  term::clear(); draw_title_ascii(); std::cout<<"\n";
  for(int i=0;i<(int)items.size();++i){
    std::string line=std::to_string(i+1)+") "+items[i].label;
    if(i==selected) line=term::inv(" "+line+" ");
    term::println_center(line);
  }
  std::cout<<"\n";
  term::println_center(term::dim("Flechas ↑/↓ o 1..4. Enter para elegir. 'q' para salir."));
}
static void select_mode(GameMode& mode){
    term::clear();
    term::println_center("Seleccione el modo de juego:");
    term::println_center("1) Un jugador (velocidad media)");
    term::println_center("2) Un jugador (velocidad rápida)");
    term::println_center("3) Dos jugadores (Blinky con WASD)");
    keys::RawGuard rg; keys::Key k=keys::NONE; while(k==keys::NONE) k=keys::read();
    if(k==keys::NUM1) mode=MODE_1;
    else if(k==keys::NUM2) mode=MODE_2;
    else if(k==keys::NUM3) mode=MODE_3;
    else mode=MODE_1;
}

// ============================================================================
// MAIN
// ============================================================================
int main(){
    srand((unsigned)time(nullptr));
    GameState state;
    GameMode mode=MODE_1;

    std::vector<MenuItem> items = {
        {"Iniciar partida", [&](){
            select_mode(mode);
            state = GameState();      // reinicia todo
            game_loop(state, mode);   // corre la partida; 'q' vuelve al menú
        }},
        {"Instrucciones", [](){ screen_instrucciones(); }},
        {"Puntajes", [](){ screen_puntajes(); }},
        {"Salir", [](){ exit(0); }}
    };

    int sel=0; keys::RawGuard rg;
    while(true){
        draw_menu(items, sel);
        keys::Key k=keys::NONE; do{k=keys::read();}while(k==keys::NONE);
        if(k==keys::QUIT) break;
        if(k==keys::UP)   sel=(sel-1+(int)items.size())%(int)items.size();
        if(k==keys::DOWN) sel=(sel+1)%(int)items.size();
        if(k==keys::NUM1) sel=0;
        if(k==keys::NUM2) sel=1;
        if(k==keys::NUM3) sel=2;
        if(k==keys::NUM4) sel=3;
        if(k==keys::ENTER){
            if(sel==(int)items.size()-1) break; // "Salir"
            items[sel].action();
        }
    }
    return 0;
}
