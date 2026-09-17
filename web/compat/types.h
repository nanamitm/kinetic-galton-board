#pragma once
// Minimal value/container adapters used only by the WebAssembly physics build.
// Desktop builds continue to use Qt's actual types.
#include <vector>
#include <string>
#include <random>
using QString = std::string;
class QPointF {
    double px = 0, py = 0;
public:
    QPointF() = default;
    QPointF(double x, double y) : px(x), py(y) {}
    double x() const { return px; }
    double y() const { return py; }
    QPointF operator+(QPointF b) const { return {px+b.px, py+b.py}; }
    QPointF operator-(QPointF b) const { return {px-b.px, py-b.py}; }
    QPointF operator-() const { return {-px, -py}; }
    QPointF operator*(double k) const { return {px*k, py*k}; }
    QPointF operator/(double k) const { return {px/k, py/k}; }
};
template<class T> class QVector : public std::vector<T> {
public:
    using std::vector<T>::vector;
    bool isEmpty() const { return this->empty(); }
    const T &first() const { return this->front(); }
    const T &last() const { return this->back(); }
};
class QRandomGenerator {
    std::mt19937 engine{5489};
public:
    static QRandomGenerator *global() { static QRandomGenerator rng; return &rng; }
    double bounded(double upper) {
        return std::generate_canonical<double, 53>(engine) * upper;
    }
};
