#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/search_result_model.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace aiws::gui {

SearchWidget::SearchWidget(QWidget* parent)
    : QWidget(parent) {

    query_edit_ = new QLineEdit(this);
    query_edit_->setPlaceholderText("Enter a search query");

    result_count_spin_ = new QSpinBox(this);
    result_count_spin_->setRange(1, 100);
    result_count_spin_->setValue(3);

    search_button_ = new QPushButton("Search", this);
    search_button_->setEnabled(false);

    result_model_ = new SearchResultModel(this);

    results_view_ = new QTableView(this);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(
        QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    results_view_->horizontalHeader()->setStretchLastSection(true);

    selected_result_text_ = new QPlainTextEdit(this);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText(
        "Select a search result to view its details.");

    view_source_button_ = new QPushButton("View Source", this);
    view_source_button_->setEnabled(false);

    auto* query_layout = new QHBoxLayout;
    query_layout->addWidget(new QLabel("Query:", this));
    query_layout->addWidget(query_edit_);
    query_layout->addWidget(new QLabel("Results:", this));
    query_layout->addWidget(result_count_spin_);
    query_layout->addWidget(search_button_);

    auto* selected_layout = new QHBoxLayout;
    selected_layout->addWidget(
        new QLabel("Selected Result:", this));
    selected_layout->addStretch();
    selected_layout->addWidget(view_source_button_);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addLayout(query_layout);
    main_layout->addWidget(results_view_);
    main_layout->addLayout(selected_layout);
    main_layout->addWidget(selected_result_text_);

    connect(query_edit_,
            &QLineEdit::textChanged,
            this,
            &SearchWidget::query_changed);

    connect(search_button_,
            &QPushButton::clicked,
            this,
            &SearchWidget::request_search);

    connect(results_view_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &SearchWidget::show_selected_result);

    connect(view_source_button_,
            &QPushButton::clicked,
            this,
            &SearchWidget::request_source_document);
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query_edit_->text().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    result_model_->set_results(std::move(results));

    results_view_->clearSelection();
    results_view_->setCurrentIndex(QModelIndex());

    selected_result_text_->clear();
    view_source_button_->setEnabled(false);

    emit selected_chunk_changed(QString());
}

void SearchWidget::clear_results() {
    result_model_->clear();

    results_view_->clearSelection();
    results_view_->setCurrentIndex(QModelIndex());

    selected_result_text_->clear();
    view_source_button_->setEnabled(false);

    emit selected_chunk_changed(QString());
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);

    clear_results();

    search_button_->setEnabled(false);
}

void SearchWidget::request_search() {
    if (!has_query()) {
        return;
    }

    emit search_requested(
        query_edit_->text(),
        result_count_spin_->value());
}

void SearchWidget::show_selected_result() {
    const QModelIndexList selected =
        results_view_->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    const SearchResult* result =
        result_model_->result_at(selected.first().row());

    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    QString details;

    details += QString("Document: %1\n")
        .arg(QString::fromStdString(result->document_id));

    details += QString("Chunk: %1\n")
        .arg(static_cast<qulonglong>(result->chunk_sequence));

    details += QString("Score: %1\n")
        .arg(result->score);

    details += QString("Text: %1")
        .arg(QString::fromStdString(result->text));

    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);

    emit selected_chunk_changed(
        QString::fromStdString(result->chunk_id));
}

void SearchWidget::request_source_document() {
    const QModelIndexList selected =
        results_view_->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        return;
    }

    const SearchResult* result =
        result_model_->result_at(selected.first().row());

    if (result == nullptr) {
        return;
    }

    emit source_document_requested(
        QString::fromStdString(result->document_id));
}

}  // namespace aiws::gui