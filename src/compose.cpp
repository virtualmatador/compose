#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <ext/stdio_filebuf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compose.h"

std::atomic<bool> compose::reload_;
std::atomic<bool> compose::stop_;

void compose::handle_signal(int signal) {
  switch (signal) {
  case SIGHUP:
    reload_ = true;
    break;
  case SIGINT:
  case SIGTERM:
    stop_ = true;
    break;
  }
}

compose::compose(const std::filesystem::path &pipe_path)
    : pipe_path_{pipe_path} {
  reload_ = false;
  stop_ = false;
  signal(SIGHUP, handle_signal);
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
}

compose::~compose() {}

std::size_t compose::add_ticker(std::weak_ptr<std::function<void()>> &&ticker,
                                const std::chrono::milliseconds &interval) {
  tickers_.emplace_back(std::move(ticker),
                        std::array{std::chrono::milliseconds(0), interval});
  return tickers_.size() - 1;
}

void compose::add_reloader(std::weak_ptr<std::function<void()>> &&reloader) {
  reloaders_.emplace_back(std::move(reloader));
}

void compose::add_runner(std::weak_ptr<std::function<void()>> &&runner,
                         const std::chrono::milliseconds &timeout) {
  runners_.emplace_back(std::move(runner),
                        std::array{std::chrono::milliseconds(0), timeout});
}

void compose::add_handler(
    const std::string &command,
    std::weak_ptr<std::function<void(jsonio::json &&)>> &&handler) {
  handlers_[command].emplace_back(std::move(handler));
}

void compose::set_ticker(std::size_t index,
                         const std::chrono::milliseconds &interval) {
  tickers_[index].second[1] = interval;
}

bool compose::need_reload() const { return reload_; }

bool compose::need_stop() const { return stop_; }

void compose::request_reload() { reload_ = true; }

void compose::request_stop() { stop_ = true; }

void compose::run(const std::chrono::milliseconds &nap_time) {
  std::unique_ptr<int, void (*)(int *)> descriptor{nullptr, [](int *fd) {
                                                     if (fd) {
                                                       close(*fd);
                                                       delete fd;
                                                     }
                                                   }};
  std::unique_ptr<__gnu_cxx::stdio_filebuf<char>> file_buf;
  std::unique_ptr<std::istream> pipe;
  if (!pipe_path_.empty()) {
    std::filesystem::remove(pipe_path_);
    if (mkfifo(pipe_path_.c_str(), S_IROTH | S_IWOTH | S_IRUSR | S_IWUSR) !=
        0) {
      throw std::runtime_error("create pipe");
    }
    descriptor.reset(new int{open(pipe_path_.c_str(), O_RDONLY | O_NONBLOCK)});
    if (*descriptor == -1) {
      throw std::runtime_error("open pipe");
    }
    file_buf = std::make_unique<decltype(file_buf)::element_type>(
        *descriptor, std::ios_base::in);
    pipe = std::make_unique<decltype(pipe)::element_type>(file_buf.get());
  }
  jsonio::json_obj pipe_json;
  while (!stop_) {
    if (reload_) {
      reload_ = false;
      for (auto it = reloaders_.begin(); it != reloaders_.end();) {
        if (auto active_callback = it->lock()) {
          (*active_callback)();
          ++it;
        } else {
          it = reloaders_.erase(it);
        }
      }
    }
    for (auto it = tickers_.begin(); it != tickers_.end();) {
      if (auto active_ticker = it->first.lock()) {
        it->second[0] += nap_time;
        if (it->second[0] >= it->second[1]) {
          it->second[0] = std::chrono::milliseconds(0);
          (*active_ticker)();
        }
        ++it;
      } else {
        it = tickers_.erase(it);
      }
    }
    for (auto it = runners_.begin(); it != runners_.end();) {
      if (auto active_runner = it->first.lock()) {
        it->second[0] += nap_time;
        if (it->second[0] >= it->second[1]) {
          it = runners_.erase(it);
          (*active_runner)();
        } else {
          ++it;
        }
      } else {
        it = runners_.erase(it);
      }
    }
    if (pipe.get()) {
      pipe->clear();
      pipe_json.read(*pipe);
      if (pipe_json.completed()) {
        auto command = pipe_json.at("command");
        auto payload = pipe_json.at("payload");
        if (command && command->type() == jsonio::JsonType::J_STRING &&
            payload && payload->type() == jsonio::JsonType::J_OBJECT) {
          auto callbacks = handlers_[command->get_string()];
          for (auto it = callbacks.begin(); it != callbacks.end();) {
            if (auto active_callback = it->lock()) {
              (*active_callback)(std::move(*payload));
              ++it;
            } else {
              it = callbacks.erase(it);
            }
          }
        }
        pipe_json = jsonio::json_obj{};
      }
    }
    std::this_thread::sleep_for(nap_time);
  }
  if (!pipe_path_.empty()) {
    std::filesystem::remove(pipe_path_);
  }
}
