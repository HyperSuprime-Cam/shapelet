// -*- LSST-C++ -*-

/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010, 2011 LSST Corporation.
 * 
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the LSST License Statement and 
 * the GNU General Public License along with this program.  If not, 
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */

#include "lsst/shapelet/ConversionMatrix.h"
#include "lsst/pex/exceptions.h"
#include "ndarray/eigen.h"

#include <boost/format.hpp>
#include <boost/math/special_functions/binomial.hpp>
#include <boost/math/special_functions/factorials.hpp>

#include "Eigen/LU"

#include <complex>
#include <vector>

namespace lsst { namespace shapelet {

namespace {

inline std::complex<double> iPow(int z) {
    switch (z % 4) {
    case 0:
        return std::complex<double>(1.0, 0.0);
    case 1:
        return std::complex<double>(0.0, 1.0);
    case 2:
        return std::complex<double>(-1.0, 0.0);
    case 3:
        return std::complex<double>(0.0, -1.0);
    };
    return 0.0;
}

class ConversionSingleton {
public:

    typedef std::vector<Eigen::MatrixXd> BlockVec;

    Eigen::MatrixXd const & getBlockH2L(int n) const {
        return _h2l[n];
    }

    Eigen::MatrixXd const & getBlockL2H(int n) const {
        return _l2h[n];
    }

    BlockVec const & getH2L() const { return _h2l; }

    BlockVec const & getL2H() const { return _l2h; }

    void ensure(int order) {
        if (order > _max_order) {
            _h2l.reserve(order + 1);
            _l2h.reserve(order + 1);
            for (int i = _max_order + 1; i <= order; ++i) {
                _h2l.push_back(makeBlockH2L(i));
                _l2h.push_back(makeBlockL2H(i, _h2l.back()));
                ++_max_order;
            }
        }
    }

    static Eigen::MatrixXd makeBlockH2L(int n) {
        Eigen::MatrixXcd c = Eigen::MatrixXcd::Zero(n + 1, n + 1);
        for (int m = -n, i = 0; m <= n; m += 2, ++i) {
            int const p = (n + m) / 2;
            int const q = (n - m) / 2;
            double const p_factorial = boost::math::unchecked_factorial<double>(p);
            double const q_factorial = boost::math::unchecked_factorial<double>(q);
            std::complex<double> const v1 = std::pow(std::complex<double>(0.0, -1.0), m) 
                * std::pow(2.0, -0.5 * n)
                / std::sqrt(p_factorial * q_factorial);
            for (int x = 0, y = n; x <= n; ++x, --y) {
                double const x_factorial = boost::math::unchecked_factorial<double>(x);
                double const y_factorial = boost::math::unchecked_factorial<double>(y);
                std::complex<double> const v2 = v1 * std::sqrt(x_factorial * y_factorial);
                for (int r = 0; r <= p; ++r) {
                    for (int s = 0; s <= q; ++s) {
                        if (r + s == x) {
                            int const m_p = r - s;
                            c(i, x) += v2 * std::pow(std::complex<double>(0.0, 1.0), m_p)
                                * boost::math::binomial_coefficient<double>(p, r)
                                * boost::math::binomial_coefficient<double>(q, s);
                        }
                    }
                }
            }
        }

        Eigen::MatrixXd b = Eigen::MatrixXd::Zero(n + 1, n + 1);
        for (int x = 0, y = n; x <= n; ++x, --y) {
            for (int p = n, q = 0; q <= p; --p, ++q) {
                b(2 * q, x) = c(q, x).real();
                if (q < p) {
                    b(2 * q + 1, x) = -c(q, x).imag();
                }
            }
        }
        return b;
    }

    static Eigen::MatrixXd makeBlockL2H(int n, Eigen::MatrixXd const & h2l) {
        Eigen::MatrixXd l2h = h2l.inverse();
        return l2h;
    }

    static ConversionSingleton & get() {
        static ConversionSingleton instance;
        return instance;
    }

private:
    ConversionSingleton() : _max_order(-1) {}

    // No copying
    ConversionSingleton ( const ConversionSingleton & ) = delete;
    ConversionSingleton & operator= ( const ConversionSingleton & ) = delete;

    // No moving
    ConversionSingleton ( ConversionSingleton && ) = delete;
    ConversionSingleton & operator= ( ConversionSingleton && ) = delete;

    int _max_order;
    BlockVec _h2l;
    BlockVec _l2h;
};

} // anonymous

Eigen::MatrixXd ConversionMatrix::getBlock(int n) const { 
    if (_input == _output) return Eigen::MatrixXd::Identity(n + 1, n + 1);
    if (_input == HERMITE)
        return ConversionSingleton::get().getBlockH2L(n);
    else
        return ConversionSingleton::get().getBlockL2H(n);
}

Eigen::MatrixXd ConversionMatrix::buildDenseMatrix() const { 
    int const size = computeSize(_order);
    if (_input == _output) return Eigen::MatrixXd::Identity(size, size);
    Eigen::MatrixXd r = Eigen::MatrixXd::Zero(size, size);
    if (_input == HERMITE) {
        for (int n = 0, offset = 0; n <= _order; offset += ++n) {
            r.block(offset, offset, n + 1, n + 1) = ConversionSingleton::get().getBlockH2L(n);
        }
    } else {
        for (int n = 0, offset = 0; n <= _order; offset += ++n) {
            r.block(offset, offset, n + 1, n + 1) = ConversionSingleton::get().getBlockL2H(n);
        }
    }
    return r;
}

void ConversionMatrix::multiplyOnLeft(
    ndarray::Array<double,1> const & array) const {
    if (array.getSize<0>() != computeSize(_order)) {
        throw LSST_EXCEPT(
            lsst::pex::exceptions::LengthError,
            (boost::format(
                "Array for multiplyOnLeft has incorrect size (%n, should be %n)."
            ) % array.getSize<0>() % computeSize(_order)).str()
        );
    }
    if (_input == _output) return;
    ConversionSingleton::BlockVec::const_iterator i;
    if (_input == HERMITE) {
        i = ConversionSingleton::get().getH2L().begin();
    } else {
        i = ConversionSingleton::get().getL2H().begin();
    }
    auto vector = ndarray::asEigenMatrix(array);
    for (int offset = 0; offset < vector.size(); ++i) {
        vector.segment(offset, i->rows()).transpose() *= i->transpose();
        offset += i->rows();
    }
}

void ConversionMatrix::multiplyOnRight(
    ndarray::Array<double,1> const & array) const {
    if (array.getSize<0>() != computeSize(_order)) {
        throw LSST_EXCEPT(
            lsst::pex::exceptions::LengthError,
            (boost::format(
                "Array for multiplyOnRight has incorrect size (%n, should be %n)."
            ) % array.getSize<0>() % computeSize(_order)).str()
        );
    }
    if (_input == _output) return;
    ConversionSingleton::BlockVec::const_iterator i;
    if (_input == HERMITE) {
        i = ConversionSingleton::get().getH2L().begin();
    } else {
        i = ConversionSingleton::get().getL2H().begin();
    }
    auto vector = ndarray::asEigenMatrix(array);
    for (int offset = 0; offset < vector.size(); ++i) {
        vector.segment(offset, i->rows()).transpose() *= (*i);
        offset += i->rows();
    }
}

ConversionMatrix::ConversionMatrix(BasisTypeEnum input, BasisTypeEnum output, int order) :
    _order(order), _input(input), _output(output)
{
    ConversionSingleton::get().ensure(_order);
}

void ConversionMatrix::convertCoefficientVector(
    ndarray::Array<double,1> const & array,
    BasisTypeEnum input,
    BasisTypeEnum output,
    int order
) {
    if (input == output) return;
    ConversionMatrix m(input, output, order);
    m.multiplyOnLeft(array);
}

void ConversionMatrix::convertOperationVector(
    ndarray::Array<double,1> const & array,
    BasisTypeEnum input,
    BasisTypeEnum output,
    int order
) {
    if (input == output) return;
    ConversionMatrix m(output, input, order);
    m.multiplyOnRight(array);
}

}} // namespace lsst::shapelet
