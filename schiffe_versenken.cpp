#include <iostream>
#include <memory>
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

    ChildProcess(ChildProcess &&other) : ChildProcess() { swap(*this, other); }

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
    Player(char which, ChildProcess &&child = ChildProcess())
        : _which(which)
        , _child(std::move(child))
    { }

    char which() const { return _which; }

private:
    char _which;
    ChildProcess _child;
};

void print_usage(std::string name)
{
    std::cerr << "Verwendung:\n\n"
              << "    " << name << " SPIELER_A SPIELER_B\n\n"
              << "Für SPIELER_A oder SPIELER_B kann eingesetzt werden:\n\n"
              << "    - 'mensch': Spieler spielt über die Tastatur\n"
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
                "Programm '" + spec + "' muss ausführbarer Pfad sein.\n"
                "(Vielleicht ist ./" + spec + " gemeint?)\n");
    }
    std::cerr << " ist das Programm `" << spec << "', starte dieses ...\n";
    return Player(which, ChildProcess(spec));
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
        Player player_a = make_player(args[1], 'A');
        Player player_b = make_player(args[2], 'B');
    } catch(const std::runtime_error &e) {
        std::cerr << "Fehler: " << e.what() << std::endl;
        return 3;
    }

    std::cerr << "\nSpieler bereit, Spiel startet!\n";

    // ...

    std::cerr << "Ende\n";
    return 0;
}

