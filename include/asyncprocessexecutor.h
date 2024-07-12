#include <vector>
#include <queue>
#include <variant>
#include <optional>
#include <future>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace async {

    class Succeeded {};
    class Executed {};
    class Error {
        public:
        void setTimedout(bool status) { _timedout = status; }
        bool timedout() const { return _timedout; }

        private:
        bool _timedout = false;
    };

    class Context {
        friend class Process;

        public:

        std::future<void> & getFuture() { return _future; }
        void success() {
            if(!_stopped) {
                _successFunc(Succeeded{});
                _promise.set_value();
            }
        }

        void error(Error && err) {
            if(!_stopped) {
                _errorFunc(err);
                _promise.set_value();
            }
        }
        void error(const Error & err) {
            if(!_stopped) {
                _errorFunc(err);
                _promise.set_value();
            }
        }

        private:
        void setOnSucceeded(const std::function<void(Succeeded)> & cb) { _successFunc = cb; }
        void setOnError(const std::function<void(Error)> & cb) { _errorFunc = cb; }

        private:

        std::atomic_bool _stopped;
        std::promise<void> _promise;
        std::future<void> _future;

        std::function<void(Succeeded)> _successFunc;
        std::function<void(Error)> _errorFunc;
    };
    using ContextPtr = std::shared_ptr<Context>;

    using ProcessFunc = std::function<void(ContextPtr)>;

    class Process {
        friend class QueuedExecutor;

        public:
        Process() { _context = std::make_shared<Context>(); }
        virtual ~Process() = default;

        enum Trigger {
            OnSuccess,
            OnExecuted,
            Async
        };

        virtual void execute(ContextPtr context) = 0;

        void setTimeout(std::chrono::milliseconds ms) { _timeout = ms; }
        std::chrono::milliseconds getTimeout() { return _timeout; }

        void setTrigger(Trigger trigger) { _trigger = trigger; }
        Trigger getTrigger() { return _trigger; }

        void setOnSucceeded(const std::function<void(Succeeded)> & cb) { _context->setOnSucceeded(cb); }
        void setOnExecuted(const std::function<void(Executed)> & cb) { _executedFunc = cb; }
        void setOnError(const std::function<void(Error)> & cb) { _context->setOnError(cb); }

        private:
        ContextPtr _context;

        std::function<void(Executed)> _executedFunc;

        Trigger _trigger = Trigger::OnExecuted;
        std::chrono::milliseconds _timeout = std::chrono::milliseconds{ 0 };
    };

    class QueuedExecutor {
        using ProcessOwner = std::unique_ptr<Process>;
        static constexpr uint32_t maxThread = 8;

        public:

        QueuedExecutor() {
            const uint32_t num_threads = std::thread::hardware_concurrency(); // Max # of threads the system supports
            for(uint32_t ii = 0; ii < std::min(num_threads, maxThread); ++ii) {
                std::shared_ptr<std::atomic_bool> _isActive = std::make_shared<std::atomic_bool>(false);
                _threadPool.emplace_back(std::thread(&QueuedExecutor::ThreadLoop, this, _isActive));
                _threadStates.insert({ _threadPool.back().get_id(), std::move(_isActive) });
            }
        }

        bool push(std::vector<std::unique_ptr<Process>> && processes) {
            std::unique_lock<std::timed_mutex> lock{ _mutex, std::defer_lock };
            if(lock.try_lock_for(std::chrono::milliseconds(100))) {
                for(auto && proc: processes) {
                    _queue.push(std::move(proc));
                }
                if(!hasActiveThread()) _next_proc.notify_one();
                return true;
            }
            return false;
        }

        bool push(std::unique_ptr<Process> && process) {
            std::unique_lock<std::timed_mutex> lock{ _mutex, std::defer_lock };
            if(lock.try_lock_for(std::chrono::milliseconds(100))) {
                _queue.push(std::move(process));
                if(!hasActiveThread()) _next_proc.notify_one();
                return true;
            }
            return false;
        }

        void cancel() { _shouldCancel = true; }

        private:

        bool hasActiveThread() {
            for(const std::thread & t: _threadPool) {
                if(_threadStates[t.get_id()]) {
                    return true;
                }
            }
            return false;
        }

        ProcessOwner && pop() {
            std::unique_lock<std::timed_mutex> lock{ _mutex };
            std::unique_ptr<Process> proc = std::move(_queue.front());
            _queue.pop();
            return std::move(proc);
        }

        void ThreadLoop(std::shared_ptr<std::atomic_bool> running) {
            *running = false;
            while(!_shouldCancel) {
                ProcessOwner proc;
                {
                    std::unique_lock<std::timed_mutex> lock(_mutex);
                    _next_proc.wait(lock, [this] { return !_queue.empty() || _shouldCancel; });
                    if(_shouldCancel) {
                        return;
                    }
                    proc = std::move(_queue.front());
                    _queue.pop();

                    if(!_queue.empty() && _queue.front()->getTrigger() == Process::Trigger::Async) {
                        _next_proc.notify_one();
                    }
                }
                *running = true;

                std::future<void> & future = proc->_context->getFuture();
                proc->execute(proc->_context);
                proc->_executedFunc(Executed{});

                if(!_queue.empty() && _queue.front()->getTrigger() == Process::Trigger::OnExecuted) {
                    _next_proc.notify_one();
                }

                if(proc->getTimeout() > std::chrono::microseconds{0}) {
                    std::future_status status = future.wait_for(proc->getTimeout());
                    if(status != std::future_status::ready) {
                        Error err;
                        err.setTimedout(true);
                        proc->_context->error(err);
                    }
                } else {
                    future.wait();
                }

                _next_proc.notify_one();
                *running = false;
            }
        }

        private:
        std::atomic_bool _shouldCancel{false};
        std::condition_variable_any _next_proc;
        std::vector<std::thread> _threadPool;
        std::unordered_map<std::thread::id, std::shared_ptr<std::atomic_bool>> _threadStates;

        std::timed_mutex _mutex;
        std::queue<ProcessOwner> _queue;
    };

}
