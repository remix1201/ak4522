#pragma once

// Least-squares sine fitting for converter and host-path measurements. Shared
// by the tests; independent of the code under test.

#include <cmath>
#include <span>
#include <utility>

namespace ak4522::test {

struct SineFit {
    double amplitude;
    double phase;       // y ~ amplitude * sin(2 pi f t + phase)
    double residualRms; // after removing the fitted sine and DC
};

// Fits a + b cos(w t) + c sin(w t) with t = startTime + n / rate.
inline SineFit fitSine(std::span<const double> y, double freq, double rate, double startTime = 0.0)
{
    constexpr double pi = 3.14159265358979323846;
    double s[3][3]{};
    double t[3]{};
    for (std::size_t n = 0; n < y.size(); ++n) {
        const double ph = 2.0 * pi * freq * (startTime + static_cast<double>(n) / rate);
        const double basis[3] = {1.0, std::cos(ph), std::sin(ph)};
        for (int i = 0; i < 3; ++i) {
            t[i] += basis[i] * y[n];
            for (int j = 0; j < 3; ++j) {
                s[i][j] += basis[i] * basis[j];
            }
        }
    }
    double a[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            a[i][j] = s[i][j];
        }
        a[i][3] = t[i];
    }
    for (int c = 0; c < 3; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 3; ++r) {
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) {
                pivot = r;
            }
        }
        std::swap(a[c], a[pivot]);
        for (int r = 0; r < 3; ++r) {
            if (r != c) {
                const double f = a[r][c] / a[c][c];
                for (int k = c; k < 4; ++k) {
                    a[r][k] -= f * a[c][k];
                }
            }
        }
    }
    const double dc = a[0][3] / a[0][0];
    const double cosCoef = a[1][3] / a[1][1];
    const double sinCoef = a[2][3] / a[2][2];
    double sq = 0.0;
    for (std::size_t n = 0; n < y.size(); ++n) {
        const double ph = 2.0 * pi * freq * (startTime + static_cast<double>(n) / rate);
        const double e = y[n] - (dc + cosCoef * std::cos(ph) + sinCoef * std::sin(ph));
        sq += e * e;
    }
    // b cos + c sin = A sin(ph + theta) with A cos(theta) = c, A sin(theta) = b.
    return {std::hypot(cosCoef, sinCoef), std::atan2(cosCoef, sinCoef),
            std::sqrt(sq / static_cast<double>(y.size()))};
}

} // namespace ak4522::test
