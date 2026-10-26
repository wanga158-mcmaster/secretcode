#pragma once
#include "common.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace opt::league {

using Matrix = std::vector<std::vector<double>>;

inline void validate(const Matrix& payoff) {
    if (payoff.empty() || payoff.front().empty()) throw std::runtime_error("league matrix must contain a row and a column");
    const auto columns = payoff.front().size();
    for (const auto& row : payoff) {
        if (row.size() != columns) throw std::runtime_error("league matrix is ragged or incomplete");
        for (double value : row)
            if (!std::isfinite(value)) throw std::runtime_error("league matrix contains a nonfinite payoff");
    }
}

struct Solution {
    std::vector<double> policy, adversary;
    double lower{}, upper{}, gap{};
};

inline std::vector<double> normalized(const std::vector<std::uint64_t>& counts) {
    long double total = 0;
    for (auto count : counts) total += count;
    if (!(total > 0)) throw std::runtime_error("league strategy has no mass");
    std::vector<double> result;
    for (auto count : counts) result.push_back(static_cast<double>(static_cast<long double>(count) / total));
    return result;
}

// These are valid security bounds for this finite, fully evaluated empirical matrix
// at every iteration. They are not a confidence interval or a bound on the full game.
inline Solution bounds(const Matrix& payoff, const std::vector<std::uint64_t>& rows,
                       const std::vector<std::uint64_t>& columns) {
    validate(payoff);
    if (rows.size() != payoff.size() || columns.size() != payoff.front().size())
        throw std::runtime_error("league strategy dimensions disagree with matrix");
    Solution result;
    result.policy = normalized(rows);
    result.adversary = normalized(columns);
    result.lower = std::numeric_limits<double>::infinity();
    for (std::size_t j = 0; j < columns.size(); ++j) {
        double value = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) value += result.policy[i] * payoff[i][j];
        result.lower = std::min(result.lower, value);
    }
    result.upper = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        double value = 0;
        for (std::size_t j = 0; j < columns.size(); ++j) value += result.adversary[j] * payoff[i][j];
        result.upper = std::max(result.upper, value);
    }
    result.gap = std::max(0.0, result.upper - result.lower);
    return result;
}

struct FictitiousPlay {
    std::vector<std::uint64_t> row_counts, column_counts;
    std::uint64_t steps{};

    FictitiousPlay(std::size_t rows, std::size_t columns)
        : row_counts(rows, 1), column_counts(columns, 1) {
        if (!rows || !columns) throw std::runtime_error("league strategy dimensions must be positive");
    }

    void step(const Matrix& payoff) {
        validate(payoff);
        if (row_counts.size() != payoff.size() || column_counts.size() != payoff.front().size())
            throw std::runtime_error("league counts disagree with matrix");
        std::size_t best_row = 0, worst_column = 0;
        long double best = -std::numeric_limits<long double>::infinity();
        long double worst = std::numeric_limits<long double>::infinity();
        for (std::size_t i = 0; i < row_counts.size(); ++i) {
            long double value = 0;
            for (std::size_t j = 0; j < column_counts.size(); ++j) value += payoff[i][j] * column_counts[j];
            if (value > best) { best = value; best_row = i; }
        }
        for (std::size_t j = 0; j < column_counts.size(); ++j) {
            long double value = 0;
            for (std::size_t i = 0; i < row_counts.size(); ++i) value += payoff[i][j] * row_counts[i];
            if (value < worst) { worst = value; worst_column = j; }
        }
        if (row_counts[best_row] == std::numeric_limits<std::uint64_t>::max()
            || column_counts[worst_column] == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("league count overflow");
        ++row_counts[best_row];
        ++column_counts[worst_column];
        ++steps;
    }

    Json checkpoint() const {
        Json rows = Json::array(), columns = Json::array();
        for (auto n : row_counts) rows.push_back(n);
        for (auto n : column_counts) columns.push_back(n);
        return { {"row_counts", rows}, {"column_counts", columns}, {"steps", steps} };
    }

    void restore(const Json& value) {
        const auto& rows = value.at("row_counts").as_array();
        const auto& columns = value.at("column_counts").as_array();
        if (rows.size() != row_counts.size() || columns.size() != column_counts.size())
            throw std::runtime_error("league checkpoint matrix dimensions changed");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if(!rows[i].is_number_integer() || rows[i].get<std::int64_t>()<0)throw std::runtime_error("invalid league row count");
            row_counts[i] = rows[i].get<std::uint64_t>();
        }
        for (std::size_t j = 0; j < columns.size(); ++j) {
            if(!columns[j].is_number_integer() || columns[j].get<std::int64_t>()<0)throw std::runtime_error("invalid league column count");
            column_counts[j] = columns[j].get<std::uint64_t>();
        }
        steps = value.at("steps").get<std::uint64_t>();
        normalized(row_counts);
        normalized(column_counts);
    }
};

inline Solution solve(const Matrix& payoff, std::uint64_t steps = 10000) {
    validate(payoff);
    FictitiousPlay state(payoff.size(), payoff.front().size());
    for (std::uint64_t i = 0; i < steps; ++i) state.step(payoff);
    return bounds(payoff, state.row_counts, state.column_counts);
}

inline Json json(const Solution& solution) {
    Json policies = Json::array(), adversaries = Json::array();
    for (double p : solution.policy) policies.push_back(p);
    for (double p : solution.adversary) adversaries.push_back(p);
    return { {"policy", policies}, {"adversary", adversaries}, {"empirical_lower", solution.lower},
             {"empirical_upper", solution.upper}, {"empirical_gap", solution.gap},
             {"claim", "security bounds for the finite fully evaluated empirical matrix only"} };
}

} // namespace opt::league
