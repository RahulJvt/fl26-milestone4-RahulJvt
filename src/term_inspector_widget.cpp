#include "aiws/gui/term_inspector_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent)
    : QWidget(parent) {

    term_edit_ = new QLineEdit(this);
    term_edit_->setPlaceholderText("Enter a term");

    inspect_button_ = new QPushButton("Inspect", this);
    inspect_button_->setEnabled(false);

    document_frequency_value_ = new QLabel("-", this);
    chunk_frequency_value_ = new QLabel("-", this);

    auto* term_layout = new QHBoxLayout;
    term_layout->addWidget(new QLabel("Term:", this));
    term_layout->addWidget(term_edit_);
    term_layout->addWidget(inspect_button_);

    auto* statistics_layout = new QHBoxLayout;

    statistics_layout->addWidget(
        new QLabel("Document Frequency:", this));
    statistics_layout->addWidget(document_frequency_value_);

    statistics_layout->addSpacing(20);

    statistics_layout->addWidget(
        new QLabel("Selected Chunk TF:", this));
    statistics_layout->addWidget(chunk_frequency_value_);

    statistics_layout->addStretch();

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addLayout(term_layout);
    main_layout->addLayout(statistics_layout);

    connect(inspect_button_,
            &QPushButton::clicked,
            this,
            &TermInspectorWidget::request_inspection);
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    selected_chunk_id_ = chunk_id;

    // A different selected result means any displayed TF
    // may no longer describe the current chunk.
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::set_statistics(
    std::size_t document_frequency,
    bool has_chunk_frequency,
    std::size_t chunk_frequency) {

    document_frequency_value_->setText(
        QString::number(
            static_cast<qulonglong>(document_frequency)));

    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(
            QString::number(
                static_cast<qulonglong>(chunk_frequency)));
    } else {
        chunk_frequency_value_->setText("-");
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("-");
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::reset() {
    term_edit_->clear();

    selected_chunk_id_.clear();

    clear_statistics();

    inspect_button_->setEnabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();

    if (term.isEmpty()) {
        return;
    }

    emit term_inspection_requested(
        term,
        selected_chunk_id_);
}

}  // namespace aiws::gui