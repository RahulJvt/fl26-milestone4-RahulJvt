#include "aiws/gui/context_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>

namespace aiws::gui {

ContextWidget::ContextWidget(QWidget* parent)
    : QWidget(parent) {

    context_budget_slider_ = new QSlider(Qt::Horizontal, this);
    context_budget_slider_->setRange(50, 1000);
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    context_budget_slider_->setValue(300);

    budget_value_label_ = new QLabel("300", this);

    build_context_button_ = new QPushButton("Build Context", this);
    build_context_button_->setEnabled(false);

    context_text_ = new QPlainTextEdit(this);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText(
        "Built context will appear here.");

    auto* controls_layout = new QHBoxLayout;
    controls_layout->addWidget(new QLabel("Token budget:", this));
    controls_layout->addWidget(context_budget_slider_);
    controls_layout->addWidget(budget_value_label_);
    controls_layout->addWidget(build_context_button_);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addLayout(controls_layout);
    main_layout->addWidget(context_text_);

    connect(context_budget_slider_,
            &QSlider::valueChanged,
            this,
            &ContextWidget::budget_changed);

    connect(build_context_button_,
            &QPushButton::clicked,
            this,
            &ContextWidget::request_context);
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(
    const std::vector<ContextItem>& items) {

    context_text_->clear();

    QString output;

    for (const auto& item : items) {
        output += QString("Document: %1\n")
                      .arg(QString::fromStdString(item.document_id));

        output += QString("Chunk: %1\n")
                      .arg(static_cast<qulonglong>(
                          item.chunk_sequence));

        output += QString("Tokens: %1\n")
                      .arg(static_cast<qulonglong>(
                          item.token_count));

        output += QString("Score: %1\n")
                      .arg(item.score);

        output += QString("Truncated: %1\n")
                      .arg(item.truncated ? "Yes" : "No");

        output += QString("Text: %1\n\n")
                      .arg(QString::fromStdString(item.text));
    }

    context_text_->setPlainText(output);
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(300);
    budget_value_label_->setText("300");

    context_text_->clear();

    build_context_button_->setEnabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(
        context_budget_slider_->value());
}

void ContextWidget::budget_changed(int value) {
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui