/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Component that can generate various useful Yul functions.
 */

#include <libsolidity/codegen/InstructionsUtils.h>

#include <libsolidity/ast/Types.h>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::frontend;
using namespace solidity::evmasm;
using namespace std::string_literals;

std::string solidity::frontend::LoadCode(Type const& _type)
{
	if (_type.dataStoredIn(DataLocation::Storage))
		return "sload";
	else if (_type.dataStoredIn(DataLocation::TransientStorage))
		return "tload";
	else
		solAssert(false, "");
}

std::string solidity::frontend::StoreCode(Type const& _type)
{
	if (_type.dataStoredIn(DataLocation::Storage))
		return "sstore";
	else if (_type.dataStoredIn(DataLocation::TransientStorage))
		return "tstore";
	else
		solAssert(false, "");
}

Instruction solidity::frontend::LoadInstr(bool _transient)
{
	return _transient ? Instruction::TLOAD : Instruction::SLOAD;
}

Instruction solidity::frontend::StoreInstr(bool _transient)
{
	return _transient ? Instruction::TSTORE : Instruction::SSTORE;
}

Instruction solidity::frontend::LoadInstr(Type const& _type)
{
	if (_type.dataStoredIn(DataLocation::TransientStorage))
		return Instruction::TLOAD;
	else if (_type.dataStoredIn(DataLocation::Storage))
		return Instruction::SLOAD;
	else
		solAssert(false, "");
}

Instruction solidity::frontend::StoreInstr(Type const& _type)
{
	if (_type.dataStoredIn(DataLocation::TransientStorage))
		return Instruction::TSTORE;
	else if (_type.dataStoredIn(DataLocation::Storage))
		return Instruction::SSTORE;
	else
		solAssert(false, "");
}