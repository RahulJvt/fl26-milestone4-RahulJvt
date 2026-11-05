#include "aiws/gui/workspace_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

WorkspaceWidget::WorkspaceWidget(QWidget* parent)
    : QWidget(parent) {

    document_list_ = new QListWidget(this);

    add_document_button_ = new QPushButton("Add Document", this);
    build_corpus_button_ = new QPushButton("Build Corpus", this);

    corpus_status_label_ = new QLabel("Corpus: Not built", this);

    document_title_label_ = new QLabel("Selected Document -", this);

    document_text_ = new QPlainTextEdit(this);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText(
        "Select a document to view its contents.");

    build_corpus_button_->setEnabled(false);

    auto* button_layout = new QHBoxLayout;
    button_layout->addWidget(add_document_button_);
    button_layout->addWidget(build_corpus_button_);
    button_layout->addStretch();

    auto* workspace_layout = new QVBoxLayout;
    workspace_layout->addLayout(button_layout);
    workspace_layout->addWidget(document_list_);
    workspace_layout->addWidget(corpus_status_label_);

    auto* selected_layout = new QVBoxLayout;
    selected_layout->addWidget(document_title_label_);
    selected_layout->addWidget(document_text_);

    auto* main_layout = new QHBoxLayout(this);
    main_layout->addLayout(workspace_layout, 1);
    main_layout->addLayout(selected_layout, 1);

    connect(add_document_button_,
            &QPushButton::clicked,
            this,
            &WorkspaceWidget::add_document_requested);

    connect(build_corpus_button_,
            &QPushButton::clicked,
            this,
            &WorkspaceWidget::build_corpus_requested);

    connect(document_list_,
            &QListWidget::currentRowChanged,
            this,
            &WorkspaceWidget::document_selected);
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    if (row >= 0 && row < document_list_->count()) {
        document_list_->setCurrentRow(row);
    }
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                               const QString& text) {
    document_title_label_->setText(
        QString("Selected Document - %1").arg(title));

    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_list_->clearSelection();
    document_list_->setCurrentRow(-1);

    document_title_label_->setText("Selected Document -");
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();

    document_title_label_->setText("Selected Document -");
    document_text_->clear();

    corpus_status_label_->setText("Corpus: Not built");

    build_corpus_button_->setEnabled(false);
    add_document_button_->setEnabled(true);
}

}  // namespace aiws::gui