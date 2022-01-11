#ifndef COMPOSE_H
#define COMPOSE_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <json.h>

class compose
{
public:
    compose(const std::filesystem::path& pipe_path);
    ~compose();
    void run(const std::chrono::milliseconds& nap_time);
    void add_reloader(const std::shared_ptr<std::function<void()>>& reloader);
    void add_ticker(const std::shared_ptr<std::function<void()>>& ticker,
        const std::chrono::milliseconds& interval);
    void add_handler(const std::string& command, const std::shared_ptr<
        std::function<void(const jsonio::json&)>>& handler);
    bool need_reload() const;
    bool need_stop() const;
    void request_reload();
    void request_stop();
private:
    std::filesystem::path pipe_path_;
    std::vector<std::weak_ptr<std::function<void()>>> reloaders_;
    std::vector<std::pair<std::weak_ptr<std::function<void()>>,
        std::array<std::chrono::milliseconds, 2>>> tickers_;
    std::map<std::string, std::vector<std::weak_ptr<
        std::function<void(const jsonio::json&)>>>> handlers_;

private:
    static std::atomic<bool> reload_;
    static std::atomic<bool> stop_;

private:
    static void handle_signal(int signal);
};

#endif //COMPOSE_H
