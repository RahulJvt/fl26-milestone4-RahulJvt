
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string default_socket_path() {
    const char* user = std::getenv("USER");

    if (user != nullptr && user[0] != '\0') {
        return std::string("/tmp/aiws-") + user + ".sock";
    }

    return "/tmp/aiws-" +
           std::to_string(static_cast<unsigned long>(getuid())) +
           ".sock";
}

bool send_all(int fd, const void* data, std::size_t size) {
    const auto* bytes =
        static_cast<const unsigned char*>(data);

    std::size_t sent = 0;

    while (sent < size) {
        const ssize_t count =
            send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);

        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

bool receive_all(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<unsigned char*>(data);
    std::size_t received = 0;

    while (received < size) {
        const ssize_t count =
            recv(fd, bytes + received, size - received, 0);

        if (count > 0) {
            received += static_cast<std::size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

void encode_length(std::uint32_t value, unsigned char* bytes) {
    bytes[0] = static_cast<unsigned char>(value >> 24);
    bytes[1] = static_cast<unsigned char>(value >> 16);
    bytes[2] = static_cast<unsigned char>(value >> 8);
    bytes[3] = static_cast<unsigned char>(value);
}

std::uint32_t decode_length(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

bool set_socket_timeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    return setsockopt(
               fd,
               SOL_SOCKET,
               SO_RCVTIMEO,
               &timeout,
               sizeof(timeout)) == 0 &&
           setsockopt(
               fd,
               SOL_SOCKET,
               SO_SNDTIMEO,
               &timeout,
               sizeof(timeout)) == 0;
}

int connect_to_server(const std::string& socket_path) {
    sockaddr_un address{};

    if (socket_path.size() >= sizeof(address.sun_path)) {
        std::cerr << "socket path is too long\n";
        return -1;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        std::cerr << "cannot create socket\n";
        return -1;
    }

    address.sun_family = AF_UNIX;
    std::memcpy(
        address.sun_path,
        socket_path.c_str(),
        socket_path.size() + 1);

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(2);

    while (true) {
        if (connect(
                fd,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) == 0) {
            return fd;
        }

        const int error = errno;

        if (error != ENOENT &&
            error != ECONNREFUSED &&
            error != EINTR) {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(25));
    }

    std::cerr << "cannot reach AI Workspace\n";
    close(fd);
    return -1;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = default_socket_path();
    std::string file_path;

    int index = 1;

    if (index < argc &&
        std::string(argv[index]) == "--socket") {

        if (index + 1 >= argc) {
            std::cerr << "missing socket path\n";
            return 1;
        }

        socket_path = argv[index + 1];
        index += 2;
    }

    if (argc - index != 2 ||
        std::string(argv[index]) != "add") {

        std::cerr
            << "usage: aiws_send [--socket PATH] add FILE\n";
        return 1;
    }

    file_path = argv[index + 1];

    if (file_path.empty() ||
        file_path.size() >
            std::numeric_limits<std::uint32_t>::max()) {

        std::cerr << "invalid file path\n";
        return 1;
    }

    char* resolved = realpath(file_path.c_str(), nullptr);

    if (resolved == nullptr) {
        std::cerr << "cannot resolve file path: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    const std::string absolute_path(resolved);
    std::free(resolved);

    if (absolute_path.size() >
        std::numeric_limits<std::uint32_t>::max()) {

        std::cerr << "file path is too long\n";
        return 1;
    }

    const int fd = connect_to_server(socket_path);

    if (fd < 0) {
        return 2;
    }

    if (!set_socket_timeout(fd, 5)) {
        std::cerr << "cannot configure socket timeout\n";
        close(fd);
        return 2;
    }

    // request: four-byte length followed by utf-8 path
    unsigned char header[4];

    encode_length(
        static_cast<std::uint32_t>(absolute_path.size()),
        header);

    if (!send_all(fd, header, sizeof(header)) ||
        !send_all(
            fd,
            absolute_path.data(),
            absolute_path.size())) {

        std::cerr << "failed to send request\n";
        close(fd);
        return 2;
    }

    // response: one status byte, then message length and text
    unsigned char status = 0;
    unsigned char response_header[4];

    if (!receive_all(fd, &status, 1) ||
        !receive_all(
            fd,
            response_header,
            sizeof(response_header))) {

        std::cerr << "failed to receive response\n";
        close(fd);
        return 2;
    }

    const std::uint32_t message_length =
        decode_length(response_header);

    if (message_length > 65536) {
        std::cerr << "invalid server response\n";
        close(fd);
        return 2;
    }

    std::string message(message_length, '\0');

    if (!receive_all(
            fd,
            message.data(),
            message.size())) {

        std::cerr << "incomplete server response\n";
        close(fd);
        return 2;
    }

    close(fd);

    if (status == 0) {
        std::cout << "document accepted\n";
        return 0;
    }

    if (status == 1) {
        std::cerr << (message.empty()
                          ? "document rejected"
                          : message)
                  << '\n';
        return 1;
    }

    std::cerr << "invalid server status\n";
    return 2;
}
