
#pragma once

#include <QMainWindow>

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>

class QLabel;
class QPushButton;
class QString;
class QWidget;

namespace aiws::gui {

class ContextWidget;
class SearchWidget;
class TermInspectorWidget;
class WorkspaceWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    using ExitCallback = void (*)(QWidget* parent);

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void set_exit_callback(ExitCallback callback) noexcept;

    bool add_document_file(const QString& path);
    void set_build_delay_ms(int ms_per_document);
    void set_build_failure_for_testing(bool enabled);

private slots:
    void add_document();
    void build_corpus();
    void check_build_completion();

    void document_selected(int row);
    void search_requested(const QString& query, int k);
    void query_changed(const QString& query);
    void build_context_requested(int token_budget);
    void source_document_requested(const QString& document_id);
    void term_inspection_requested(
        const QString& term,
        const QString& chunk_id);

    void reset_application();
    void exit_application();

private:
    struct BuildResult;

    void update_action_states();
    void invalidate_corpus();
    void clear_processing_outputs();

    Workspace workspace_;
    ProcessingCore processing_core_;
    bool corpus_current_{false};
    ExitCallback exit_callback_{nullptr};

    std::uint64_t workspace_revision_{0};
    int build_delay_ms_{0};
    bool force_build_failure_{false};

    std::atomic<bool> cancel_build_{false};
    std::thread build_thread_;
    std::future<std::unique_ptr<BuildResult>> build_future_;
    bool build_running_{false};

    WorkspaceWidget* workspace_widget_{nullptr};
    SearchWidget* search_widget_{nullptr};
    TermInspectorWidget* term_inspector_widget_{nullptr};
    ContextWidget* context_widget_{nullptr};

    QLabel* status_label_{nullptr};
    QPushButton* reset_button_{nullptr};
    QPushButton* exit_button_{nullptr};
};

}  // namespace aiws::gui
