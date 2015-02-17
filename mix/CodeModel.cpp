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
/** @file CodeModel.cpp
 * @author Arkadiy Paronyan arkadiy@ethdev.com
 * @date 2014
 * Ethereum IDE client.
 */

#include <sstream>
#include <memory>
#include <QDebug>
#include <QApplication>
#include <QtQml>
#include <libsolidity/CompilerStack.h>
#include <libsolidity/SourceReferenceFormatter.h>
#include <libsolidity/InterfaceHandler.h>
#include <libevmcore/Instruction.h>
#include <libethcore/CommonJS.h>
#include "QContractDefinition.h"
#include "QFunctionDefinition.h"
#include "QVariableDeclaration.h"
#include "CodeHighlighter.h"
#include "FileIo.h"
#include "CodeModel.h"

using namespace dev::mix;

const std::set<std::string> c_predefinedContracts =
	{ "Config", "Coin", "CoinReg", "coin", "service", "owned", "mortal", "NameReg", "named", "std", "configUser" };

void BackgroundWorker::queueCodeChange(int _jobId)
{
	m_model->runCompilationJob(_jobId);
}

CompiledContract::CompiledContract(const dev::solidity::CompilerStack& _compiler, QString const& _contractName, QString const& _source):
	QObject(nullptr),
	m_sourceHash(qHash(_source))
{
	auto const& contractDefinition = _compiler.getContractDefinition(_contractName.toStdString());
	m_contract.reset(new QContractDefinition(&contractDefinition));
	QQmlEngine::setObjectOwnership(m_contract.get(), QQmlEngine::CppOwnership);
	m_bytes = _compiler.getBytecode(_contractName.toStdString());
	dev::solidity::InterfaceHandler interfaceHandler;
	m_contractInterface = QString::fromStdString(*interfaceHandler.getABIInterface(contractDefinition));
	if (m_contractInterface.isEmpty())
		m_contractInterface = "[]";
	if (contractDefinition.getLocation().sourceName.get())
		m_documentId = QString::fromStdString(*contractDefinition.getLocation().sourceName);
}

QString CompiledContract::codeHex() const
{
	return QString::fromStdString(toJS(m_bytes));
}

CodeModel::CodeModel(QObject* _parent):
	QObject(_parent),
	m_compiling(false),
	m_codeHighlighterSettings(new CodeHighlighterSettings()),
	m_backgroundWorker(this),
	m_backgroundJobId(0)
{
	m_backgroundThread.start();
	m_backgroundWorker.moveToThread(&m_backgroundThread);
	connect(this, &CodeModel::scheduleCompilationJob, &m_backgroundWorker, &BackgroundWorker::queueCodeChange, Qt::QueuedConnection);
	qRegisterMetaType<CompiledContract*>("CompiledContract*");
	qRegisterMetaType<QContractDefinition*>("QContractDefinition*");
	qRegisterMetaType<QFunctionDefinition*>("QFunctionDefinition*");
	qRegisterMetaType<QVariableDeclaration*>("QVariableDeclaration*");
	qmlRegisterType<QFunctionDefinition>("org.ethereum.qml", 1, 0, "QFunctionDefinition");
	qmlRegisterType<QVariableDeclaration>("org.ethereum.qml", 1, 0, "QVariableDeclaration");
}

CodeModel::~CodeModel()
{
	stop();
	disconnect(this);
	releaseContracts();
}

void CodeModel::stop()
{
	///@todo: cancel bg job
	m_backgroundThread.exit();
	m_backgroundThread.wait();
}

void CodeModel::reset(QVariantMap const& _documents)
{
	///@todo: cancel bg job
	Guard l(x_contractMap);
	releaseContracts();
	Guard pl(x_pendingContracts);
	m_pendingContracts.clear();

	for (QVariantMap::const_iterator d =  _documents.cbegin(); d != _documents.cend(); ++d)
		m_pendingContracts[d.key()] = d.value().toString();
	// launch the background thread
	m_compiling = true;
	emit stateChanged();
	emit scheduleCompilationJob(++m_backgroundJobId);
}

void CodeModel::registerCodeChange(QString const& _documentId, QString const& _code)
{
	{
		Guard l(x_contractMap);
		CompiledContract* contract = m_contractMap.value(_documentId);
		if (contract != nullptr && contract->m_sourceHash == qHash(_code))
			return;

		Guard pl(x_pendingContracts);
		m_pendingContracts[_documentId] = _code;
	}

	// launch the background thread
	m_compiling = true;
	emit stateChanged();
	emit scheduleCompilationJob(++m_backgroundJobId);
}

QVariantMap CodeModel::contracts() const
{
	QVariantMap result;
	Guard l(x_contractMap);
	for (ContractMap::const_iterator c = m_contractMap.cbegin(); c != m_contractMap.cend(); ++c)
		result.insert(c.key(), QVariant::fromValue(c.value()));
	return result;
}

CompiledContract* CodeModel::contractByDocumentId(QString _documentId) const
{
	Guard l(x_contractMap);
	for (ContractMap::const_iterator c = m_contractMap.cbegin(); c != m_contractMap.cend(); ++c)
		if (c.value()->m_documentId == _documentId)
			return c.value();
	return nullptr;
}

CompiledContract const& CodeModel::contract(QString _name) const
{
	Guard l(x_contractMap);
	CompiledContract* res = m_contractMap.value(_name);
	if (res == nullptr)
		BOOST_THROW_EXCEPTION(dev::Exception() << dev::errinfo_comment("Contract not found: " + _name.toStdString()));
	return *res;
}

void CodeModel::releaseContracts()
{
	for (ContractMap::iterator c = m_contractMap.begin(); c != m_contractMap.end(); ++c)
		c.value()->deleteLater();
	m_contractMap.clear();
}

void CodeModel::runCompilationJob(int _jobId)
{
	if (_jobId != m_backgroundJobId)
		return; //obsolete job

	ContractMap result;
	solidity::CompilerStack cs(true);
	try
	{
		cs.addSource("configUser", R"(contract configUser{function configAddr()constant returns(address a){ return 0xf025d81196b72fba60a1d4dddad12eeb8360d828;}})");
		{
			Guard l(x_pendingContracts);
			for (auto const& c: m_pendingContracts)
				cs.addSource(c.first.toStdString(), c.second.toStdString());
		}
		cs.compile(false);

		{
			Guard pl(x_pendingContracts);
			Guard l(x_contractMap);
			for (std::string n: cs.getContractNames())
			{
				if (c_predefinedContracts.count(n) != 0)
					continue;
				QString name = QString::fromStdString(n);
				auto sourceIter = m_pendingContracts.find(name);
				QString source = sourceIter != m_pendingContracts.end() ? sourceIter->second : QString();
				CompiledContract* contract = new CompiledContract(cs, name, source);
				QQmlEngine::setObjectOwnership(contract, QQmlEngine::CppOwnership);
				result[name] = contract;
				CompiledContract* prevContract = m_contractMap.value(name);
				if (prevContract != nullptr && prevContract->contractInterface() != result[name]->contractInterface())
					emit contractInterfaceChanged(name);
			}
			releaseContracts();
			m_contractMap.swap(result);
			emit codeChanged();
			emit compilationComplete();
		}
	}
	catch (dev::Exception const& _exception)
	{
		std::ostringstream error;
		solidity::SourceReferenceFormatter::printExceptionInformation(error, _exception, "Error", cs);
		solidity::Location const* location = boost::get_error_info<solidity::errinfo_sourceLocation>(_exception);
		QString message = QString::fromStdString(error.str());
		CompiledContract* contract = nullptr;
		if (location && location->sourceName.get() && (contract = contractByDocumentId(QString::fromStdString(*location->sourceName))))
			message = message.replace(QString::fromStdString(*location->sourceName), contract->contract()->name()); //substitute the location to match our contract names
		compilationError(message);
	}
	m_compiling = false;
	emit stateChanged();
}

bool CodeModel::hasContract() const
{
	Guard l(x_contractMap);
	return m_contractMap.size() != 0;
}

dev::bytes const& CodeModel::getStdContractCode(const QString& _contractName, const QString& _url)
{
	auto cached = m_compiledContracts.find(_contractName);
	if (cached != m_compiledContracts.end())
		return cached->second;

	FileIo fileIo;
	std::string source = fileIo.readFile(_url).toStdString();
	solidity::CompilerStack cs(false);
	cs.setSource(source);
	cs.compile(false);
	for (std::string const& name: cs.getContractNames())
	{
		dev::bytes code = cs.getBytecode(name);
		m_compiledContracts.insert(std::make_pair(QString::fromStdString(name), std::move(code)));
	}
	return m_compiledContracts.at(_contractName);
}

