#pragma once

// Least-squares fit of several sines at known frequencies plus DC, for alias
// containment measurements: the residual is everything not explained by the
// intended components. Independent of the code under test.

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace ak4522::test {

struct MultiSineFit {
    std::vector<double> amplitudes; // one per frequency
    std::vector<double> phases;     // y ~ amplitude * sin(2 pi f t + phase)
    double residualRms;
};

inline MultiSineFit fitSines(std::span<const double> y, std::span<const double> freqs, double rate,
                             double startTime = 0.0)
{
    constexpr double pi = 3.14159265358979323846;
    const std::size_t k = 1 + 2 * freqs.size();
    std::vector<double> a(k * (k + 1), 0.0); // augmented normal equations
    std::vector<double> basis(k);
    auto fill = [&](std::size_t n) {
        const double t = startTime + static_cast<double>(n) / rate;
        basis[0] = 1.0;
        for (std::size_t i = 0; i < freqs.size(); ++i) {
            const double ph = 2.0 * pi * freqs[i] * t;
            basis[1 + 2 * i] = std::cos(ph);
            basis[2 + 2 * i] = std::sin(ph);
        }
    };
    for (std::size_t n = 0; n < y.size(); ++n) {
        fill(n);
        for (std::size_t i = 0; i < k; ++i) {
            for (std::size_t j = 0; j < k; ++j) {
                a[i * (k + 1) + j] += basis[i] * basis[j];
            }
            a[i * (k + 1) + k] += basis[i] * y[n];
        }
    }
    // Gaussian elimination with partial pivoting.
    for (std::size_t c = 0; c < k; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < k; ++r) {
            if (std::abs(a[r * (k + 1) + c]) > std::abs(a[pivot * (k + 1) + c])) {
                pivot = r;
            }
        }
        for (std::size_t j = 0; j <= k; ++j) {
            std::swap(a[c * (k + 1) + j], a[pivot * (k + 1) + j]);
        }
        for (std::size_t r = 0; r < k; ++r) {
            if (r != c) {
                const double f = a[r * (k + 1) + c] / a[c * (k + 1) + c];
                for (std::size_t j = c; j <= k; ++j) {
                    a[r * (k + 1) + j] -= f * a[c * (k + 1) + j];
                }
            }
        }
    }
    std::vector<double> coef(k);
    for (std::size_t i = 0; i < k; ++i) {
        coef[i] = a[i * (k + 1) + k] / a[i * (k + 1) + i];
    }
    double sq = 0.0;
    for (std::size_t n = 0; n < y.size(); ++n) {
        fill(n);
        double model = 0.0;
        for (std::size_t i = 0; i < k; ++i) {
            model += coef[i] * basis[i];
        }
        const double e = y[n] - model;
        sq += e * e;
    }
    MultiSineFit out;
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        out.amplitudes.push_back(std::hypot(coef[1 + 2 * i], coef[2 + 2 * i]));
        out.phases.push_back(std::atan2(coef[1 + 2 * i], coef[2 + 2 * i]));
    }
    out.residualRms = std::sqrt(sq / static_cast<double>(y.size()));
    return out;
}

} // namespace ak4522::test
