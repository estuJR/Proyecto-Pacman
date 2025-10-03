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
#include <sstream>
#include <algorithm>

// ============================================================================
// Terminal UI
// ============================================================================
namespace term {
inline void clear(){ std::cout << "\x1b[2J\x1b[H"; }
inline std::string bold(const std::string& s){ return "\x1b[1m"+s+"\x1b[0m"; }
inline std::string dim (const std::string& s){ return "\x1b[2m"+s+"\x1b[0m"; }
inline std::string inv (const std::string& s){ return "\x1b[7m"+s+"\x1b[0m"; }
inline int width(){ winsize w{}; ioctl(STDOUT_FILENO, TIOCGWINSZ, &w); return (w.ws_col>0)? w.ws_col : 80; }
inline void println_center(const std::string& s){ int W=width(); int pad=(W-(int)s.size())/2; if(pad<0) pad=0; std::cout<<std::string(pad,' ')<<s<<"\n"; }
}

// ============================================================================
// Modo teclado raw (y helpers para alternar cooked cuando pedimos iniciales)
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
        if(c=='\n'||c=='\r') return ENTER;
        if(c=='q'||c=='Q')   return QUIT;
        if(c=='w'||c=='W')   return W;
        if(c=='a'||c=='A')   return A;
        if(c=='s'||c=='S')   return S;
        if(c=='d'||c=='D')   return D;
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

// Helpers para alternar cooked/raw cuando usamos std::cin para iniciales
namespace tty_mode {
static bool saved=false;
static termios saved_t{};
inline void to_cooked(){
    if(!isatty(STDIN_FILENO)) return;
    if(!saved){ tcgetattr(STDIN_FILENO,&saved_t); saved=true; }
    termios cooked = saved_t;
    cooked.c_lflag |= (ICANON|ECHO);
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&cooked);
}
inline void to_raw(){
    if(!isatty(STDIN_FILENO)) return;
    termios raw{};
    if(!saved){ tcgetattr(STDIN_FILENO,&saved_t); saved=true; }
    raw=saved_t;
    raw.c_lflag &= ~(ICANON|ECHO);
    raw.c_cc[VMIN]=0; raw.c_cc[VTIME]=1;
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw);
}
}

// ============================================================================
// Pantallas simples
// ============================================================================
void screen_wait_anykey(const std::string& title, const std::vector<std::string>& lines){
    term::clear();
    term::println_center(term::bold(title)); std::cout<<"\n";
    for(const auto& l: lines) term::println_center(l);
    std::cout<<"\n";
    term::println_center(term::dim("Presiona cualquier tecla para volver..."));
    keys::RawGuard rg; while(keys::read()==keys::NONE);
}
void screen_instrucciones(){
    screen_wait_anykey("INSTRUCCIONES",{
        "Objetivo: Pac-Man recolecta todas las fichas y evita fantasmas.",
        "Controles Pac-Man: Flechas ← ↑ → ↓ | 'q' regresa al menú",
        "Modo 3: Blinky (rojo) con WASD",
        "Power (P): los fantasmas huyen temporalmente.",
        "Puntajes con iniciales y registro de MAX/MIN en scores.txt"
    });
}
void screen_puntajes(){
    // Solo pantalla informativa (el archivo se actualiza al final de cada partida)
    screen_wait_anykey("PUNTAJES",{
        "Revisa scores.txt: contiene historial y al final las marcas MAX/MIN.",
        "Formato: AAA,12345 (iniciales, puntaje)"
    });
}

// ============================================================================
// Estado del juego
// ============================================================================
static const char WALL='#', TOKEN='.', POWER='P', EMPTY=' ', DOOR='-';
static const int  DIRS[4][2]={{0,-1},{-1,0},{0,1},{1,0}}; // U L D R

struct GameState {
    // Mapas
    std::vector<std::string> maze_base, maze_live;
    int H=0, W=0;

    // Pac-Man
    int px=1, py=1, pdx=1, pdy=0;
    int lives=3, score=0, tokens=0;

    // Power
    bool power=false; int powerTimer=0;

    // Control
    bool stop=false;

    // Jugador
    std::string playerInitials="???";

    // Fantasmas
    struct Ghost{
        std::string name;
        int x=1,y=1,dx=0,dy=0;
        int color=31;       // 31 rojo, 35 rosa, 36 cian, 33 naranja
        bool inHouse=true;
        int release=0;      // ticks para salir
        int last_tick=0;    // sincronización con el tick global
    };
    Ghost blinky, pinky, inky, clyde;

    // Casa de fantasmas
    int houseX=0, houseY=0, doorY=0, doorX1=0, doorX2=0;

    // ---------- 🔒 SINCRONIZACIÓN ----------
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;        // 1) Mutex
    pthread_cond_t  cond_tick   = PTHREAD_COND_INITIALIZER; // 2) Condición: nuevo tick
    pthread_cond_t  cond_render = PTHREAD_COND_INITIALIZER; // 3) Condición: todos listos para render
    int tick_id=0;               // contador de ticks
    int ghosts_done=0;           // cuántos fantasmas terminaron su paso
    static constexpr int NUM_GHOSTS = 4;

    // Modo 3: Blinky humano
    bool blinky_human=false;
    int  blinky_cmd_dx=0, blinky_cmd_dy=0;

    GameState(){
        // ---- Mapa estilo clásico (simétrico) ----
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

        // Rellenar espacios vacíos con puntos
        for(int y=0;y<H;y++) for(int x=0;x<W;x++)
            if(maze_live[y][x]==' ') maze_live[y][x]='.';

        // Contar comestibles
        tokens=0;
        for(auto &row: maze_live) for(char c: row) if(c=='.'||c=='P') tokens++;

        // Pac-Man
        px=1; py=1; pdx=1; pdy=0;

        // Detectar puerta/casa
        for(int y=0;y<H;y++){
            for(int x=0;x<W;x++){
                if(maze_base[y][x]==DOOR){
                    doorY=y; int a=x,b=x;
                    while(a>0 && maze_base[y][a-1]==DOOR) a--;
                    while(b+1<W && maze_base[y][b+1]==DOOR) b++;
                    doorX1=a; doorX2=b; houseX=(a+b)/2; houseY=y+1;
                }
            }
        }

        // Quitar puntos en y alrededor de la casa (para que no aparezcan dentro/arriba)
        auto clear_dot=[&](int X,int Y){
            if(Y>=0&&Y<H&&X>=0&&X<W){
                if(maze_live[Y][X]=='.' || maze_live[Y][X]=='P'){
                    maze_live[Y][X]=' ';
                    --tokens;
                }
            }
        };
        for(int y=houseY-2; y<=houseY+2; ++y)
            for(int x=doorX1-2; x<=doorX2+2; ++x)
                clear_dot(x,y);
        for(int x=doorX1-3; x<=doorX2+3; ++x) clear_dot(x,doorY);

        // Fantasmas con salida escalonada (respawn rápido tras ser comidos)
        blinky={"Blinky",houseX,houseY,0,0,31,true,5,0};
        pinky ={"Pinky", houseX,houseY,0,0,35,true,25,0};
        inky  ={"Inky",  houseX,houseY,0,0,36,true,45,0};
        clyde ={"Clyde", houseX,houseY,0,0,33,true,65,0};
    }

    inline void wrap(int &x,int &y) const { if(x<0)x=W-1; if(x>=W)x=0; if(y<0)y=H-1; if(y>=H)y=0; }
    inline bool is_wall(int x,int y) const { return maze_base[y][x]==WALL; }
    inline bool is_door(int x,int y) const { return maze_base[y][x]==DOOR; }
    inline bool solid_for_pacman(int x,int y) const { return is_wall(x,y)||is_door(x,y); }
    inline bool solid_for_ghost (int x,int y) const { return is_wall(x,y); } // puerta la cruzan
};

// ============================================================================
// Render
// ============================================================================
static inline std::string color_cell(char c){
    switch(c){
        case '#': return "\033[34m#\033[0m"; // paredes azules
        case '.': return "\033[37m.\033[0m";
        case 'P': return "\033[32mP\033[0m";
        case '-': return "\033[36m-\033[0m";
        case ' ': return " ";
        default:  return std::string(1,c);
    }
}
static inline std::string ghost_symbol(int ansiColor, bool frightened){
    if(frightened) return "\033[34mF\033[0m";  // azul en modo asustado
    return "\033["+std::to_string(ansiColor)+"mF\033[0m";
}
static inline std::string ghost_at(const GameState& s,int x,int y){
    auto gstr = [&](const GameState::Ghost& g)->std::string{
        if(!g.inHouse && g.x==x && g.y==y) return ghost_symbol(g.color, s.power);
        return {};
    };
    std::string r=gstr(s.blinky); if(!r.empty()) return r;
    r=gstr(s.pinky);  if(!r.empty()) return r;
    r=gstr(s.inky);   if(!r.empty()) return r;
    r=gstr(s.clyde);  return r;
}
static void render_locked(const GameState& s){
    term::clear();
    term::println_center(term::bold("=== PAC-MAN ==="));
    std::cout<<"\n";
    int margin=(term::width()-s.W)/2; if(margin<0) margin=0;

    for(int y=0;y<s.H;y++){
        std::string line;
        for(int x=0;x<s.W;x++){
            if(x==s.px && y==s.py){
                line += "\033[93mC\033[0m"; // Pac-Man visible
            } else {
                std::string gh=ghost_at(s,x,y);
                if(!gh.empty()) line += gh;
                else {
                    char live = s.maze_live[y][x];
                    if(live=='.' || live=='P' || live==' ')
                        line += color_cell(live);
                    else
                        line += color_cell(s.maze_base[y][x]);
                }
            }
        }
        if(margin) std::cout<<std::string(margin,' ');
        std::cout<<line<<"\n";
    }
    std::cout<<"\n";
    std::string hud = "Jugador: " + s.playerInitials + " | Puntos: " + std::to_string(s.score) +
                      " | Vidas: " + std::to_string(s.lives) + (s.power ? " | POWER!" : "");
    term::println_center(hud);
    std::cout.flush();
}

// ============================================================================
// Comer y mover
// ============================================================================
static inline void eat_cell(GameState& s,int x,int y){
    char &c = s.maze_live[y][x];
    if(c==TOKEN){ s.score+=10; s.tokens--; c=EMPTY; }
    else if(c==POWER){ s.score+=50; s.tokens--; c=EMPTY; s.power=true; s.powerTimer=110; } // power un poco más corto
}
static inline void move_pacman(GameState& s,int dx,int dy){
    int nx=s.px+dx, ny=s.py+dy; s.wrap(nx,ny);
    if(!s.solid_for_pacman(nx,ny)){
        s.px=nx; s.py=ny; s.pdx=dx; s.pdy=dy;
        eat_cell(s,nx,ny);
    }
}

// ============================================================================
// IA Fantasmas
// ============================================================================
static inline int dist2(int ax,int ay,int bx,int by){ int dx=ax-bx, dy=ay-by; return dx*dx+dy*dy; }

static inline void compute_target(const GameState& s,
                                  const GameState::Ghost& blinky,
                                  const GameState::Ghost& me,
                                  int &tx, int &ty)
{
    if(s.power){
        // Huir hacia la esquina más alejada de Pac-Man
        int cands[4][2]={{1,1},{s.W-2,1},{1,s.H-2},{s.W-2,s.H-2}};
        int best=-1e9; tx=1; ty=1;
        for(auto &c: cands){
            int d=-dist2(c[0],c[1],s.px,s.py);
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
        if(d>64){ tx=s.px; ty=s.py; }
        else    { tx=1; ty=s.H-2; }
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
        if(ndx==-g.dx && ndy==-g.dy) continue; // evita reversa inmediata
        if(!can_move_ghost(s,g,ndx,ndy)) continue;
        int nx=g.x+ndx, ny=g.y+ndy; s.wrap(nx,ny);
        int dd=dist2(nx,ny,tx,ty);
        if(dd<best){ best=dd; bdx=ndx; bdy=ndy; }
    }
    if(best==1e9){
        // sin opciones, intenta reversa
        if(can_move_ghost(s,g,-g.dx,-g.dy)){ g.x-=g.dx; g.y-=g.dy; g.dx=-g.dx; g.dy=-g.dy; return; }
        // o cualquier válida
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
        g.inHouse=false; g.dx=1; g.dy=0; // fuera
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
                s.score+=200;                // fantasma comido
                g.inHouse=true;              // respawn rápido
                g.x=s.houseX; g.y=s.houseY; g.dx=g.dy=0; g.release=5;
            }else{
                s.lives--;                   // Pac-Man pierde vida
                s.px=1; s.py=1; s.pdx=1; s.pdy=0;
            }
        }
    };
    touch(s.blinky); touch(s.pinky); touch(s.inky); touch(s.clyde);

    if(s.power){
        s.powerTimer--;
        if(s.powerTimer<=0) s.power=false;
    }
}

// ============================================================================
// Hilos y sincronización
// ============================================================================
enum GameMode { MODE_1=0, MODE_2=1, MODE_3=2 };
static int TICK_US=90000;

struct GhostArgs { GameState* st; GameState::Ghost* g; GameMode mode; };

void* pacman_thread(void* arg){
    GameState* s=(GameState*)arg;
    keys::RawGuard rg;
    while(true){
        keys::Key k = keys::read();

        pthread_mutex_lock(&s->mtx);
        if(s->stop || s->lives<=0 || s->tokens<=0){ pthread_mutex_unlock(&s->mtx); break; }

        // Entrada Pac-Man
        if(k==keys::QUIT){ s->stop=true; pthread_mutex_unlock(&s->mtx); break; }
        int dx=0,dy=0;
        if(k==keys::LEFT)  dx=-1;
        if(k==keys::RIGHT) dx=+1;
        if(k==keys::UP)    dy=-1;
        if(k==keys::DOWN)  dy=+1;
        if(dx||dy) move_pacman(*s,dx,dy);

        // Modo 3: comando humano para Blinky
        s->blinky_cmd_dx = 0; s->blinky_cmd_dy = 0;
        if(s->blinky_human){
            if(k==keys::A) s->blinky_cmd_dx=-1;
            else if(k==keys::D) s->blinky_cmd_dx=+1;
            else if(k==keys::W) s->blinky_cmd_dy=-1;
            else if(k==keys::S) s->blinky_cmd_dy=+1;
        }

        // Lanzar tick
        s->ghosts_done = 0;
        s->tick_id++;
        pthread_cond_broadcast(&s->cond_tick);

        // Esperar a TODOS los fantasmas
        while(!s->stop && s->ghosts_done < GameState::NUM_GHOSTS)
            pthread_cond_wait(&s->cond_render,&s->mtx);

        // Colisiones + render
        if(!s->stop){ handle_collisions(*s); render_locked(*s); }

        bool finished = (s->stop || s->lives<=0 || s->tokens<=0);
        pthread_mutex_unlock(&s->mtx);

        if(finished) break;
        usleep(TICK_US);
    }
    return nullptr;
}

void* ghost_thread(void* arg){
    GhostArgs* a=(GhostArgs*)arg;
    GameState* s=a->st;
    GameState::Ghost* g=a->g;
    GameMode mode=a->mode;

    while(true){
        pthread_mutex_lock(&s->mtx);
        if(s->stop || s->lives<=0 || s->tokens<=0){ pthread_mutex_unlock(&s->mtx); break; }

        // Esperar nuevo tick
        while(!s->stop && g->last_tick >= s->tick_id)
            pthread_cond_wait(&s->cond_tick,&s->mtx);
        if(s->stop || s->lives<=0 || s->tokens<=0){ pthread_mutex_unlock(&s->mtx); break; }

        // Ejecutar un paso
        if(mode==MODE_3 && g->name=="Blinky" && !g->inHouse){
            // Blinky humano si hay comando válido
            int ddx=s->blinky_cmd_dx, ddy=s->blinky_cmd_dy;
            if(ddx||ddy){
                int nx=g->x+ddx, ny=g->y+ddy; s->wrap(nx,ny);
                if(!s->solid_for_ghost(nx,ny)){ g->x=nx; g->y=ny; g->dx=ddx; g->dy=ddy; }
            }else{
                ghost_tick_ai(*s,*g);
            }
        }else{
            ghost_tick_ai(*s,*g);
        }

        // Marcar tick consumido
        g->last_tick = s->tick_id;

        // Contabilizar fantasma listo
        s->ghosts_done++;
        if(s->ghosts_done >= GameState::NUM_GHOSTS)
            pthread_cond_signal(&s->cond_render);

        pthread_mutex_unlock(&s->mtx);
        usleep(TICK_US/2);
    }

    return nullptr;
}

// ============================================================================
// Menú + Puntajes
// ============================================================================
struct MenuItem { std::string label; std::function<void()> action; };

void draw_title_ascii(){
  term::println_center(term::bold(" ____            __  __              "));
  term::println_center(term::bold("|  _ \\ __ _  ___|  \\/  | __ _ _ __   "));
  term::println_center(term::bold("| |_) / _` |/ __| |\\/| |/ _` | '_ \\  "));
  term::println_center(term::bold("|  __/ (_| | (__| |  | | (_| | | | | "));
  term::println_center(term::bold("|_|   \\__,_|\\___|_|  |_|\\__,_|_| |_| "));
  term::println_center(term::dim ("Proyecto Pac-Man – Fase 3 (Terminal + Sync + Puntajes)"));
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

// Selección de modo
static void select_mode(GameMode& mode, GameState& state){
    term::clear();
    term::println_center("Seleccione el modo de juego:");
    term::println_center("1) Un jugador (velocidad media)");
    term::println_center("2) Un jugador (velocidad rápida)");
    term::println_center("3) Dos jugadores (Blinky con WASD)");
    keys::RawGuard rg; keys::Key k=keys::NONE; while(k==keys::NONE) k=keys::read();
    if(k==keys::NUM1){ mode=MODE_1; TICK_US=90000; state.blinky_human=false; }
    else if(k==keys::NUM2){ mode=MODE_2; TICK_US=60000; state.blinky_human=false; }
    else if(k==keys::NUM3){ mode=MODE_3; TICK_US=90000; state.blinky_human=true; }
    else { mode=MODE_1; TICK_US=90000; state.blinky_human=false; }
}

// Guardado de puntajes (reescribe el archivo preservando historial y agrega MAX/MIN al final)
static void save_score_and_rewrite_file(const std::string& initials, int score){
    // Cargar historial previo
    std::vector<std::pair<std::string,int>> entries;
    {
        std::ifstream in("scores.txt");
        std::string line;
        while(std::getline(in,line)){
            if(line.empty()) continue;
            // Saltar líneas MAX/MIN antiguas
            if(line.rfind("MAX,",0)==0 || line.rfind("MIN,",0)==0 || line=="---") continue;
            std::stringstream ss(line);
            std::string name; int sc;
            if(std::getline(ss,name,',') && (ss>>sc)){
                entries.emplace_back(name,sc);
            }
        }
    }
    // Agregar la nueva
    entries.emplace_back(initials,score);

    // Calcular max/min
    int maxScore = entries.front().second, minScore = entries.front().second;
    for(auto &e: entries){ if(e.second>maxScore) maxScore=e.second; if(e.second<minScore) minScore=e.second; }

    // Reescribir todo el archivo ordenado por fecha de inserción (historial) + bloque MAX/MIN
    std::ofstream out("scores.txt", std::ios::trunc);
    for(auto &e: entries) out << e.first << "," << e.second << "\n";
    out << "---\n";
    out << "MAX," << maxScore << "\n";
    out << "MIN," << minScore << "\n";
    out.close();
}

// Partida (montaje hilos y join)
static void start_game(GameState& state, GameMode mode){
    pthread_mutex_lock(&state.mtx);
    state.stop=false; state.tick_id=0; state.ghosts_done=0;
    state.blinky_cmd_dx=state.blinky_cmd_dy=0;
    pthread_mutex_unlock(&state.mtx);

    pthread_t tpac, tg1, tg2, tg3, tg4;
    GhostArgs a1{&state, &state.blinky, mode};
    GhostArgs a2{&state, &state.pinky , mode};
    GhostArgs a3{&state, &state.inky  , mode};
    GhostArgs a4{&state, &state.clyde , mode};

    pthread_create(&tpac, nullptr, pacman_thread, &state);
    pthread_create(&tg1,  nullptr, ghost_thread,  &a1);
    pthread_create(&tg2,  nullptr, ghost_thread,  &a2);
    pthread_create(&tg3,  nullptr, ghost_thread,  &a3);
    pthread_create(&tg4,  nullptr, ghost_thread,  &a4);

    pthread_join(tpac,nullptr);
    pthread_join(tg1,nullptr);
    pthread_join(tg2,nullptr);
    pthread_join(tg3,nullptr);
    pthread_join(tg4,nullptr);

    pthread_mutex_lock(&state.mtx);
    std::cout<<"\n";
    if(state.tokens==0)      term::println_center(term::bold("¡Has ganado!"));
    else if(state.lives<=0)  term::println_center(term::bold("Game Over"));
    else if(state.stop)      term::println_center(term::bold("Has regresado al menú"));
    term::println_center("Puntaje final: "+std::to_string(state.score));
    // Guardar/reescribir scores.txt con historial + MAX/MIN
    save_score_and_rewrite_file(state.playerInitials, state.score);
    pthread_mutex_unlock(&state.mtx);
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
            // Pedir iniciales en modo "cooked" para que std::cin funcione
            tty_mode::to_cooked();
            term::clear();
            term::println_center("Ingresa tus iniciales (3 letras, p.ej. JAL): ");
            std::string initials;
            std::cout << "\n> ";
            std::cin  >> initials;
            if(initials.size()>3) initials = initials.substr(0,3);
            for(char &ch: initials) if(ch>='a'&&ch<='z') ch = char(ch-'a'+'A'); // a MAYÚSCULAS
            tty_mode::to_raw();

            select_mode(mode, state);

            // Reiniciar estado y asignar iniciales
            state = GameState();
            state.playerInitials = (initials.size()==3? initials : "???");

            start_game(state, mode);
            screen_puntajes();
        }},
        {"Instrucciones", [](){ screen_instrucciones(); }},
        {"Puntajes",     [](){ screen_puntajes(); }},
        {"Salir",        [](){ exit(0); }}
    };

    int sel=0;
    keys::RawGuard rg;
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
// ============================================================================
// FIN
// ============================================================================
