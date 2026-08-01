#include "runyard/runner/process.hpp"
#include "runyard/domain/error.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

namespace runyard {
namespace {
struct Pipe {
  int fd[2]{-1, -1};
  Pipe() {
    if (pipe(fd) != 0)
      throw Error(ErrorCode::unavailable, "cannot create process pipe");
    for (int f : fd)
      fcntl(f, F_SETFD, FD_CLOEXEC);
  }
  ~Pipe() {
    for (int f : fd)
      if (f >= 0)
        close(f);
  }
};
struct SpawnActions {
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  SpawnActions() {
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
  }
  ~SpawnActions() {
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
  }
};
} // namespace
PosixProcess::PosixProcess(const std::vector<std::string> &command,
                           const std::map<std::string, std::string> &environment,
                           const std::string &directory, const Clock &clock)
    : clock_(clock) {
  if (command.empty())
    throw Error(ErrorCode::invalid, "empty process command");
  Pipe output, error;
  SpawnActions spawn;
  posix_spawn_file_actions_addopen(&spawn.actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&spawn.actions, output.fd[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&spawn.actions, error.fd[1], STDERR_FILENO);
  for (auto *pipe : {&output, &error})
    for (int fd : pipe->fd)
      posix_spawn_file_actions_addclose(&spawn.actions, fd);
  posix_spawn_file_actions_addchdir_np(&spawn.actions, directory.c_str());
  sigset_t empty, defaults;
  sigemptyset(&empty);
  sigemptyset(&defaults);
  sigaddset(&defaults, SIGTERM);
  sigaddset(&defaults, SIGINT);
  sigaddset(&defaults, SIGPIPE);
  posix_spawnattr_setsigmask(&spawn.attributes, &empty);
  posix_spawnattr_setsigdefault(&spawn.attributes, &defaults);
  posix_spawnattr_setpgroup(&spawn.attributes, 0);
  posix_spawnattr_setflags(&spawn.attributes,
                           POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
  std::vector<char *> args;
  for (const auto &arg : command)
    args.push_back(const_cast<char *>(arg.c_str()));
  args.push_back(nullptr);
  std::vector<std::string> storage;
  for (const auto &[key, value] : environment)
    storage.push_back(key + "=" + value);
  std::vector<char *> envp;
  for (auto &value : storage)
    envp.push_back(value.data());
  envp.push_back(nullptr);
  // posix_spawn avoids running allocation/locking code in a forked gRPC process.
  int result =
      posix_spawnp(&pid_, args[0], &spawn.actions, &spawn.attributes, args.data(), envp.data());
  if (result != 0) {
    pid_ = -1;
    throw Error(ErrorCode::invalid,
                std::string("cannot execute command: ") + std::strerror(result));
  }
  stdout_ = std::exchange(output.fd[0], -1);
  stderr_ = std::exchange(error.fd[0], -1);
  fcntl(stdout_, F_SETFL, fcntl(stdout_, F_GETFL) | O_NONBLOCK);
  fcntl(stderr_, F_SETFL, fcntl(stderr_, F_GETFL) | O_NONBLOCK);
}
PosixProcess::~PosixProcess() {
  if (pid_ > 0 && !status_) {
    kill(-pid_, SIGKILL);
    int status;
    while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
    }
  }
  if (stdout_ >= 0)
    close(stdout_);
  if (stderr_ >= 0)
    close(stderr_);
}
void PosixProcess::drain(
    const std::function<void(const std::string &, const std::string &)> &consume) {
  std::array<char, 16384> buffer{};
  for (auto [fd, kind] : {std::pair{stdout_, "stdout"}, std::pair{stderr_, "stderr"}}) {
    // A noisy child must not starve lease checks or termination.
    for (int i = 0; i < 16; ++i) {
      auto count = read(fd, buffer.data(), buffer.size());
      if (count > 0)
        consume(kind, std::string(buffer.data(), count));
      else if (count < 0 && errno == EINTR)
        continue;
      else
        break;
    }
  }
}
std::optional<int> PosixProcess::poll() {
  if (status_)
    return status_;
  if (kill_at_ && clock_.now() >= *kill_at_)
    kill(-pid_, SIGKILL);
  int status{};
  auto result = waitpid(pid_, &status, WNOHANG);
  if (result == pid_) {
    status_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    // Descendants cannot outlive the supervised command and retain its pipes.
    kill(-pid_, SIGKILL);
  } else if (result < 0 && errno != EINTR)
    throw Error(ErrorCode::unavailable, "cannot reap experiment process");
  return status_;
}
void PosixProcess::stop(std::chrono::seconds grace) {
  auto deadline = clock_.now() + grace;
  if (!kill_at_ || deadline < *kill_at_) {
    kill_at_ = deadline;
    kill(-pid_, SIGTERM);
  }
}
} // namespace runyard
