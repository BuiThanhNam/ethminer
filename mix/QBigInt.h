/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file QBigInt.h
 * @author Yann yann@ethdev.com
 * @date 2015
 * Represent a big integer (u256, bigint) to be used in QML.
 */

#pragma once

#include "boost/variant.hpp"
#include "boost/variant/multivisitors.hpp"
#include <QObject>
#include <libdevcore/CommonJS.h>
#include <libdevcore/Common.h>

using namespace dev;

namespace dev
{
namespace mix
{

using BigIntVariant = boost::variant<dev::u256, dev::bigint>;

struct add: public boost::static_visitor<BigIntVariant>
{
	template<class T1, class T2>
	BigIntVariant operator()(T1 _value, T2 _otherValue) const { return _value + _otherValue; }
};

struct subtract: public boost::static_visitor<BigIntVariant>
{
	template<class T1, class T2>
	BigIntVariant operator()(T1 _value, T2 _otherValue) const { return _value - _otherValue; }
};

struct multiply: public boost::static_visitor<BigIntVariant>
{
	template<class T1, class T2>
	BigIntVariant operator()(T1 _value, T2 _otherValue) const { return _value * _otherValue; }
};

struct divide: public boost::static_visitor<BigIntVariant>
{
	template<class T1, class T2>
	BigIntVariant operator()(T1 _value, T2 _otherValue) const { return _value / _otherValue; }
};

class QBigInt: public QObject
{
	Q_OBJECT

public:
	QBigInt(QObject* _parent = 0): QObject(_parent), m_internalValue(dev::u256(0)) {}
	QBigInt(dev::u256 const& _value, QObject* _parent = 0): QObject(_parent), m_internalValue(_value) {}
	QBigInt(dev::bigint const& _value, QObject* _parent = 0): QObject(_parent), m_internalValue(_value) {}
	QBigInt(BigIntVariant const& _value, QObject* _parent = 0): QObject(_parent), m_internalValue(_value){}
	~QBigInt() {}

	/// @returns the current used big integer.
	BigIntVariant internalValue() { return m_internalValue; }
	/// @returns a string representation of the big integer used. Invokable from QML.
	Q_INVOKABLE QString value() const;
	/// Set the value of the BigInteger used. Will use u256 type. Invokable from QML.
	Q_INVOKABLE void setValue(QString const& _value) { m_internalValue = dev::jsToU256(_value.toStdString()); }
	/// Subtract by @a _value. Invokable from QML.
	Q_INVOKABLE QBigInt* subtract(QBigInt* const& _value) const;
	/// Add @a _value to the current big integer. Invokable from QML.
	Q_INVOKABLE QBigInt* add(QBigInt* const& _value) const;
	/// Multiply by @a _value. Invokable from QML.
	Q_INVOKABLE QBigInt* multiply(QBigInt* const& _value) const;
	/// divide by @a _value. Invokable from QML.
	Q_INVOKABLE QBigInt* divide(QBigInt* const& _value) const;

protected:
	 BigIntVariant m_internalValue;
};

}
}


