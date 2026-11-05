
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace aiws {

class RemoteAddServer {
public:
    using AddCallback =
        std::function<bool(const std::string&)>;

    RemoteAddServer(
        std::string socket_path,
        AddCallback add_callback);

    ~RemoteAddServer();

    RemoteAddServer(const RemoteAddServer&) = delete;
    RemoteAddServer& operator=(const RemoteAddServer&) = delete;

    bool start();
    void stop();

    const std::string& error() const noexcept;

private:
    void run();
    void handle_client(int client_fd);

    std::string socket_path_;
    AddCallback add_callback_;
    std::string error_;

    std::atomic<bool> stopping_{false};

    int listen_fd_{-1};
    std::thread worker_;

    std::mutex clients_mutex_;
    int active_client_fd_{-1};
};

}  // namespace aiws
