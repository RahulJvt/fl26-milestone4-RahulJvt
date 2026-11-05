
#include "aiws/gui/main_window.hpp"
#include "aiws/ipc/remote_add_server.hpp"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMetaObject>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using aiws::gui::MainWindow;

class StudentTests : public QObject {
    Q_OBJECT

private:
    static QPushButton* button(
        MainWindow& window,
        const QString& label) {

        const auto buttons =
            window.findChildren<QPushButton*>();

        for (QPushButton* candidate : buttons) {
            if (candidate->text() == label) {
                return candidate;
            }
        }

        return nullptr;
    }

    static QString corpus_status(MainWindow& window) {
        const auto labels =
            window.findChildren<QLabel*>();

        for (QLabel* label : labels) {
            if (label->text().startsWith("Corpus:")) {
                return label->text();
            }
        }

        return {};
    }

    static QString application_status(MainWindow& window) {
        const auto labels =
            window.findChildren<QLabel*>();

        for (QLabel* label : labels) {
            if (label->text().startsWith("Current status:")) {
                return label->text();
            }
        }

        return {};
    }

    static QString create_file(
        const QString& directory,
        const QString& name) {

        const QString path =
            QDir(directory).filePath(name);

        QFile file(path);

        if (!file.open(
                QIODevice::WriteOnly | QIODevice::Text)) {
            return {};
        }

        file.write(
            "software design and testing\n"
            "background processing and threads\n"
            "workspace documents and search\n");

        file.close();
        return path;
    }

    static QString socket_path(
        const QString& directory) {

        return QDir(directory).filePath("ipc.sock");
    }

    static QString sender_path() {
        return QCoreApplication::applicationDirPath() +
               "/aiws_send";
    }

    static bool contains_document(
        MainWindow& window,
        const QString& name) {

        const auto labels =
            window.findChildren<QLabel*>();

        for (QLabel* label : labels) {
            if (label->text().contains(name)) {
                return true;
            }
        }

        const auto views =
            window.findChildren<QAbstractItemView*>();

        for (QAbstractItemView* view : views) {
            if (view->model() == nullptr) {
                continue;
            }

            const QAbstractItemModel* model =
                view->model();

            for (int row = 0;
                 row < model->rowCount();
                 ++row) {

                const QString text =
                    model->index(row, 0)
                        .data(Qt::DisplayRole)
                        .toString();

                if (text.contains(name)) {
                    return true;
                }
            }
        }

        return false;
    }

    static bool send_bytes(
        int fd,
        const void* data,
        std::size_t length) {

        const auto* bytes =
            static_cast<const unsigned char*>(data);

        std::size_t sent = 0;

        while (sent < length) {
            const ssize_t count =
                ::send(
                    fd,
                    bytes + sent,
                    length - sent,
                    MSG_NOSIGNAL);

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

    static bool receive_bytes(
        int fd,
        void* data,
        std::size_t length) {

        auto* bytes =
            static_cast<unsigned char*>(data);

        std::size_t received = 0;

        while (received < length) {
            const ssize_t count =
                ::recv(
                    fd,
                    bytes + received,
                    length - received,
                    0);

            if (count > 0) {
                received += static_cast<std::size_t>(count);
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }

        return true;
    }

    static int connect_socket(const QString& path) {
        const QByteArray encoded =
            QFile::encodeName(path);

        sockaddr_un address{};

        if (static_cast<std::size_t>(encoded.size()) >=
            sizeof(address.sun_path)) {
            return -1;
        }

        const int fd =
            ::socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd < 0) {
            return -1;
        }

        address.sun_family = AF_UNIX;

        std::memcpy(
            address.sun_path,
            encoded.constData(),
            static_cast<std::size_t>(encoded.size()) + 1);

        if (::connect(
                fd,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) != 0) {

            ::close(fd);
            return -1;
        }

        return fd;
    }

    static bool send_split_request(
        const QString& socket,
        const QString& file_path) {

        const int fd = connect_socket(socket);

        if (fd < 0) {
            return false;
        }

        const QByteArray path =
            file_path.toUtf8();

        const std::uint32_t length =
            static_cast<std::uint32_t>(path.size());

        const unsigned char header[4] = {
            static_cast<unsigned char>(length >> 24),
            static_cast<unsigned char>(length >> 16),
            static_cast<unsigned char>(length >> 8),
            static_cast<unsigned char>(length)
        };

        bool success =
            send_bytes(fd, header, 2);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(40));

        success = success &&
                  send_bytes(fd, header + 2, 2);

        const std::size_t midpoint =
            static_cast<std::size_t>(path.size()) / 2;

        success = success &&
                  send_bytes(
                      fd,
                      path.constData(),
                      midpoint);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(40));

        success = success &&
                  send_bytes(
                      fd,
                      path.constData() + midpoint,
                      static_cast<std::size_t>(
                          path.size()) - midpoint);

        unsigned char response[5] = {};

        success = success &&
                  receive_bytes(
                      fd,
                      response,
                      sizeof(response));

        ::close(fd);

        return success && response[0] == 0;
    }

    static bool wait_for_process(
        QProcess& process,
        int timeout_ms) {

        QElapsedTimer timer;
        timer.start();

        while (process.state() != QProcess::NotRunning &&
               timer.elapsed() < timeout_ms) {

            QCoreApplication::processEvents();
            QTest::qWait(10);
        }

        return process.state() == QProcess::NotRunning;
    }

    static bool wait_for_future(
        std::future<bool>& result,
        int timeout_ms) {

        QElapsedTimer timer;
        timer.start();

        while (result.wait_for(
                   std::chrono::milliseconds(0)) !=
                   std::future_status::ready &&
               timer.elapsed() < timeout_ms) {

            QCoreApplication::processEvents();
            QTest::qWait(10);
        }

        return result.wait_for(
                   std::chrono::milliseconds(0)) ==
               std::future_status::ready;
    }

    static aiws::RemoteAddServer::AddCallback
    gui_add_callback(MainWindow& window) {

        return [&window](const std::string& path) {
            const QString file_path =
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
                    [&window, file_path, promise]() {
                        promise->set_value(
                            window.add_document_file(
                                file_path));
                    },
                    Qt::QueuedConnection);

            if (!queued) {
                return false;
            }

            if (result.wait_for(
                    std::chrono::seconds(3)) !=
                std::future_status::ready) {
                return false;
            }

            return result.get();
        };
    }

private slots:
    void unreadable_file() {
        MainWindow window;

        const QString before =
            corpus_status(window);

        QVERIFY(!window.add_document_file(
            "/nonexistent/m4_missing_document.txt"));

        QCOMPARE(corpus_status(window), before);
    }

    void successful_build() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path =
            create_file(directory.path(), "first.txt");

        QVERIFY(!path.isEmpty());

        MainWindow window;
        QVERIFY(window.add_document_file(path));

        auto* build =
            button(window, "Build Corpus");

        QVERIFY(build != nullptr);

        build->click();

        QTRY_VERIFY_WITH_TIMEOUT(
            corpus_status(window).contains("chunks"),
            5000);

        QVERIFY(application_status(window).contains(
            "Corpus built"));
    }

    void build_does_not_block_gui() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path =
            create_file(directory.path(), "slow.txt");

        QVERIFY(!path.isEmpty());

        MainWindow window;
        QVERIFY(window.add_document_file(path));

        window.set_build_delay_ms(600);

        auto* build =
            button(window, "Build Corpus");

        QVERIFY(build != nullptr);

        QElapsedTimer timer;
        timer.start();

        build->click();

        QVERIFY2(
            timer.elapsed() < 300,
            "build blocked the gui thread");

        QVERIFY(corpus_status(window).contains(
            "Building"));

        QTRY_VERIFY_WITH_TIMEOUT(
            corpus_status(window).contains("chunks"),
            5000);
    }

    void reject_stale_build_after_add() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString first =
            create_file(directory.path(), "first.txt");

        const QString second =
            create_file(directory.path(), "second.txt");

        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        MainWindow window;
        QVERIFY(window.add_document_file(first));

        window.set_build_delay_ms(400);

        auto* build =
            button(window, "Build Corpus");

        QVERIFY(build != nullptr);

        build->click();

        QVERIFY(window.add_document_file(second));

        QTRY_VERIFY_WITH_TIMEOUT(
            build->isEnabled(),
            5000);

        QVERIFY2(
            !corpus_status(window).contains("chunks"),
            "stale corpus was accepted after adding a document");

        build->click();

        QTRY_VERIFY_WITH_TIMEOUT(
            corpus_status(window).contains("chunks"),
            5000);
    }

    void reject_stale_build_after_reset() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path =
            create_file(directory.path(), "reset.txt");

        QVERIFY(!path.isEmpty());

        MainWindow window;
        QVERIFY(window.add_document_file(path));

        window.set_build_delay_ms(400);

        auto* build =
            button(window, "Build Corpus");

        auto* reset =
            button(window, "Reset");

        QVERIFY(build != nullptr);
        QVERIFY(reset != nullptr);

        build->click();
        reset->click();

        QTest::qWait(700);

        QVERIFY2(
            !corpus_status(window).contains("chunks"),
            "stale corpus was accepted after reset");

        QVERIFY(!build->isEnabled());
    }

    void build_failure_is_reported() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path =
            create_file(directory.path(), "failure.txt");

        QVERIFY(!path.isEmpty());

        MainWindow window;
        QVERIFY(window.add_document_file(path));

        window.set_build_failure_for_testing(true);

        auto* build =
            button(window, "Build Corpus");

        QVERIFY(build != nullptr);

        build->click();

        QTRY_VERIFY_WITH_TIMEOUT(
            application_status(window).contains(
                "Build failed"),
            5000);

        QVERIFY(corpus_status(window).contains(
            "Not built"));

        QVERIFY(build->isEnabled());

        window.set_build_failure_for_testing(false);

        build->click();

        QTRY_VERIFY_WITH_TIMEOUT(
            corpus_status(window).contains("chunks"),
            5000);

        QVERIFY(application_status(window).contains(
            "Corpus built"));
    }

    void remote_add_during_build() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString first =
            create_file(directory.path(), "first.txt");

        const QString second =
            create_file(
                directory.path(),
                "remote second.txt");

        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        MainWindow window;
        QVERIFY(window.add_document_file(first));

        window.set_build_delay_ms(700);

        const QString ipc =
            socket_path(directory.path());

        aiws::RemoteAddServer server(
            ipc.toStdString(),
            gui_add_callback(window));

        QVERIFY(server.start());

        auto* build =
            button(window, "Build Corpus");

        QVERIFY(build != nullptr);

        build->click();

        QVERIFY(corpus_status(window).contains(
            "Building"));

        QProcess sender;

        sender.start(
            sender_path(),
            QStringList{
                "--socket",
                ipc,
                "add",
                second
            });

        QVERIFY(sender.waitForStarted(2000));

        QVERIFY2(
            wait_for_process(sender, 5000),
            "remote sender did not finish");

        QCOMPARE(sender.exitCode(), 0);

        QVERIFY(application_status(window).contains(
            "Document added"));

        QVERIFY(corpus_status(window).contains(
            "Not built"));

        QTRY_VERIFY_WITH_TIMEOUT(
            build->isEnabled(),
            5000);

        QVERIFY2(
            !corpus_status(window).contains("chunks"),
            "remote add accepted a stale corpus");

        server.stop();
    }

    void remote_add_special_filenames() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window;

        const QString ipc =
            socket_path(directory.path());

        aiws::RemoteAddServer server(
            ipc.toStdString(),
            gui_add_callback(window));

        QVERIFY(server.start());

        const QStringList names = {
            "file with spaces.txt",
            QString::fromUtf8("café_日本語.txt"),
            "file\nwith newline.txt"
        };

        for (const QString& name : names) {
            const QString path =
                create_file(directory.path(), name);

            QVERIFY2(
                !path.isEmpty(),
                "could not create special filename");

            QProcess sender;

            sender.start(
                sender_path(),
                QStringList{
                    "--socket",
                    ipc,
                    "add",
                    path
                });

            QVERIFY(sender.waitForStarted(2000));

            QVERIFY2(
                wait_for_process(sender, 5000),
                "special filename sender timed out");

            QCOMPARE(sender.exitCode(), 0);

            QVERIFY2(
                contains_document(window, name),
                "remote document not found in workspace");
        }

        auto* build =
            button(window, "Build Corpus");

        QVERIFY(build != nullptr);
        QVERIFY(build->isEnabled());

        server.stop();
    }

    void split_socket_writes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString ipc =
            socket_path(directory.path());

        const QString path =
            create_file(
                directory.path(),
                "split request.txt");

        QVERIFY(!path.isEmpty());

        std::atomic<int> accepted{0};

        aiws::RemoteAddServer server(
            ipc.toStdString(),
            [&accepted, path](
                const std::string& received) {

                if (QString::fromUtf8(
                        received.data(),
                        static_cast<qsizetype>(
                            received.size())) == path) {

                    accepted.fetch_add(1);
                    return true;
                }

                return false;
            });

        QVERIFY(server.start());

        auto result =
            std::async(
                std::launch::async,
                [ipc, path]() {
                    return send_split_request(
                        ipc,
                        path);
                });

        QVERIFY2(
            wait_for_future(result, 3000),
            "split socket request timed out");

        QVERIFY(result.get());
        QCOMPARE(accepted.load(), 1);

        server.stop();
    }

    void socket_shutdown_cleanup() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString ipc =
            socket_path(directory.path());

        aiws::RemoteAddServer server(
            ipc.toStdString(),
            [](const std::string&) {
                return true;
            });

        QVERIFY(server.start());
        QVERIFY(QFileInfo::exists(ipc));

        const int fd = connect_socket(ipc);
        QVERIFY(fd >= 0);

        const unsigned char partial_header[2] = {
            0, 0
        };

        QVERIFY(send_bytes(
            fd,
            partial_header,
            sizeof(partial_header)));

        QTest::qWait(100);

        QElapsedTimer timer;
        timer.start();

        server.stop();

        QVERIFY2(
            timer.elapsed() < 500,
            "socket server shutdown exceeded 500 ms");

        QVERIFY2(
            !QFileInfo::exists(ipc),
            "socket path was not removed");

        ::close(fd);
    }
};

QTEST_MAIN(StudentTests)

#include "student_tests.moc"
