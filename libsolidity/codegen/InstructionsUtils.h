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
 * @author Christian <c@ethdev.com>
 * @date 2015
 * LValues for use in the expression compiler.
 */

#pragma once

#include <libevmasm/Instruction.h>

namespace solidity::frontend
{

class Type;

// [Amxx] TODO
std::string LoadCode(Type const& _type);
std::string StoreCode(Type const& _type);
solidity::evmasm::Instruction LoadInstr(bool _transient);
solidity::evmasm::Instruction StoreInstr(bool _transient);
solidity::evmasm::Instruction LoadInstr(Type const& _type);
solidity::evmasm::Instruction StoreInstr(Type const& _type);

}
