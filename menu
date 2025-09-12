#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cctype>

#ifdef _WIN32
  #include <windows.h>
  #include <conio.h>     // _kbhit, _getch
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/ioctl.h> // ioctl, TIOCGWINSZ
#endif

// ---------- Utilidades de consola ----------
namespace term {

// Limpia pantalla y mueve cursor al origen
inline void clear() {
  // ANSI clear screen + home
  std::cout << "\x1b[2J\x1b[H";
}

// Colores / estilos (ANSI). En Windows 10+ suelen funcionar.
// Para compatibilidad clásica se podría desactivar.
inline std::string dim(const std::string& s){ return "\x1b[2m"+s+"\x1b[0m"; }
inline std::string bold(const std::string& s){ return "\x1b[1m"+s+"\x1b[0m"; }
inline std::string inv(const std::string& s){ return "\x1b[7m"+s+"\x1b[0m"; }

#ifdef _WIN32
// Ancho de consola (Windows)
inline int width() {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
    return info.srWindow.Right - info.srWindow.Left + 1;
  }
  return 80;
}
#else
// Ancho de consola (Unix)
inline int width() {
  winsize w{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) return w.ws_col;
  return 80;
}
#endif

// Centrar un texto en el ancho actual
inline void println_center(const std::string& s) {
  int W = width();
  int n = (int)s.size();
  int pad = (W - n) / 2;
  if (pad < 0) pad = 0;
  std::cout << std::string(pad, ' ') << s << "\n";
}

} // namespace term

// ---------- Lectura de teclas no bloqueante / cruda ----------
namespace keys {

enum Key {
  NONE=0, ENTER, UP, DOWN, NUM1, NUM2, NUM3, NUM4, QUIT
};

#ifndef _WIN32
struct RawGuard {
  termios old{};
  bool active=false;
  RawGuard() {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &old) == -1) return;
    termios raw = old;
    raw.c_lflag &= ~(ICANON | ECHO); // sin buffer de línea, sin eco
    raw.c_cc[VMIN] = 0;  // lectura no bloqueante
    raw.c_cc[VTIME] = 1; // timeout 0.1s
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) active = true;
  }
  ~RawGuard() {
    if (active) tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
  }
};
#endif

inline Key read() {
#ifdef _WIN32
  // Modo Windows: _kbhit / _getch
  if (!_kbhit()) return NONE;
  int c = _getch();
  if (c == 0 || c == 224) { // teclas especiales
    int s = _getch();
    if (s == 72) return UP;
    if (s == 80) return DOWN;
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
  // Modo Unix: lectura cruda, secuencias ANSI para flechas
  unsigned char buf[3];
  ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
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
  }
  if (n == 3 && buf[0] == 0x1b && buf[1] == '[') {
    if (buf[2] == 'A') return UP;
    if (buf[2] == 'B') return DOWN;
  }
  return NONE;
#endif
}

} // namespace keys

// ---------- Pantallas ----------
void screen_wait_anykey(const std::string& title, const std::vector<std::string>& lines) {
  term::clear();
  term::println_center(term::bold(title));
  std::cout << "\n";
  for (auto& l : lines) term::println_center(l);
  std::cout << "\n";
  term::println_center(term::dim("Presiona cualquier tecla para volver al menú..."));

#ifdef _WIN32
  _getch();
#else
  keys::RawGuard rg;
  // Espera a que llegue una tecla cualquiera
  while (true) {
    auto k = keys::read();
    if (k != keys::NONE) break;
  }
#endif
}

void screen_instrucciones() {
  screen_wait_anykey("INSTRUCCIONES",
    {
      "Objetivo: Pac-Man recolecta todas las fichas evitando a los fantasmas.",
      "Controles P1 (Pac-Man): Flechas \xE2\x86\x91 \xE2\x86\x93 \xE2\x86\x90 \xE2\x86\x92",
      "Controles P2 (Fantasma): W A S D",
      "Cada entidad correra en su propio hilo en la fase de juego.",
    }
  );
}

void screen_puntajes() {
  screen_wait_anykey("PUNTAJES",
    {
      "Aqui se mostraran los puntajes destacados.",
      "Persistencia se implementara en la fase de puntajes.",
    }
  );
}

void screen_iniciar_partida_stub() {
  screen_wait_anykey("INICIAR PARTIDA",
    {
      "Stub de juego. En Fase 03 se activara la logica concurrente y visualizacion.",
    }
  );
}

// ---------- Menú ----------
struct MenuItem {
  std::string label;
  std::function<void()> action;
};

void draw_title_ascii() {
  // ASCII-Art opcional (simple, centrado)
  term::println_center(term::bold(" ____            __  __              "));
  term::println_center(term::bold("|  _ \\ __ _  ___|  \\/  | __ _ _ __   "));
  term::println_center(term::bold("| |_) / _` |/ __| |\\/| |/ _` | '_ \\ "));
  term::println_center(term::bold("|  __/ (_| | (__| |  | | (_| | | | | "));
  term::println_center(term::bold("|_|   \\__,_|\\___|_|  |_|\\__,_|_| |_| "));
  term::println_center(term::dim("Proyecto 01 – Menú de inicio"));
}

void draw_menu(const std::vector<MenuItem>& items, int selected) {
  term::clear();
  draw_title_ascii();
  std::cout << "\n";
  for (int i = 0; i < (int)items.size(); ++i) {
    std::string line = std::to_string(i+1) + ") " + items[i].label;
    if (i == selected) line = term::inv(" " + line + " ");
    term::println_center(line);
  }
  std::cout << "\n";
  term::println_center(term::dim("Usa flechas ↑/↓ o teclas 1..4. Enter para seleccionar. 'q' para salir."));
}

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  std::vector<MenuItem> items = {
    { "Iniciar partida", [](){ screen_iniciar_partida_stub(); } },
    { "Instrucciones",   [](){ screen_instrucciones(); } },
    { "Puntajes",        [](){ screen_puntajes(); } },
    { "Salir",           [](){ /* manejar fuera del loop */ } }
  };

  int selected = 0;
#ifndef _WIN32
  keys::RawGuard rg; // modo raw en Unix; en Windows no es necesario
#endif

  while (true) {
    draw_menu(items, selected);
    // Leer tecla
    keys::Key k = keys::read();

    // También aceptamos entrada por línea si el usuario presiona Enter con números
    // (fallback cuando no llegan flechas).
    if (k == keys::NONE) {
      // Pequeño fallback bloqueante si no estamos en modo raw
#ifndef _WIN32
      // nada; read() ya tiene timeout corto
#else
      // En win ya usamos kbhit, así que NONE significa no hay tecla
#endif
      continue;
    }

    if (k == keys::QUIT) break;

    if (k == keys::UP)    { selected = (selected - 1 + (int)items.size()) % (int)items.size(); }
    if (k == keys::DOWN)  { selected = (selected + 1) % (int)items.size(); }

    if (k == keys::NUM1) selected = 0;
    if (k == keys::NUM2) selected = 1;
    if (k == keys::NUM3) selected = 2;
    if (k == keys::NUM4) selected = 3;

    if (k == keys::ENTER) {
      if (selected == 3) break;      // “Salir”
      items[selected].action();       // Ejecutar pantalla
    }
  }

  term::clear();
  term::println_center("Gracias por usar el menú. ¡Hasta pronto!");
  std::cout << "\n";
  return 0;
}
