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
/** @file Message.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <mutex>
#include <array>
#include <set>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <libdevcore/RLP.h>
#include <libdevcore/Guards.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/SHA3.h>
#include "Common.h"

namespace dev
{
namespace shh
{

class Message;

static const unsigned Undefined = (unsigned)-1;

struct FilterKey
{
	FilterKey() {}
	FilterKey(unsigned _tI, Secret const& _k): topicIndex(_tI), key(_k) {}
	unsigned topicIndex = Undefined;
	Secret key;
};

enum IncludeNonce
{
	WithoutNonce = 0,
	WithNonce = 1
};

class Envelope
{
	friend class Message;

public:
	Envelope() {}
	Envelope(RLP const& _m);

	operator bool() const { return !!m_expiry; }

	void streamRLP(RLPStream& _s, IncludeNonce _withNonce = WithNonce) const { _s.appendList(_withNonce ? 5 : 4) << m_expiry << m_ttl << m_topic << m_data; if (_withNonce) _s << m_nonce; }
	h256 sha3(IncludeNonce _withNonce = WithNonce) const { RLPStream s; streamRLP(s, _withNonce); return dev::sha3(s.out()); }

	unsigned sent() const { return m_expiry - m_ttl; }
	unsigned expiry() const { return m_expiry; }
	unsigned ttl() const { return m_ttl; }
	CollapsedTopic const& topic() const { return m_topic; }
	bytes const& data() const { return m_data; }

	Message open(FullTopic const& _ft, Secret const& _s = Secret()) const;

	unsigned workProved() const;
	void proveWork(unsigned _ms);

private:
	Envelope(unsigned _exp, unsigned _ttl, CollapsedTopic const& _topic): m_expiry(_exp), m_ttl(_ttl), m_topic(_topic) {}

	unsigned m_expiry = 0;
	unsigned m_ttl = 0;
	u256 m_nonce;

	CollapsedTopic m_topic;
	bytes m_data;
};

enum /*Message Flags*/
{
	ContainsSignature = 1
};

/// An (unencrypted) message, constructed from the combination of an Envelope, and, potentially,
/// a Secret key to decrypt the Message.
class Message
{
public:
	Message() {}
	Message(Envelope const& _e, FullTopic const& _ft, Secret const& _s = Secret());
	Message(bytes const& _payload): m_payload(_payload) {}
	Message(bytesConstRef _payload): m_payload(_payload.toBytes()) {}
	Message(bytes&& _payload) { std::swap(_payload, m_payload); }

	Public from() const { return m_from; }
	Public to() const { return m_to; }
	bytes const& payload() const { return m_payload; }

	void setFrom(Public _from) { m_from = _from; }
	void setTo(Public _to) { m_to = _to; }
	void setPayload(bytes const& _payload) { m_payload = _payload; }
	void setPayload(bytes&& _payload) { swap(m_payload, _payload); }

	operator bool() const { return !!m_payload.size() || m_from || m_to; }

	/// Turn this message into a ditributable Envelope.
	Envelope seal(Secret _from, FullTopic const& _topic, unsigned _workToProve = 50, unsigned _ttl = 50) const;
	// Overloads for skipping _from or specifying _to.
	Envelope seal(FullTopic const& _topic, unsigned _ttl = 50, unsigned _workToProve = 50) const { return seal(Secret(), _topic, _workToProve, _ttl); }
	Envelope sealTo(Public _to, FullTopic const& _topic, unsigned _workToProve = 50, unsigned _ttl = 50) { m_to = _to; return seal(Secret(), _topic, _workToProve, _ttl); }
	Envelope sealTo(Secret _from, Public _to, FullTopic const& _topic, unsigned _workToProve = 50, unsigned _ttl = 50) { m_to = _to; return seal(_from, _topic, _workToProve, _ttl); }

private:
	bool populate(bytes const& _data);

	Public m_from;
	Public m_to;
	bytes m_payload;
};

}
}
