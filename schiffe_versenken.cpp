/*
 * Implementierung des Spiels "Schiffe versenken".
 *
 * Sie muessen diesen Programmcode NICHT VERSTEHEN - es implementiert lediglich
 * die Spiellogik und die Kommunikation mit einem Menschen bzw. IHREM Programm
 * laut der Spezifikation in der Angabe.
 *
 * Kompilieren Sie das Programm wie folgt:
 *
 *     g++ -std=c++11 -o schiffe_versenken schiffe_versenken.cpp
 *
 * Autor: Markus Wallerberger
 */
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <iomanip>

// C and POSIX headers
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

template <typename T> T checked(T errcode)
{
    if (errcode < T(0))
        throw std::runtime_error(strerror(errno));
    return errcode;
}

template <typename T> T monitored(T errcode)
{
    if (errcode < T(0))
        std::cerr << strerror(errno) << std::endl;
    return errcode;
}

// list of all children ever created
static std::vector<pid_t> all_children;
static std::vector<int> all_pipes;

class ChildProcess
{
public:
    ChildProcess() : _child_pid(-1), _fd_from_child(-1), _fd_to_child(-1) { }

    ChildProcess(std::string name)
        : ChildProcess()
    {
        // First, make pipes: pipe[0] <--- pipe[1]
        int to_child[2], from_child[2];
        checked(pipe(to_child));
        checked(pipe(from_child));

        // Then, fork
        _child_pid = checked(fork());
        if (_child_pid == 0) {
            // on child
            checked(dup2(to_child[0], STDIN_FILENO));
            checked(dup2(from_child[1], STDOUT_FILENO));
            checked(close(to_child[0]));
            checked(close(to_child[1]));
            checked(close(from_child[0]));
            checked(close(from_child[1]));
            execl(name.c_str(), name.c_str(), NULL);

            // This will only be reached if exec fails
            std::cerr << "\nFEHLER beim Ausführen von `" << name << "': "
                      << strerror(errno) << std::endl;
            exit(47);
        }

        // on parent
        monitored(close(to_child[0]));
        monitored(close(from_child[1]));
        _fd_from_child = from_child[0];
        _fd_to_child = to_child[1];

        all_children.push_back(_child_pid);
        all_pipes.push_back(_fd_from_child);
        all_pipes.push_back(_fd_to_child);
    }

    ~ChildProcess() {
        if (_fd_from_child >= 0) {
            monitored(close(_fd_from_child));
            _fd_from_child = -1;
        }
        if (_fd_to_child >= 0) {
            monitored(close(_fd_to_child));
            _fd_to_child = -1;
        }
        if (_child_pid >= 0) {
            monitored(kill(_child_pid, SIGKILL));
            monitored(waitpid(_child_pid, NULL, 0));
            _child_pid = -1;
        }
    }

    // copy-and-swap idiom
    ChildProcess(ChildProcess &&other) : ChildProcess() { swap(*this, other); }

    ChildProcess &operator=(ChildProcess &&other) { swap(*this, other); return *this; }

    friend void swap(ChildProcess &left, ChildProcess &right) {
        std::swap(left._child_pid, right._child_pid);
        std::swap(left._fd_from_child, right._fd_from_child);
        std::swap(left._fd_to_child, right._fd_to_child);
    }

    bool started() const { return _child_pid >= 0; }

    std::string getline(int maxlen=200, int timeout=2000) const {
        for(int tries=0; tries != 3; ++tries) {
            size_t linesize = _buffer.find('\n');
            if (linesize != std::string::npos) {
                std::string line = _buffer.substr(0, linesize+1);
                _buffer = _buffer.substr(linesize+1);
                return line;
            }

            pollfd poll_info;
            poll_info.fd = _fd_from_child;
            poll_info.events = POLLIN;

            // The process has not returned any data :(
            if (checked(poll(&poll_info, 1, timeout)) == 0) {
                throw std::runtime_error(
                    "Timeout: das Spieler-Programm hat innerhalb einiger Zeit "
                    "keine Zeile geschrieben");
            }

            char buffer[maxlen+1];
            int nbytes = read(_fd_from_child, buffer, maxlen);
            if (nbytes <= 0) {
                throw std::runtime_error(
                    "Das Spieler-Programm ist wahrscheinlich abgestürzt "
                    "oder ist zu frueh fertig");
            }
            buffer[nbytes] = 0;
            _buffer += std::string(buffer);
        }
        throw std::runtime_error(
                        "Das Spieler-Programm gibt zu lange Zeilen aus");
    }

    void send(const std::string &input) const {
        checked(write(_fd_to_child, input.c_str(), input.size() + 1));
    }

    int from_child_fd() const { return _fd_from_child; }

    int to_child_fd() const { return _fd_to_child; }

    int child_pid() const { return _child_pid; }

private:
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess operator=(const ChildProcess &) = delete;

    pid_t _child_pid;
    int _fd_from_child, _fd_to_child;
    mutable std::string _buffer;
};

class Player
{
public:
    Player() : _which('x') { }

    Player(char which, ChildProcess &&child = ChildProcess())
        : _which(which)
        , _child(std::move(child))
        , _live(0)
    {
        std::fill_n(&_board[0][0], 100, ' ');
    }

    bool is_machine() const { return _child.started(); }

    void die() { _live = 0; }

    bool alive() const { return _live > 0; }

    char which() const { return _which; }

    const ChildProcess &child() const { return _child; }

    std::string prompt() const {
        if (is_machine()) {
            return _child.getline();
        } else {
            std::string line;
            if (!getline(std::cin, line)) {
                std::cerr << "Aborted by user\n";
                exit(96);
            }
            return line;
        }
    }

    void send(char c) const {
        if (is_machine()) {
            char msg[3] = {c, '\n', '\0'};
            _child.send(std::string(msg));
        } else {
            std::cout << c << std::endl;
        }
    }

    void check_valid(int r, int c) const {
        if (r < 0 || r > 9)
            throw std::runtime_error("Ungueltige Zeile");
        if (c < 0 || c > 9)
            throw std::runtime_error("Ungueltige Spalte");
        if (r != 0 && _board[r-1][c] != ' ')
            throw std::runtime_error("Schiff beruehrt oben anderes Schiff");
        if (r != 9 && _board[r+1][c] != ' ')
            throw std::runtime_error("Schiff beruehrt unten anderes Schiff");
        if (c != 0 && _board[r][c-1] != ' ')
            throw std::runtime_error("Schiff beruehrt links anderes Schiff");
        if (c != 9 && _board[r][c+1] != ' ')
            throw std::runtime_error("Schiff beruehrt rechts anderes Schiff");
    }

    char board(int r, int c) const { return _board[r][c]; }

    void place(int r, int c, int size, bool downward) {
        if (size <= 0)
            throw std::runtime_error("Ungueltige Groesse");
        if (downward) {
            if (r > 6)
                throw std::runtime_error("Schiff hat nach unten nicht Platz.");
            for (int rr = r; rr != r + size; ++rr)
                check_valid(rr, c);
            for (int rr = r; rr != r + size; ++rr)
                _board[rr][c] = 'S';
        } else {
            if (c > 6)
                throw std::runtime_error("Schiff hat nach rechts nicht Platz.");
            for (int cc = c; cc != c + size; ++cc)
                check_valid(r, cc);
            for (int cc = c; cc != c + size; ++cc)
                _board[r][cc] = 'S';
        }
        _live += size;
    }

    bool incoming(int r, int c) {
        if (r < 0 || r > 9)
            throw std::runtime_error("Ungueltige Zeile");
        if (c < 0 || c > 9)
            throw std::runtime_error("Ungueltige Spalte");

        if (_board[r][c] == 'S') {
            _board[r][c] = 'X';
            --_live;
            return true;
        } else {
            if (_board[r][c] == ' ')
                _board[r][c] = 'o';
            return false;
        }
    }

private:
    char _which;
    ChildProcess _child;
    char _board[10][10];
    int _live;
};

void print_usage(std::string name)
{
    std::cerr << "Verwendung:\n\n"
              << "    " << name << " SPIELER_A SPIELER_B\n\n"
              << "Fuer SPIELER_A oder SPIELER_B kann eingesetzt werden:\n\n"
              << "    - 'mensch': Spieler spielt ueber die Tastatur\n"
              << "    - './PROGRAMMNAME': Spieler ist ein Programm\n";
}

Player make_player(std::string spec, char which)
{
    std::cerr << "Spieler " << which;
    if (spec == "mensch") {
        std::cerr << " ist ein Mensch ...\n";
        return Player(which);
    }
    if (spec.find('/') == std::string::npos) {
        throw std::runtime_error(
                "Programm '" + spec + "' muss ausfuehrbarer Pfad sein.\n"
                "(Vielleicht ist ./" + spec + " gemeint?)\n");
    }
    std::cerr << " ist das Programm `" << spec << "', starte dieses ...\n";
    return Player(which, ChildProcess(spec));
}

void print_board(std::ostream &out, const Player &player, bool visible, int row)
{
    if (row == 0 || row == 13) {
        out << player.which() << " | 0 1 2 3 4 5 6 7 8 9 | " << player.which();
    } else if (row == 1 || row == 12) {
        out << "--+---------------------+--";
    } else if (row <= 11) {
        row -= 2;
        out << row << " | ";
        for (int c = 0; c != 10; ++c) {
            char field = player.board(row, c);
            if (visible || field == 'o' || field == 'X')
                out << (field == ' ' ? '.' : field) << ' ';
            else
                out << ". ";
        }
        out << "| " << row;
    }
}

void print_boards(std::ostream &out, const Player &me, const Player &other,
                  bool other_visible)
{
    out << "\n";
    for (int r = 0; r != 14; ++r) {
        out << "        ";
        print_board(out, me, true, r);
        out << "          ";
        print_board(out, other, other_visible, r);
        out << "\n";
    }
    out << std::endl;
}

void place(Player &me)
{
    Player dummy = Player(me.which() == 'A' ? 'B' : 'A');
    bool am_human = !me.is_machine();
    for (int ship=1; ship<=4; ++ship) {
        // Be nice to humans
        if (am_human)
            print_boards(std::cerr, me, dummy, false);

        std::string line;
        for (bool ok = false; !ok;) {
            std::cerr << "Spieler " << me.which()
                      << " - Schiff #" << ship << " eingeben: ";
            line = me.prompt();
            if (line.empty())
                continue;

            std::istringstream linestr(line);
            int i, j;
            char c;
            linestr >> i >> j >> c >> std::ws;
            try {
                if (!linestr.eof() || linestr.fail()) {
                    throw std::runtime_error(
                        "Ungueltige Eingabe - erwarte eine Zeile der Form:\n\n"
                        "   zeile spalte richtung\n\n"
                        "zeile, spalte kann eine Zahl von 0-9 sein, Richtung muss\n"
                        "entweder U oder R sein");
                }
                if (c != 'R' && c != 'U') {
                    throw std::runtime_error(
                        "Ungueltige Richtung: muss entweder 'R' oder 'U' sein");
                }
                me.place(i, j, 4, c == 'U');
                ok = true;
                if (!am_human)
                    std::cerr << "[Erfolgreich eigegeben, aber geheim]\n";
            } catch(const std::runtime_error &e) {
                if (am_human) {
                    std::cerr << "Eingabefehler Spieler " << me.which() << ":\n"
                              << e.what() << std::endl;
                } else {
                    std::cerr << "\nBisher gesetzt:\n";
                    print_boards(std::cerr, me, dummy, false);
                    std::cerr << "\nEingeben wurde:\n" << line;
                    throw;
                }
            }
        }
    }
    if (am_human)
        print_boards(std::cerr, me, dummy, false);
}

void shoot(Player &me, Player &other)
{
    bool am_human = !me.is_machine();

    // Be nice to humans
    if (am_human)
        print_boards(std::cerr, me, other, false);

    std::string line;
    bool treffer;
    int i, j;
    for (bool ok = false; !ok;) {
        std::cerr << "Spieler " << me.which() << " - Zielfeld eingeben: ";
        line = me.prompt();
        if (line.empty())
            continue;

        std::istringstream linestr(line);
        linestr >> i >> j >> std::ws;
        try {
            if (!linestr.eof() || linestr.fail()) {
                throw std::runtime_error(
                    "Ungueltige Eingabe - erwarte eine Zeile der Form:\n\n"
                    "   zeile spalte\n\n"
                    "zeile, spalte kann eine Zahl von 0-9 sein");
            }
            treffer = other.incoming(i, j);
            ok = true;
        } catch(const std::runtime_error &e) {
            std::cerr << "Eingabefehler Spieler " << me.which() << ":\n"
                        << e.what() << std::endl;
            if (!am_human) {
                std::cerr << "\nJetziges Feld:\n";
                print_boards(std::cerr, me, other, true);
                std::cerr << "\nEingeben wurde:\n" << line;
                throw;
            }
        }
    }
    if (!am_human) {
        std::cerr << i << " " << j
            << (treffer ? " - TREFFER!" : " - daneben.")
            << (other.is_machine() && me.which() == 'A' ? "  ---  " : "\n");
    }
    if (!me.alive()) {
        me.send('L');
    } else if (treffer) {
        me.send(other.alive() ? 'T' : 'W');
    } else {
        me.send('F');
    }
}

extern "C" void signal_handler(int)
{
    // We kill all children and all pipes here for cleanup
    int nkilled = 0, nclosed = 0;
    for (pid_t pid : all_children) {
        if(kill(pid, SIGKILL) == 0) {
            monitored(waitpid(pid, NULL, 0));
            ++nkilled;
        }
    }
    for (int pipe : all_pipes) {
        if(close(pipe) == 0)
            ++nclosed;
    }
    if (nkilled || nclosed) {
        std::cerr << "\nABORT: Killed " << nkilled << " subprocesses, closed "
                  << nclosed << " pipes.\n";
    }
    std::quick_exit(1);
}

int main(int argc, char *argv[])
{
    // register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);

    // handle arguments
    std::vector<std::string> args(argv, argv + argc);
    if (args.size() != 3) {
        print_usage(args[0]);
        return 3;
    }

    // create players
    Player player_a, player_b;
    try {
        player_a = make_player(args[1], 'A');
        player_b = make_player(args[2], 'B');
    } catch(const std::runtime_error &e) {
        std::cerr << "Fehler: " << e.what() << std::endl;
        return 3;
    }

    // placement phase
    std::cerr << "\nSpieler A setzt Schiffe:\n";
    try {
        place(player_a);
    } catch(const std::runtime_error &e) {
        std::cerr << "\n\n" << e.what()
                  << "\nSpieler B hat gewonnen! (Illegale Platzierung von A)\n";
        return 2;
    }
    std::cerr << "\nSpieler B setzt Schiffe:\n";
    try {
        place(player_b);
    } catch(const std::runtime_error &e) {
        std::cerr << "\n\n" << e.what()
                  << "\nSpieler A hat gewonnen! (Illegale Platzierung von B)\n";
        return 1;
    }

    std::cerr << "\nLos gehts!\n";
    // shootout phase
    for (int move = 1; player_a.alive() && player_b.alive(); ++move) {
        if (move == 101) {
            std::cerr << "100 Züge gespielt - das ist genug.\n";
            player_a.die();
            player_b.die();
            break;
        }
        std::cerr << "Zug " << std::setw(3) << move << ": ";
        try {
            shoot(player_a, player_b);
        } catch(const std::runtime_error &e) {
            std::cerr << "\n\n" << e.what() << "\nIllegale Aktion von A\n";
            player_a.die();
            break;
        }
        try {
            shoot(player_b, player_a);
        } catch(const std::runtime_error &e) {
            std::cerr << "\n\n" << e.what() << "\nIllegaler Aktion von B\n";
            player_b.die();
            break;
        }
    }

    // scoring
    print_boards(std::cerr, player_a, player_b, true);
    if (player_a.alive()) {
        std::cerr << "Spieler A hat gewonnen!\n";
        return 1;
    } else if (player_b.alive()) {
        std::cerr << "Spieler B hat gewonnen!\n";
        return 2;
    } else {
        std::cerr << "Unentschieden ...\n";
        return 0;
    }
}
