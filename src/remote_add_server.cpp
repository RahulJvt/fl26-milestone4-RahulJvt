
#include "aiws/ipc/remote_add_server.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace aiws {

namespace {

bool receive_all(
    int fd,
    void* buffer,
    std::size_t length,
    const std::atomic<bool>& stopping) {

    auto* bytes = static_cast<unsigned char*>(buffer);
    std::size_t received = 0;

    while (received < length && !stopping.load()) {
        const ssize_t count =
            recv(fd, bytes + received, length - received, 0);

        if (count > 0) {
            received += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }

    return received == length;
}

bool send_all(
    int fd,
    const void* buffer,
    std::size_t length) {

    const auto* bytes =
        static_cast<const unsigned char*>(buffer);

    std::size_t sent = 0;

    while (sent < length) {
        const ssize_t count =
            send(fd, bytes + sent, length - sent, MSG_NOSIGNAL);

        if (count > 0) {
            sent += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }

    return true;
}

std::uint32_t decode_length(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

void encode_length(
    std::uint32_t value,
    unsigned char* bytes) {

    bytes[0] = static_cast<unsigned char>(value >> 24);
    bytes[1] = static_cast<unsigned char>(value >> 16);
    bytes[2] = static_cast<unsigned char>(value >> 8);
    bytes[3] = static_cast<unsigned char>(value);
}

void send_response(
    int fd,
    unsigned char status,
    const std::string& message) {

    unsigned char header[5];
    header[0] = status;

    encode_length(
        static_cast<std::uint32_t>(message.size()),
        header + 1);

    send_all(fd, header, sizeof(header));
    send_all(fd, message.data(), message.size());
}

}  // namespace

RemoteAddServer::RemoteAddServer(
    std::string socket_path,
    AddCallback add_callback)
    : socket_path_(std::move(socket_path)),
      add_callback_(std::move(add_callback)) {}

RemoteAddServer::~RemoteAddServer() {
    stop();
}

bool RemoteAddServer::start() {
    sockaddr_un address{};

    if (socket_path_.empty() ||
        socket_path_.size() >= sizeof(address.sun_path)) {

        error_ = "invalid socket path";
        return false;
    }

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);

    if (listen_fd_ < 0) {
        error_ = std::strerror(errno);
        return false;
    }

    address.sun_family = AF_UNIX;

    std::memcpy(
        address.sun_path,
        socket_path_.c_str(),
        socket_path_.size() + 1);

    unlink(socket_path_.c_str());

    if (bind(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0) {

        error_ = std::strerror(errno);
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 8) != 0) {
        error_ = std::strerror(errno);
        close(listen_fd_);
        listen_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }

    stopping_.store(false);
    worker_ = std::thread(&RemoteAddServer::run, this);

    return true;
}

void RemoteAddServer::stop() {
    stopping_.store(true);

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        if (active_client_fd_ >= 0) {
            shutdown(active_client_fd_, SHUT_RDWR);
        }
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    unlink(socket_path_.c_str());
}

const std::string& RemoteAddServer::error() const noexcept {
    return error_;
}

void RemoteAddServer::run() {
    while (!stopping_.load()) {
        pollfd descriptor{};
        descriptor.fd = listen_fd_;
        descriptor.events = POLLIN;

        const int ready = poll(&descriptor, 1, 50);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            continue;
        }

        if (!(descriptor.revents & POLLIN)) {
            continue;
        }

        const int client_fd =
            accept(listen_fd_, nullptr, nullptr);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            active_client_fd_ = client_fd;
        }

        timeval timeout{};
        timeout.tv_sec = 5;

        setsockopt(
            client_fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout));

        setsockopt(
            client_fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout));

        handle_client(client_fd);

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);

            if (active_client_fd_ == client_fd) {
                active_client_fd_ = -1;
            }
        }

        close(client_fd);
    }
}

void RemoteAddServer::handle_client(int client_fd) {
    unsigned char header[4];

    if (!receive_all(
            client_fd,
            header,
            sizeof(header),
            stopping_)) {
        return;
    }

    const std::uint32_t path_length =
        decode_length(header);

    if (path_length == 0 || path_length > 65536) {
        send_response(
            client_fd,
            1,
            "invalid path length");
        return;
    }

    std::string path(path_length, '\0');

    if (!receive_all(
            client_fd,
            path.data(),
            path.size(),
            stopping_)) {
        return;
    }

    if (path.find('\0') != std::string::npos) {
        send_response(
            client_fd,
            1,
            "invalid path");
        return;
    }

    if (stopping_.load()) {
        return;
    }

    bool accepted = false;

    try {
        accepted = add_callback_(path);
    } catch (...) {
        accepted = false;
    }

    if (stopping_.load()) {
        return;
    }

    if (accepted) {
        send_response(
            client_fd,
            0,
            "accepted");
    } else {
        send_response(
            client_fd,
            1,
            "document could not be added");
    }
}

}  // namespace aiws
