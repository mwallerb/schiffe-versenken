#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

// C and POSIX headers
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
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

class ChildProcess
{
public:
    ChildProcess() : _child_pid(-1) { }

    ChildProcess(std::string name) {
        // First, make pipes: pipe[0] <--- pipe[1]
        int to_child[2], from_child[2], safety_pipe[2];
        char errormsg[100];

        checked(pipe(to_child));
        checked(pipe(from_child));
        checked(pipe(safety_pipe));
        checked(fcntl(safety_pipe[0], F_SETFD, FD_CLOEXEC));
        checked(fcntl(safety_pipe[1], F_SETFD, FD_CLOEXEC));

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
            strncpy(errormsg, strerror(errno), sizeof(errormsg));
            write(safety_pipe[1], errormsg, sizeof(errormsg));
            exit(47);
        }

        // on parent
        monitored(close(to_child[0]));
        monitored(close(from_child[1]));
        _fd_from_child = from_child[0];
        _fd_to_child = to_child[1];

        // Attempt at portably guessing when child fails ... this can be
        // brittle due to scheduling delays, however :(
        pollfd poll_info;
        poll_info.fd = safety_pipe[0];
        poll_info.events = POLLIN;
        if (checked(poll(&poll_info, 1, 100)) > 0) {
            monitored(read(safety_pipe[0], errormsg, sizeof(errormsg)));
            monitored(close(_fd_from_child));
            monitored(close(_fd_to_child));
            throw std::runtime_error(
                    "Fehler beim Ausführen von '" + name + "': " + errormsg);
        }
        checked(close(safety_pipe[0]));
        checked(close(safety_pipe[1]));
    }

    ~ChildProcess() {
        if (_child_pid < 0)
            return;

        monitored(close(_fd_from_child));
        monitored(close(_fd_to_child));
        if(monitored(kill(_child_pid, SIGKILL)) == 0)
            monitored(waitpid(_child_pid, NULL, 0));
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

    int from_child_fd() const { return _fd_from_child; }

    int to_child_fd() const { return _fd_to_child; }

    int child_pid() const { return _child_pid; }

private:
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess operator=(const ChildProcess &) = delete;

    pid_t _child_pid;
    int _fd_from_child, _fd_to_child;
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
        std::fill(_board[0], _board[10], ' ');
    }

    bool is_machine() const { return _child.started(); }

    bool alive() const { return _live > 0; }

    char which() const { return _which; }

    const ChildProcess &child() const { return _child; }

    std::string prompt() const {
        if (is_machine()) {
            //FIXME
            return "";
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
            //FIXME
            return;
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
            std::cerr << "Schiff #" << ship << " eingeben: ";
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
            } catch(const std::runtime_error &e) {
                std::cerr << "Eingabefehler Spieler " << me.which() << ":\n"
                          << e.what() << std::endl;
                if (!am_human) {
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
    for (bool ok = false; !ok;) {
        std::cerr << "Zielfeld eingeben: ";
        line = me.prompt();
        if (line.empty())
            continue;

        std::istringstream linestr(line);
        int i, j;
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

    if (!me.alive())
        me.send('L');
    else if (treffer)
        me.send(other.alive() ? 'T' : 'W');
    else
        me.send('F');
}

int main(int argc, char *argv[])
{
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
    bool both_ai = player_a.is_machine() && player_b.is_machine();

    // placement phase
    std::cerr << "\nSpieler A setzt Schiffe:\n";
    try {
        place(player_a);
    } catch(const std::runtime_error &e) {
        std::cerr << "Spieler B hat gewonnen! (Illegale Platzierung von A)\n";
        return 2;
    }
    std::cerr << "\nSpieler B setzt Schiffe:\n";
    try {
        place(player_b);
    } catch(const std::runtime_error &e) {
        std::cerr << "Spieler A hat gewonnen! (Illegale Platzierung von B)\n";
        return 1;
    }
    if (both_ai)
        print_boards(std::cerr, player_a, player_b, true);

    // shootout phase
    do {
        try {
            shoot(player_a, player_b);
        } catch(const std::runtime_error &e) {
            std::cerr << "Spieler B hat gewonnen! (Illegaler Zug von A)\n";
            return 2;
        }
        try {
            shoot(player_b, player_a);
        } catch(const std::runtime_error &e) {
            std::cerr << "Spieler A hat gewonnen! (Illegaler Zug von B)\n";
            return 1;
        }
    } while (player_a.alive() && player_b.alive());

    // scoring
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
