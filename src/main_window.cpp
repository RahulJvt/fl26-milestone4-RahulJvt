
#include "aiws/gui/main_window.hpp"

#include "aiws/document.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aiws::gui {

struct MainWindow::BuildResult {
    std::uint64_t revision{0};
    std::unique_ptr<ProcessingCore> core;
    std::string error;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    workspace_widget_ = new WorkspaceWidget(this);
    search_widget_ = new SearchWidget(this);
    term_inspector_widget_ = new TermInspectorWidget(this);
    context_widget_ = new ContextWidget(this);

    status_label_ = new QLabel("Current status: Ready", this);

    reset_button_ = new QPushButton("Reset", this);
    exit_button_ = new QPushButton("Exit", this);

    reset_button_->setStyleSheet(
        "QPushButton {"
        "background-color: #d6a84b;"
        "color: black;"
        "font-weight: bold;"
        "padding: 6px 20px;"
        "}");

    exit_button_->setStyleSheet(
        "QPushButton {"
        "background-color: #4f78a8;"
        "color: white;"
        "font-weight: bold;"
        "padding: 6px 20px;"
        "}");

    auto* title_label = new QLabel("AI Workspace", this);
    title_label->setStyleSheet(
        "QLabel {"
        "background-color: #702632;"
        "color: white;"
        "font-size: 22px;"
        "font-weight: bold;"
        "padding: 12px;"
        "}");

    auto* bottom_pair = new QHBoxLayout;
    bottom_pair->addWidget(term_inspector_widget_, 1);
    bottom_pair->addWidget(context_widget_, 1);

    auto* status_layout = new QHBoxLayout;
    status_layout->addWidget(status_label_);
    status_layout->addStretch();
    status_layout->addWidget(reset_button_);
    status_layout->addWidget(exit_button_);

    auto* main_layout = new QVBoxLayout(central);
    main_layout->addWidget(title_label);
    main_layout->addWidget(workspace_widget_, 2);
    main_layout->addWidget(search_widget_, 3);
    main_layout->addLayout(bottom_pair, 2);
    main_layout->addLayout(status_layout);

    connect(workspace_widget_,
            &WorkspaceWidget::add_document_requested,
            this,
            &MainWindow::add_document);

    connect(workspace_widget_,
            &WorkspaceWidget::build_corpus_requested,
            this,
            &MainWindow::build_corpus);

    connect(workspace_widget_,
            &WorkspaceWidget::document_selected,
            this,
            &MainWindow::document_selected);

    connect(search_widget_,
            &SearchWidget::search_requested,
            this,
            &MainWindow::search_requested);

    connect(search_widget_,
            &SearchWidget::query_changed,
            this,
            &MainWindow::query_changed);

    connect(search_widget_,
            &SearchWidget::source_document_requested,
            this,
            &MainWindow::source_document_requested);

    connect(search_widget_,
            &SearchWidget::selected_chunk_changed,
            term_inspector_widget_,
            &TermInspectorWidget::set_selected_chunk);

    connect(term_inspector_widget_,
            &TermInspectorWidget::term_inspection_requested,
            this,
            &MainWindow::term_inspection_requested);

    connect(context_widget_,
            &ContextWidget::build_context_requested,
            this,
            &MainWindow::build_context_requested);

    connect(reset_button_,
            &QPushButton::clicked,
            this,
            &MainWindow::reset_application);

    connect(exit_button_,
            &QPushButton::clicked,
            this,
            &MainWindow::exit_application);

    auto* build_timer = new QTimer(this);
    build_timer->setInterval(20);

    connect(build_timer,
            &QTimer::timeout,
            this,
            &MainWindow::check_build_completion);

    build_timer->start();

    update_action_states();
}

MainWindow::~MainWindow() {
    cancel_build_.store(true);

    if (build_thread_.joinable()) {
        build_thread_.join();
    }
}

void MainWindow::set_exit_callback(
    ExitCallback callback) noexcept {

    exit_callback_ = callback;
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = std::max(0, ms_per_document);
}

void MainWindow::set_build_failure_for_testing(bool enabled) {
    force_build_failure_ = enabled;
}

bool MainWindow::add_document_file(const QString& path) {
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    const QString text = stream.readAll();

    if (stream.status() != QTextStream::Ok) {
        return false;
    }

    QFileInfo info(path);

    const QString absolute_path = info.absoluteFilePath();
    const QString title = info.fileName();

    workspace_.add_document(
        Document(
            absolute_path.toStdString(),
            title.toStdString(),
            text.toStdString()));

    workspace_widget_->add_document(title);

    ++workspace_revision_;
    invalidate_corpus();

    status_label_->setText(
        "Current status: Document added.");

    update_action_states();
    return true;
}

void MainWindow::add_document() {
    const QString file_path =
        QFileDialog::getOpenFileName(
            this,
            "Add Document",
            QString(),
            "Text Files (*.txt);;All Files (*)");

    if (file_path.isEmpty()) {
        return;
    }

    if (!add_document_file(file_path)) {
        status_label_->setText(
            "Current status: Could not open document.");
    }
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty() || build_running_) {
        return;
    }

    if (build_thread_.joinable()) {
        build_thread_.join();
    }

    cancel_build_.store(false);

    const Workspace snapshot = workspace_;
    const std::uint64_t revision = workspace_revision_;
    const int delay = build_delay_ms_;
    const bool force_failure = force_build_failure_;
    const std::size_t document_count =
        snapshot.documents().size();

    std::promise<std::unique_ptr<BuildResult>> promise;
    build_future_ = promise.get_future();

    build_running_ = true;
    corpus_current_ = false;

    workspace_widget_->set_corpus_status(
        "Corpus: Building");

    clear_processing_outputs();

    status_label_->setText(
        "Current status: Building corpus...");

    update_action_states();

    try {
        build_thread_ = std::thread(
            [this, snapshot, revision, delay,
             force_failure, document_count,
             promise = std::move(promise)]() mutable {

                auto result = std::make_unique<BuildResult>();
                result->revision = revision;

                try {
                    if (force_failure) {
                        throw std::runtime_error(
                            "simulated build failure");
                    }

                    auto core =
                        std::make_unique<ProcessingCore>();

                    core->rebuild(snapshot);

                    for (std::size_t i = 0;
                         i < document_count;
                         ++i) {

                        int remaining = delay;

                        while (remaining > 0 &&
                               !cancel_build_.load()) {

                            const int interval =
                                std::min(remaining, 10);

                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(
                                    interval));

                            remaining -= interval;
                        }

                        if (cancel_build_.load()) {
                            result->error = "build cancelled";
                            break;
                        }
                    }

                    if (result->error.empty() &&
                        !cancel_build_.load()) {
                        result->core = std::move(core);
                    } else if (result->error.empty()) {
                        result->error = "build cancelled";
                    }
                } catch (const std::exception& error) {
                    result->error = error.what();
                } catch (...) {
                    result->error = "unknown build error";
                }

                promise.set_value(std::move(result));
            });
    } catch (const std::exception& error) {
        build_running_ = false;
        corpus_current_ = false;

        workspace_widget_->set_corpus_status(
            "Corpus: Not built");

        status_label_->setText(
            QString("Current status: Build failed: %1")
                .arg(error.what()));

        update_action_states();
    }
}

void MainWindow::check_build_completion() {
    if (!build_running_ || !build_future_.valid()) {
        return;
    }

    if (build_future_.wait_for(
            std::chrono::milliseconds(0)) !=
        std::future_status::ready) {
        return;
    }

    std::unique_ptr<BuildResult> result;

    try {
        result = build_future_.get();
    } catch (const std::exception& error) {
        result = std::make_unique<BuildResult>();
        result->revision = workspace_revision_;
        result->error = error.what();
    } catch (...) {
        result = std::make_unique<BuildResult>();
        result->revision = workspace_revision_;
        result->error = "unknown build error";
    }

    if (build_thread_.joinable()) {
        build_thread_.join();
    }

#ifdef AIWS_FAULT_LOSE_FAILURE
    if (!result->error.empty() || !result->core) {
        return;
    }
#endif

    build_running_ = false;

#ifndef AIWS_FAULT_ACCEPT_STALE
    if (result->revision != workspace_revision_) {
        update_action_states();
        return;
    }
#endif

    if (!result->error.empty() || !result->core) {
        corpus_current_ = false;

        workspace_widget_->set_corpus_status(
            "Corpus: Not built");

        clear_processing_outputs();

        status_label_->setText(
            QString("Current status: Build failed: %1")
                .arg(QString::fromStdString(
                    result->error.empty()
                        ? "no corpus produced"
                        : result->error)));

        update_action_states();
        return;
    }

    processing_core_ = std::move(*result->core);
    corpus_current_ = true;

    workspace_widget_->set_corpus_status(
        QString("Corpus: %1 chunks")
            .arg(static_cast<qulonglong>(
                processing_core_.chunk_count())));

    clear_processing_outputs();

    status_label_->setText(
        "Current status: Corpus built.");

    update_action_states();
}

void MainWindow::document_selected(int row) {
    if (row < 0 ||
        row >= static_cast<int>(
            workspace_.documents().size())) {

        workspace_widget_->clear_selected_document();
        return;
    }

    const auto& document =
        workspace_.documents()[
            static_cast<std::size_t>(row)];

    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text()));
}

void MainWindow::search_requested(
    const QString& query,
    int k) {

    if (!corpus_current_) {
        return;
    }

    try {
        const auto results =
            processing_core_.search(
                query.toStdString(),
                k);

        search_widget_->set_results(results);
        context_widget_->clear_context();

        status_label_->setText(
            QString("Current status: Search complete. %1 result(s).")
                .arg(static_cast<qulonglong>(
                    results.size())));
    } catch (const std::exception& error) {
        status_label_->setText(
            QString("Current status: Search failed: %1")
                .arg(error.what()));
    }
}

void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states();
}

void MainWindow::build_context_requested(
    int token_budget) {

    if (!corpus_current_ ||
        !search_widget_->has_query()) {
        return;
    }

    try {
        const auto items =
            processing_core_.build_context(
                search_widget_->query().toStdString(),
                search_widget_->result_count(),
                static_cast<std::size_t>(
                    token_budget));

        context_widget_->set_context(items);

        status_label_->setText(
            "Current status: Context built.");
    } catch (const std::exception& error) {
        status_label_->setText(
            QString("Current status: Context failed: %1")
                .arg(error.what()));
    }
}

void MainWindow::source_document_requested(
    const QString& document_id) {

    const auto& documents = workspace_.documents();

    for (std::size_t i = 0;
         i < documents.size();
         ++i) {

        if (documents[i].id() ==
            document_id.toStdString()) {

            workspace_widget_->select_document(
                static_cast<int>(i));
            return;
        }
    }
}

void MainWindow::term_inspection_requested(
    const QString& term,
    const QString& chunk_id) {

    if (!corpus_current_) {
        return;
    }

    const QString effective_term = term.trimmed();

    if (effective_term.isEmpty()) {
        return;
    }

    try {
        const std::size_t document_frequency =
            processing_core_.document_frequency(
                effective_term.toStdString());

        if (chunk_id.isEmpty()) {
            term_inspector_widget_->set_statistics(
                document_frequency,
                false,
                0);
        } else {
            const std::size_t chunk_frequency =
                processing_core_.term_frequency(
                    effective_term.toStdString(),
                    chunk_id.toStdString());

            term_inspector_widget_->set_statistics(
                document_frequency,
                true,
                chunk_frequency);
        }

        status_label_->setText(
            "Current status: Term inspected.");
    } catch (const std::exception& error) {
        status_label_->setText(
            QString("Current status: Inspection failed: %1")
                .arg(error.what()));
    }
}

void MainWindow::reset_application() {
    workspace_.clear();
    ++workspace_revision_;

    corpus_current_ = false;

    std::vector<Resettable*> components{
        workspace_widget_,
        search_widget_,
        term_inspector_widget_,
        context_widget_
    };

    for (Resettable* component : components) {
        component->reset();
    }

    status_label_->setText(
        "Current status: Application reset.");

    update_action_states();
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    const bool has_documents =
        !workspace_.documents().empty();

    workspace_widget_->set_build_corpus_enabled(
        has_documents && !build_running_);

    const bool processing_available =
        corpus_current_;

    const bool query_available =
        search_widget_->has_query();

    search_widget_->set_search_enabled(
        processing_available && query_available);

    term_inspector_widget_->set_inspection_enabled(
        processing_available);

    context_widget_->set_build_context_enabled(
        processing_available && query_available);
}

void MainWindow::invalidate_corpus() {
    corpus_current_ = false;

    workspace_widget_->set_corpus_status(
        "Corpus: Not built");

    clear_processing_outputs();
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();

    term_inspector_widget_->set_selected_chunk(
        QString());

    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
