#ifndef COMPOSE_H
#define COMPOSE_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <json.hpp>

class compose {
public:
  compose(const std::filesystem::path &pipe_path);
  ~compose();
  void run(const std::chrono::milliseconds &nap_time);
  std::size_t add_ticker(std::weak_ptr<std::function<void()>> &&ticker,
                         const std::chrono::milliseconds &interval);
  void add_reloader(std::weak_ptr<std::function<void()>> &&reloader);
  void add_runner(std::weak_ptr<std::function<void()>> &&runner,
                  const std::chrono::milliseconds &timeout);
  void
  add_handler(const std::string &command,
              std::weak_ptr<std::function<void(jsonio::json &&)>> &&handler);
  void set_ticker(std::size_t index, const std::chrono::milliseconds &interval);
  bool need_reload() const;
  bool need_stop() const;
  void request_reload();
  void request_stop();

private:
  std::filesystem::path pipe_path_;
  std::vector<std::weak_ptr<std::function<void()>>> reloaders_;
  std::vector<std::pair<std::weak_ptr<std::function<void()>>,
                        std::array<std::chrono::milliseconds, 2>>>
      tickers_;
  std::list<std::pair<std::weak_ptr<std::function<void()>>,
                      std::array<std::chrono::milliseconds, 2>>>
      runners_;
  std::map<std::string,
           std::vector<std::weak_ptr<std::function<void(jsonio::json &&)>>>>
      handlers_;

private:
  static std::atomic<bool> reload_;
  static std::atomic<bool> stop_;

private:
  static void handle_signal(int signal);
};

#endif // COMPOSE_H
