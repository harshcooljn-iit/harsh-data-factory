#include "flowforge/process/posix_process_runner.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "flowforge/util/time_utils.hpp"

extern "C" char** environ;

namespace flowforge::process {

namespace {

using util::Clock;
using util::TimePoint;

constexpr std::size_t kReadChunk = 64 * 1024;
constexpr std::size_t kMaxPartialLine = 1 * 1024 * 1024;

::pollfd make_pollfd(int fd) {
    ::pollfd pfd{};
    pfd.fd = fd;
    pfd.events = static_cast<short>(POLLIN);
    pfd.revents = 0;
    return pfd;
}

void set_nonblock_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    const int fdflags = ::fcntl(fd, F_GETFD, 0);
    if (fdflags != -1) {
        ::fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
    }
}

}  // namespace

// ===========================================================================
// Impl
// ===========================================================================
class PosixProcessRunner::Impl {
  public:
    explicit Impl(Options options) : options_(options) {
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            // A process runner that cannot create its own wakeup pipe is
            // unusable; fail loudly rather than limp along.
            std::abort();
        }
        wakeup_read_ = fds[0];
        wakeup_write_ = fds[1];
        set_nonblock_cloexec(wakeup_read_);
        set_nonblock_cloexec(wakeup_write_);
        reactor_ = std::thread([this] { run(); });
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Command cmd;
            cmd.type = Command::Type::kShutdown;
            queue_.push_back(std::move(cmd));
        }
        wake();
        if (reactor_.joinable()) {
            reactor_.join();
        }
        ::close(wakeup_read_);
        ::close(wakeup_write_);
    }

    ProcessHandle launch(const ProcessSpec& spec, ProcessCallbacks callbacks) {
        if (!running_.load(std::memory_order_acquire)) {
            return kInvalidHandle;
        }
        const ProcessHandle handle = next_handle_.fetch_add(1, std::memory_order_relaxed);
        active_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Command cmd;
            cmd.type = Command::Type::kLaunch;
            cmd.handle = handle;
            cmd.spec = spec;
            cmd.callbacks = std::move(callbacks);
            queue_.push_back(std::move(cmd));
        }
        wake();
        return handle;
    }

    void request_terminate(ProcessHandle handle, bool as_cancellation) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Command cmd;
            cmd.type = Command::Type::kTerminate;
            cmd.handle = handle;
            cmd.as_cancellation = as_cancellation;
            queue_.push_back(std::move(cmd));
        }
        wake();
    }

    std::size_t active_count() const { return active_.load(std::memory_order_relaxed); }

  private:
    struct Command {
        enum class Type { kLaunch, kTerminate, kShutdown };
        Type type = Type::kShutdown;
        ProcessHandle handle = kInvalidHandle;
        ProcessSpec spec;
        ProcessCallbacks callbacks;
        bool as_cancellation = false;
    };

    struct Child {
        ProcessHandle handle = kInvalidHandle;
        ::pid_t pid = -1;
        int stdout_fd = -1;
        int stderr_fd = -1;
        std::string stdout_partial;
        std::string stderr_partial;
        TimePoint start;
        std::optional<TimePoint> deadline;

        bool exited = false;
        int wait_status = 0;
        std::optional<TimePoint> pipes_closed_at;

        // Termination escalation: 0 = none, 1 = SIGTERM sent, 2 = SIGKILL sent.
        int term_stage = 0;
        TimePoint term_sent_at;
        ProcessOutcome forced_outcome = ProcessOutcome::kExited;  // when term_stage > 0
        bool forced = false;

        ProcessCallbacks callbacks;
    };

    void wake() {
        const char byte = 1;
        ssize_t n = ::write(wakeup_write_, &byte, 1);
        (void)n;
    }

    void run() {
        while (true) {
            drain_commands();
            if (shutting_down_ && children_.empty()) {
                break;
            }
            reap_children();
            enforce_deadlines();
            finalize_children();
            if (shutting_down_ && children_.empty()) {
                break;
            }
            poll_once();
        }
        running_.store(false, std::memory_order_release);
    }

    void drain_commands() {
        std::deque<Command> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            local.swap(queue_);
        }
        // Drain the wakeup pipe.
        std::array<char, 256> sink{};
        while (::read(wakeup_read_, sink.data(), sink.size()) > 0) {
        }

        for (auto& cmd : local) {
            switch (cmd.type) {
                case Command::Type::kLaunch:
                    handle_launch(cmd);
                    break;
                case Command::Type::kTerminate:
                    handle_terminate(cmd.handle, cmd.as_cancellation);
                    break;
                case Command::Type::kShutdown:
                    shutting_down_ = true;
                    running_.store(false, std::memory_order_release);
                    for (auto& [handle, child] : children_) {
                        (void)handle;
                        if (!child->exited) {
                            ::kill(child->pid, SIGKILL);
                            child->forced = true;
                            child->forced_outcome = ProcessOutcome::kCancelled;
                            child->term_stage = 2;
                        }
                    }
                    break;
            }
        }
    }

    void report_spawn_failure(const Command& cmd, const std::string& message) {
        ProcessResult result;
        result.outcome = ProcessOutcome::kSpawnFailed;
        result.spawn_error = message;
        if (cmd.callbacks.on_exit) {
            cmd.callbacks.on_exit(result);
        }
        active_.fetch_sub(1, std::memory_order_relaxed);
    }

    void handle_launch(Command& cmd) {
        if (const auto err = cmd.spec.validate()) {
            report_spawn_failure(cmd, *err);
            return;
        }

        int out_fds[2] = {-1, -1};
        int err_fds[2] = {-1, -1};
        int exec_fds[2] = {-1, -1};
        if (::pipe(out_fds) != 0 || ::pipe(err_fds) != 0 || ::pipe(exec_fds) != 0) {
            const std::string msg = std::string("pipe(): ") + std::strerror(errno);
            for (int fd : {out_fds[0], out_fds[1], err_fds[0], err_fds[1], exec_fds[0],
                           exec_fds[1]}) {
                if (fd >= 0) {
                    ::close(fd);
                }
            }
            report_spawn_failure(cmd, msg);
            return;
        }
        // exec-error pipe: the parent's read end stays BLOCKING so the read
        // below waits for either an errno (exec failed) or EOF (write end
        // closed by a successful execve). Both ends are close-on-exec.
        for (const int fd : {exec_fds[0], exec_fds[1]}) {
            const int f = ::fcntl(fd, F_GETFD, 0);
            if (f != -1) {
                ::fcntl(fd, F_SETFD, f | FD_CLOEXEC);
            }
        }

        // Build argv / envp before fork so nothing allocates in the child.
        std::vector<std::string> argv_storage = cmd.spec.argv;
        std::vector<char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto& a : argv_storage) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);

        std::vector<std::string> env_storage = build_environment_block(
            cmd.spec.environment, cmd.spec.inherit_environment);
        std::vector<char*> envp;
        envp.reserve(env_storage.size() + 1);
        for (auto& e : env_storage) {
            envp.push_back(e.data());
        }
        envp.push_back(nullptr);

        const std::string cwd = cmd.spec.working_directory;
        const std::string program = cmd.spec.program;

        const ::pid_t pid = ::fork();
        if (pid < 0) {
            const std::string msg = std::string("fork(): ") + std::strerror(errno);
            for (int fd : {out_fds[0], out_fds[1], err_fds[0], err_fds[1], exec_fds[0],
                           exec_fds[1]}) {
                ::close(fd);
            }
            report_spawn_failure(cmd, msg);
            return;
        }

        if (pid == 0) {
            // ---- child: async-signal-safe only ----
            ::environ = envp.data();
            if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
                const int e = errno;
                ssize_t w = ::write(exec_fds[1], &e, sizeof(e));
                (void)w;
                _exit(127);
            }
            ::dup2(out_fds[1], STDOUT_FILENO);
            ::dup2(err_fds[1], STDERR_FILENO);
            const int devnull = ::open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                ::dup2(devnull, STDIN_FILENO);
                ::close(devnull);
            }
            ::close(out_fds[0]);
            ::close(out_fds[1]);
            ::close(err_fds[0]);
            ::close(err_fds[1]);
            ::close(exec_fds[0]);
            ::execvp(program.c_str(), argv.data());
            const int e = errno;
            ssize_t w = ::write(exec_fds[1], &e, sizeof(e));
            (void)w;
            _exit(127);
        }

        // ---- parent ----
        ::close(out_fds[1]);
        ::close(err_fds[1]);
        ::close(exec_fds[1]);

        int exec_errno = 0;
        ssize_t got = 0;
        do {
            got = ::read(exec_fds[0], &exec_errno, sizeof(exec_errno));
        } while (got < 0 && errno == EINTR);
        ::close(exec_fds[0]);

        if (got == static_cast<ssize_t>(sizeof(exec_errno))) {
            // Child failed before exec. Reap it and report.
            int status = 0;
            ::waitpid(pid, &status, 0);
            ::close(out_fds[0]);
            ::close(err_fds[0]);
            std::string what = std::strerror(exec_errno);
            report_spawn_failure(
                cmd, "could not start '" + program + "': " + what);
            return;
        }

        set_nonblock_cloexec(out_fds[0]);
        set_nonblock_cloexec(err_fds[0]);

        auto child = std::make_unique<Child>();
        child->handle = cmd.handle;
        child->pid = pid;
        child->stdout_fd = out_fds[0];
        child->stderr_fd = err_fds[0];
        child->start = Clock::now();
        if (cmd.spec.timeout && cmd.spec.timeout->count() > 0) {
            child->deadline = child->start + *cmd.spec.timeout;
        }
        child->callbacks = std::move(cmd.callbacks);
        children_.emplace(cmd.handle, std::move(child));
    }

    void handle_terminate(ProcessHandle handle, bool as_cancellation) {
        const auto it = children_.find(handle);
        if (it == children_.end()) {
            return;
        }
        Child& child = *it->second;
        if (child.exited || child.term_stage >= 1) {
            return;
        }
        ::kill(child.pid, SIGTERM);
        child.term_stage = 1;
        child.term_sent_at = Clock::now();
        child.forced = true;
        child.forced_outcome =
            as_cancellation ? ProcessOutcome::kCancelled : ProcessOutcome::kTimedOut;
    }

    void emit_lines(std::string& partial, std::string_view chunk,
                    const std::function<void(std::string_view)>& sink) {
        partial.append(chunk);
        std::size_t start = 0;
        while (true) {
            const std::size_t nl = partial.find('\n', start);
            if (nl == std::string::npos) {
                break;
            }
            std::size_t end = nl;
            if (end > start && partial[end - 1] == '\r') {
                --end;
            }
            if (sink) {
                sink(std::string_view(partial).substr(start, end - start));
            }
            start = nl + 1;
        }
        partial.erase(0, start);
        if (partial.size() > kMaxPartialLine) {
            if (sink) {
                sink(std::string_view(partial));
            }
            partial.clear();
        }
    }

    // Returns false when the fd hit EOF (and was closed).
    bool drain_fd(int& fd, std::string& partial,
                  const std::function<void(std::string_view)>& sink) {
        std::array<char, kReadChunk> buf{};
        while (true) {
            const ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n > 0) {
                emit_lines(partial, std::string_view(buf.data(), static_cast<std::size_t>(n)),
                           sink);
                continue;
            }
            if (n == 0) {
                if (!partial.empty()) {
                    if (sink) {
                        sink(std::string_view(partial));
                    }
                    partial.clear();
                }
                ::close(fd);
                fd = -1;
                return false;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            fd = -1;
            return false;
        }
    }

    void poll_once() {
        std::vector<::pollfd> pfds;
        std::vector<ProcessHandle> owners;   // parallel: which child, or 0 for wakeup
        std::vector<bool> is_stdout;

        pfds.push_back(make_pollfd(wakeup_read_));
        owners.push_back(kInvalidHandle);
        is_stdout.push_back(false);

        for (auto& [handle, child] : children_) {
            if (child->stdout_fd >= 0) {
                pfds.push_back(make_pollfd(child->stdout_fd));
                owners.push_back(handle);
                is_stdout.push_back(true);
            }
            if (child->stderr_fd >= 0) {
                pfds.push_back(make_pollfd(child->stderr_fd));
                owners.push_back(handle);
                is_stdout.push_back(false);
            }
        }

        const int timeout_ms = next_timeout_ms();
        const int rc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeout_ms);
        if (rc <= 0) {
            return;  // timeout or EINTR: loop re-evaluates deadlines
        }

        for (std::size_t i = 0; i < pfds.size(); ++i) {
            if (pfds[i].revents == 0) {
                continue;
            }
            if (owners[i] == kInvalidHandle) {
                continue;  // wakeup pipe drained in drain_commands()
            }
            const auto it = children_.find(owners[i]);
            if (it == children_.end()) {
                continue;
            }
            Child& child = *it->second;
            if (is_stdout[i]) {
                drain_fd(child.stdout_fd, child.stdout_partial, child.callbacks.on_stdout);
            } else {
                drain_fd(child.stderr_fd, child.stderr_partial, child.callbacks.on_stderr);
            }
        }
    }

    int next_timeout_ms() {
        const TimePoint now = Clock::now();
        std::optional<TimePoint> earliest;
        auto consider = [&](TimePoint tp) {
            if (!earliest || tp < *earliest) {
                earliest = tp;
            }
        };
        for (auto& [handle, child] : children_) {
            (void)handle;
            if (!child->exited && child->term_stage == 0 && child->deadline) {
                consider(*child->deadline);
            }
            if (child->term_stage == 1) {
                consider(child->term_sent_at + options_.terminate_grace);
            }
            if (child->pipes_closed_at && !child->exited) {
                consider(*child->pipes_closed_at + std::chrono::milliseconds{20});
            }
        }
        if (!earliest) {
            return -1;
        }
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(*earliest - now)
                               .count();
        if (delta <= 0) {
            return 0;
        }
        return delta > 1000 ? 1000 : static_cast<int>(delta);
    }

    void enforce_deadlines() {
        const TimePoint now = Clock::now();
        for (auto& [handle, child] : children_) {
            (void)handle;
            if (child->exited) {
                continue;
            }
            if (child->term_stage == 0 && child->deadline && now >= *child->deadline) {
                ::kill(child->pid, SIGTERM);
                child->term_stage = 1;
                child->term_sent_at = now;
                child->forced = true;
                child->forced_outcome = ProcessOutcome::kTimedOut;
            } else if (child->term_stage == 1 &&
                       now - child->term_sent_at >= options_.terminate_grace) {
                ::kill(child->pid, SIGKILL);
                child->term_stage = 2;
            }
        }
    }

    void reap_children() {
        const TimePoint now = Clock::now();
        for (auto& [handle, child] : children_) {
            (void)handle;
            if (child->exited) {
                continue;
            }
            // Drain any output that is already pending so nothing is lost.
            if (child->stdout_fd >= 0) {
                drain_fd(child->stdout_fd, child->stdout_partial, child->callbacks.on_stdout);
            }
            if (child->stderr_fd >= 0) {
                drain_fd(child->stderr_fd, child->stderr_partial, child->callbacks.on_stderr);
            }
            if (child->stdout_fd < 0 && child->stderr_fd < 0 && !child->pipes_closed_at) {
                child->pipes_closed_at = now;
            }
            int status = 0;
            const ::pid_t r = ::waitpid(child->pid, &status, WNOHANG);
            if (r == child->pid) {
                child->exited = true;
                child->wait_status = status;
            }
        }
    }

    void finalize_children() {
        std::vector<ProcessHandle> done;
        for (auto& [handle, child] : children_) {
            if (child->exited && child->stdout_fd < 0 && child->stderr_fd < 0) {
                done.push_back(handle);
            }
        }
        for (const ProcessHandle handle : done) {
            const auto it = children_.find(handle);
            Child& child = *it->second;

            ProcessResult result;
            result.pid = child.pid;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - child.start);

            const int status = child.wait_status;
            if (child.forced) {
                result.outcome = child.forced_outcome;
                result.term_signal = WIFSIGNALED(status) ? WTERMSIG(status) : SIGKILL;
            } else if (WIFEXITED(status)) {
                result.outcome = ProcessOutcome::kExited;
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.outcome = ProcessOutcome::kSignalled;
                result.term_signal = WTERMSIG(status);
            } else {
                result.outcome = ProcessOutcome::kSignalled;
                result.term_signal = 0;
            }

            if (child.callbacks.on_exit) {
                child.callbacks.on_exit(result);
            }
            children_.erase(it);
            active_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    Options options_;
    std::thread reactor_;

    int wakeup_read_ = -1;
    int wakeup_write_ = -1;

    std::mutex mutex_;
    std::deque<Command> queue_;

    std::unordered_map<ProcessHandle, std::unique_ptr<Child>> children_;
    bool shutting_down_ = false;

    std::atomic<ProcessHandle> next_handle_{1};
    std::atomic<std::size_t> active_{0};
    std::atomic<bool> running_{true};
};

// ===========================================================================
// PosixProcessRunner
// ===========================================================================
PosixProcessRunner::PosixProcessRunner() : PosixProcessRunner(Options{}) {}

PosixProcessRunner::PosixProcessRunner(Options options)
    : impl_(std::make_unique<Impl>(options)) {}

PosixProcessRunner::~PosixProcessRunner() = default;

ProcessHandle PosixProcessRunner::launch(const ProcessSpec& spec,
                                         ProcessCallbacks callbacks) {
    return impl_->launch(spec, std::move(callbacks));
}

void PosixProcessRunner::request_terminate(ProcessHandle handle, bool as_cancellation) {
    impl_->request_terminate(handle, as_cancellation);
}

std::size_t PosixProcessRunner::active_count() const { return impl_->active_count(); }

// ===========================================================================
// run_blocking
// ===========================================================================
ProcessResult run_blocking(ProcessRunner& runner, const ProcessSpec& spec, std::string* out,
                           std::string* err) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    ProcessResult result;

    ProcessCallbacks cb;
    if (out != nullptr) {
        cb.on_stdout = [out](std::string_view line) {
            out->append(line);
            out->push_back('\n');
        };
    }
    if (err != nullptr) {
        cb.on_stderr = [err](std::string_view line) {
            err->append(line);
            err->push_back('\n');
        };
    }
    cb.on_exit = [&](const ProcessResult& r) {
        std::lock_guard<std::mutex> lock(m);
        result = r;
        done = true;
        cv.notify_all();
    };

    const ProcessHandle handle = runner.launch(spec, std::move(cb));
    if (handle == kInvalidHandle) {
        result.outcome = ProcessOutcome::kSpawnFailed;
        result.spawn_error = "process runner rejected the launch (shutting down)";
        return result;
    }
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return done; });
    return result;
}

}  // namespace flowforge::process
