#include "window.h"

#include <cmath>

namespace aa::dsp {

Window makeWindow(WindowType type, std::size_t n) {
    Window w;
    w.coeff.resize(n);
    const double twoPiOverN = 2.0 * M_PI / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = twoPiOverN * static_cast<double>(i);
        double v;
        switch (type) {
            case WindowType::Hann:
                v = 0.5 * (1.0 - std::cos(t));
                break;
            case WindowType::FlatTop:
                v = 1.0 - 1.93 * std::cos(t) + 1.29 * std::cos(2 * t) -
                    0.388 * std::cos(3 * t) + 0.028 * std::cos(4 * t);
                break;
            case WindowType::Rectangular:
            default:
                v = 1.0;
                break;
        }
        w.coeff[i] = static_cast<float>(v);
        w.s1 += v;
        w.s2 += v * v;
    }
    return w;
}

}  // namespace aa::dsp
