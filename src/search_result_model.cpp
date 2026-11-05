#include "aiws/gui/search_result_model.hpp"

#include <QString>

namespace aiws::gui {

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return 5;
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() ||
        role != Qt::DisplayRole ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(results_.size())) {
        return {};
    }

    const auto& result = results_[static_cast<std::size_t>(index.row())];

    switch (index.column()) {
    case 0:
        return index.row() + 1;

    case 1:
        return QString::fromStdString(result.document_id);

    case 2:
        return static_cast<qulonglong>(result.chunk_sequence);

    case 3:
        return result.score;

    case 4:
        return QString::fromStdString(result.text);

    default:
        return {};
    }
}

QVariant SearchResultModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole) {
        return {};
    }

    if (orientation == Qt::Vertical) {
        return {};
    }

    switch (section) {
    case 0:
        return QString("Rank");

    case 1:
        return QString("Document");

    case 2:
        return QString("Chunk");

    case 3:
        return QString("Score");

    case 4:
        return QString("Text");

    default:
        return {};
    }
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if (row < 0 || row >= static_cast<int>(results_.size())) {
        return nullptr;
    }

    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui