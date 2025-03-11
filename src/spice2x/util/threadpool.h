#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
private:
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> queue;
    std::mutex mut;
    std::condition_variable cv;
    bool exit = false;

public:

    ThreadPool(size_t size) {
        for (size_t i = 0; i < size; i++) {
            threads.emplace_back([this] {
                while (true) {
                    std::function<void()> func;
                    std::unique_lock<std::mutex> lock(mut);
                    cv.wait(lock, [this] { return exit || !queue.empty(); });
                    if (exit && queue.empty()) return;
                    func = std::move(queue.front());
                    queue.pop();
                    lock.unlock();
                    func();
                }
            });
        }
    }

    ~ThreadPool() {
        exit = true;
        mut.lock();
        mut.unlock();
        cv.notify_all();
        for (auto &thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    size_t queue_size() {
        std::unique_lock<std::mutex> lock(mut);
        return queue.size();
    }

    template<class T, class... Args>
    auto add(T&& func, Args&&... args)
    -> std::future<typename std::result_of<T(Args...)>::type> {
        using ret_t = typename std::result_of<T(Args...)>::type;
        auto task = std::make_shared<std::packaged_task<ret_t()>>(
                std::bind(std::forward<T>(func), std::forward<Args>(args)...));
        std::future<ret_t> fut = task->get_future();
        std::unique_lock<std::mutex> lock(mut);
        queue.emplace([task] () { (*task)(); });
        lock.unlock();
        cv.notify_one();
        return fut;
    }
};
