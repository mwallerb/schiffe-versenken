#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// C and POSIX headers
#include <errno.h>
#include <fcntl.h>
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

    ChildProcess(ChildProcess &&other) : ChildProcess() { swap(*this, other); }

    ChildProcess(std::string name) {
        // First, make pipes: pipe[0] <--- pipe[1]
        int to_child[2], from_child[2], safety_pipe[2];
        char errormsg[100];

        checked(pipe(to_child));
        checked(pipe(from_child));
        checked(pipe(safety_pipe));
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

        if (read(safety_pipe[0], errormsg, sizeof(errormsg)) > 0) {
            monitored(close(_fd_from_child));
            monitored(close(_fd_to_child));
            throw std::runtime_error(
                    "Fehler beim Ausführen von '" + name + "': " + errormsg);
        }
    }

    ~ChildProcess() {
        if (_child_pid < 0)
            return;

        monitored(close(_fd_from_child));
        monitored(close(_fd_to_child));
        if(monitored(kill(_child_pid, SIGKILL)) == 0)
            monitored(waitpid(_child_pid, NULL, 0));
    }

    friend void swap(ChildProcess &left, ChildProcess &right) {
        std::swap(left._child_pid, right._child_pid);
        std::swap(left._fd_from_child, right._fd_from_child);
        std::swap(left._fd_to_child, right._fd_to_child);
    }

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
    Player(char which) : _which(which) { }

    char which() const { return _which; }

private:
    char _which;
};

typedef std::unique_ptr<Player> PlayerPtr;

class Human : public Player
{
public:
    Human(char which) : Player(which) { }
};

class Machine : public Player
{
public:
    Machine (char which) : Player(which) { }
};

void print_usage(std::string name)
{
    std::cerr << "Verwendung:\n\n"
              << "    " << name << " SPIELER_A SPIELER_B\n\n"
              << "Für SPIELER_A oder SPIELER_B kann eingesetzt werden:\n\n"
              << "    - 'mensch': Spieler spielt über die Tastatur\n"
              << "    - './PROGRAMMNAME': Spieler ist ein Programm\n";
}

PlayerPtr make_player(std::string spec, char which)
{
    if (spec == "mensch")
        return PlayerPtr(new Human(which));

    if (spec.find('/') == std::string::npos) {
        throw std::runtime_error(
                "Programm '" + spec + "' muss ausführbarer Pfad sein.\n"
                "(Vielleicht ist ./" + spec + " gemeint?)\n");
    }
    ChildProcess child(spec);

    return PlayerPtr(new Machine(which));
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
    try {
        PlayerPtr player_a = make_player(args[1], 'A');
        PlayerPtr player_b = make_player(args[2], 'B');
    } catch(const std::runtime_error &e) {
        std::cerr << "Fehler: " << e.what() << std::endl;
        return 3;
    }



    return 0;
}

