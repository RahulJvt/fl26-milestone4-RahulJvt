
#include "aiws/gui/main_window.hpp"
#include "aiws/ipc/remote_add_server.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>

#include <unistd.h>

namespace {

std::string default_socket_path() {
    const char* user = std::getenv("USER");

    if (user != nullptr && user[0] != '\0') {
        return std::string("/tmp/aiws-") + user + ".sock";
    }

    return "/tmp/aiws-" +
           std::to_string(
               static_cast<unsigned long>(getuid())) +
           ".sock";
}

void confirm_exit(QWidget* parent) {
    const auto choice =
        QMessageBox::question(
            parent,
            "Exit",
            "Are you sure you want to exit?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

    if (choice == QMessageBox::Yes) {
        QApplication::quit();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    int build_delay_ms = 0;
    std::string socket_path = default_socket_path();

    const QStringList arguments = app.arguments();

    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == "--build-delay-ms") {
            if (i + 1 >= arguments.size()) {
                std::cerr << "missing build delay\n";
                return 1;
            }

            bool valid = false;
            const int delay =
                arguments[++i].toInt(&valid);

            if (!valid || delay < 0) {
                std::cerr << "invalid build delay\n";
                return 1;
            }

            build_delay_ms = delay;
        } else if (arguments[i] == "--ipc-socket") {
            if (i + 1 >= arguments.size()) {
                std::cerr << "missing socket path\n";
                return 1;
            }

            socket_path =
                arguments[++i].toStdString();

            if (socket_path.empty()) {
                std::cerr << "invalid socket path\n";
                return 1;
            }
        } else {
            std::cerr << "unknown argument: "
                      << arguments[i].toStdString()
                      << '\n';
            return 1;
        }
    }

    aiws::gui::MainWindow window;

    window.set_exit_callback(&confirm_exit);
    window.set_build_delay_ms(build_delay_ms);

    // queued requests are processed on the gui thread
    std::atomic<bool> shutting_down{false};

    aiws::RemoteAddServer server(
        socket_path,
        [&window, &shutting_down](
            const std::string& path) -> bool {

            if (shutting_down.load()) {
                return false;
            }

            const QString document_path =
                QString::fromUtf8(
                    path.data(),
                    static_cast<qsizetype>(path.size()));

            auto promise =
                std::make_shared<std::promise<bool>>();

            std::future<bool> result =
                promise->get_future();

            const bool queued =
                QMetaObject::invokeMethod(
                    &window,
                    [&window,
                     &shutting_down,
                     document_path,
                     promise]() {

                        bool added = false;

                        if (!shutting_down.load()) {
                            added =
                                window.add_document_file(
                                    document_path);
                        }

                        promise->set_value(added);
                    },
                    Qt::QueuedConnection);

            if (!queued) {
                return false;
            }

            // wait for the actual gui operation
            while (result.wait_for(
                       std::chrono::milliseconds(20)) !=
                   std::future_status::ready) {

                if (shutting_down.load()) {
                    return false;
                }
            }

            return result.get();
        });

    if (!server.start()) {
        std::cerr << "failed to start ipc server: "
                  << server.error()
                  << '\n';
        return 1;
    }

    window.show();

    const int exit_code = app.exec();

    shutting_down.store(true);
    server.stop();

    return exit_code;
}
