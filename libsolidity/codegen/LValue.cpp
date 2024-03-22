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

#include <libsolidity/codegen/LValue.h>

#include <libsolidity/ast/AST.h>
#include <libsolidity/ast/Types.h>
#include <libsolidity/codegen/CompilerUtils.h>
#include <libevmasm/Instruction.h>
#include <libsolidity/codegen/InstructionsUtils.h>

#include <libsolutil/StackTooDeepString.h>

using namespace solidity;
using namespace solidity::evmasm;
using namespace solidity::frontend;
using namespace solidity::langutil;
using namespace solidity::util;

StackVariable::StackVariable(CompilerContext& _compilerContext, VariableDeclaration const& _declaration):
	LValue(_compilerContext, _declaration.type()),
	m_baseStackOffset(m_context.baseStackOffsetOfVariable(_declaration)),
	m_size(m_dataType->sizeOnStack())
{
}

void StackVariable::retrieveValue(SourceLocation const& _location, bool) const
{
	unsigned stackPos = m_context.baseToCurrentStackOffset(m_baseStackOffset);
	if (stackPos + 1 > 16) //@todo correct this by fetching earlier or moving to memory
		BOOST_THROW_EXCEPTION(
			StackTooDeepError() <<
			errinfo_sourceLocation(_location) <<
			util::errinfo_comment(util::stackTooDeepString)
		);
	solAssert(stackPos + 1 >= m_size, "Size and stack pos mismatch.");
	for (unsigned i = 0; i < m_size; ++i)
		m_context << dupInstruction(stackPos + 1);
}

void StackVariable::storeValue(Type const&, SourceLocation const& _location, bool _move) const
{
	unsigned stackDiff = m_context.baseToCurrentStackOffset(m_baseStackOffset) - m_size + 1;
	if (stackDiff > 16)
		BOOST_THROW_EXCEPTION(
			StackTooDeepError() <<
			errinfo_sourceLocation(_location) <<
			util::errinfo_comment(util::stackTooDeepString)
		);
	else if (stackDiff > 0)
		for (unsigned i = 0; i < m_size; ++i)
			m_context << swapInstruction(stackDiff) << Instruction::POP;
	if (!_move)
		retrieveValue(_location);
}

void StackVariable::setToZero(SourceLocation const& _location, bool) const
{
	CompilerUtils(m_context).pushZeroValue(*m_dataType);
	storeValue(*m_dataType, _location, true);
}

MemoryItem::MemoryItem(CompilerContext& _compilerContext, Type const& _type, bool _padded):
	LValue(_compilerContext, &_type),
	m_padded(_padded)
{
}

void MemoryItem::retrieveValue(SourceLocation const&, bool _remove) const
{
	if (m_dataType->isValueType())
	{
		if (!_remove)
			m_context << Instruction::DUP1;
		CompilerUtils(m_context).loadFromMemoryDynamic(*m_dataType, false, m_padded, false);
	}
	else
		m_context << Instruction::MLOAD;
}

void MemoryItem::storeValue(Type const& _sourceType, SourceLocation const&, bool _move) const
{
	CompilerUtils utils(m_context);
	if (m_dataType->isValueType())
	{
		solAssert(_sourceType.isValueType(), "");
		utils.moveIntoStack(_sourceType.sizeOnStack());
		utils.convertType(_sourceType, *m_dataType, true);
		if (!_move)
		{
			utils.moveToStackTop(m_dataType->sizeOnStack());
			utils.copyToStackTop(1 + m_dataType->sizeOnStack(), m_dataType->sizeOnStack());
		}
		if (!m_padded)
		{
			solAssert(m_dataType->calldataEncodedSize(false) == 1, "Invalid non-padded type.");
			solAssert(m_dataType->category() != Type::Category::UserDefinedValueType, "");
			if (m_dataType->category() == Type::Category::FixedBytes)
				m_context << u256(0) << Instruction::BYTE;
			m_context << Instruction::SWAP1 << Instruction::MSTORE8;
		}
		else
		{
			utils.storeInMemoryDynamic(*m_dataType, m_padded);
			m_context << Instruction::POP;
		}
	}
	else
	{
		solUnimplementedAssert(_sourceType == *m_dataType, "Conversion not implemented for assignment to memory.");

		solAssert(m_dataType->sizeOnStack() == 1, "");
		if (!_move)
			m_context << Instruction::DUP2 << Instruction::SWAP1;
		// stack: [value] value lvalue
		// only store the reference
		m_context << Instruction::MSTORE;
	}
}

void MemoryItem::setToZero(SourceLocation const&, bool _removeReference) const
{
	CompilerUtils utils(m_context);
	solAssert(_removeReference, "");
	utils.pushZeroValue(*m_dataType);
	utils.storeInMemoryDynamic(*m_dataType, m_padded);
	m_context << Instruction::POP;
}


ImmutableItem::ImmutableItem(CompilerContext& _compilerContext, VariableDeclaration const& _variable):
	LValue(_compilerContext, _variable.type()), m_variable(_variable)
{
	solAssert(_variable.immutable(), "");
}

void ImmutableItem::retrieveValue(SourceLocation const&, bool) const
{
	solUnimplementedAssert(m_dataType->isValueType());

	if (m_context.runtimeContext())
		CompilerUtils(m_context).loadFromMemory(
			static_cast<unsigned>(m_context.immutableMemoryOffset(m_variable)),
			*m_dataType,
			false,
			true
		);
	else
		for (auto&& slotName: m_context.immutableVariableSlotNames(m_variable))
			m_context.appendImmutable(slotName);
}

void ImmutableItem::storeValue(Type const& _sourceType, SourceLocation const&, bool _move) const
{
	CompilerUtils utils(m_context);
	solUnimplementedAssert(m_dataType->isValueType());
	solAssert(_sourceType.isValueType(), "");

	utils.convertType(_sourceType, *m_dataType, true);
	m_context << m_context.immutableMemoryOffset(m_variable);
	if (_move)
		utils.moveIntoStack(m_dataType->sizeOnStack());
	else
		utils.copyToStackTop(m_dataType->sizeOnStack() + 1, m_dataType->sizeOnStack());
	utils.storeInMemoryDynamic(*m_dataType);
	m_context << Instruction::POP;
}

void ImmutableItem::setToZero(SourceLocation const&, bool _removeReference) const
{
	CompilerUtils utils(m_context);
	solUnimplementedAssert(m_dataType->isValueType());
	solAssert(_removeReference);

	m_context << m_context.immutableMemoryOffset(m_variable);
	utils.pushZeroValue(*m_dataType);
	utils.storeInMemoryDynamic(*m_dataType);
	m_context << Instruction::POP;
}

StorageItem::StorageItem(CompilerContext& _compilerContext, VariableDeclaration const& _declaration):
	StorageItem(_compilerContext, *_declaration.type(), _declaration.transient())
{
	// std::cout << "[[[ StorageItem::StorageItem #1 ]]]" << std::endl;
	// switch (_declaration.referenceLocation())
	// {
	// 	case VariableDeclaration::Location::Unspecified: std::cout << "VariableDeclaration::Location::Unspecified" << std::endl; break;
	// 	case VariableDeclaration::Location::Storage: std::cout << "VariableDeclaration::Location::Storage" << std::endl; break;
	// 	case VariableDeclaration::Location::TransientStorage: std::cout << "VariableDeclaration::Location::TransientStorage" << std::endl; break;
	// 	case VariableDeclaration::Location::Memory: std::cout << "VariableDeclaration::Location::Memory" << std::endl; break;
	// 	case VariableDeclaration::Location::CallData: std::cout << "VariableDeclaration::Location::CallData" << std::endl; break;
	// }
	// switch (_declaration.mutability())
	// {
	// 	case VariableDeclaration::Mutability::Mutable: std::cout << "VariableDeclaration::Mutability::Mutable" << std::endl; break;
	// 	case VariableDeclaration::Mutability::Transient: std::cout << "VariableDeclaration::Mutability::Transient" << std::endl; break;
	// 	case VariableDeclaration::Mutability::Immutable: std::cout << "VariableDeclaration::Mutability::Immutable" << std::endl; break;
	// 	case VariableDeclaration::Mutability::Constant: std::cout << "VariableDeclaration::Mutability::Constant" << std::endl; break;
	// }
	solAssert(!_declaration.immutable(), "");
	auto const& location = m_context.storageLocationOfVariable(_declaration);
	m_context << location.first << u256(location.second);
}

StorageItem::StorageItem(CompilerContext& _compilerContext, Type const& _type, bool _transient):
	LValue(_compilerContext, &_type),
	m_transient(_transient)
{
	// std::cout << "[[[ StorageItem::StorageItem #2 ]]]" << std::endl;
	// switch(_type.category())
	// {
	// 	case Type::Category::Address: std::cout << "Type::Category::Address" << std::endl; break;
	// 	case Type::Category::Integer: std::cout << "Type::Category::Integer" << std::endl; break;
	// 	case Type::Category::RationalNumber: std::cout << "Type::Category::RationalNumber" << std::endl; break;
	// 	case Type::Category::StringLiteral: std::cout << "Type::Category::StringLiteral" << std::endl; break;
	// 	case Type::Category::Bool: std::cout << "Type::Category::Bool" << std::endl; break;
	// 	case Type::Category::FixedPoint: std::cout << "Type::Category::FixedPoint" << std::endl; break;
	// 	case Type::Category::Array: std::cout << "Type::Category::Array" << std::endl; break;
	// 	case Type::Category::ArraySlice: std::cout << "Type::Category::ArraySlice" << std::endl; break;
	// 	case Type::Category::FixedBytes: std::cout << "Type::Category::FixedBytes" << std::endl; break;
	// 	case Type::Category::Contract: std::cout << "Type::Category::Contract" << std::endl; break;
	// 	case Type::Category::Struct: std::cout << "Type::Category::Struct" << std::endl; break;
	// 	case Type::Category::Function: std::cout << "Type::Category::Function" << std::endl; break;
	// 	case Type::Category::Enum: std::cout << "Type::Category::Enum" << std::endl; break;
	// 	case Type::Category::UserDefinedValueType: std::cout << "Type::Category::UserDefinedValueType" << std::endl; break;
	// 	case Type::Category::Tuple: std::cout << "Type::Category::Tuple" << std::endl; break;
	// 	case Type::Category::Mapping: std::cout << "Type::Category::Mapping" << std::endl; break;
	// 	case Type::Category::TypeType: std::cout << "Type::Category::TypeType" << std::endl; break;
	// 	case Type::Category::Modifier: std::cout << "Type::Category::Modifier" << std::endl; break;
	// 	case Type::Category::Magic: std::cout << "Type::Category::Magic" << std::endl; break;
	// 	case Type::Category::Module: std::cout << "Type::Category::Module" << std::endl; break;
	// 	case Type::Category::InaccessibleDynamic: std::cout << "Type::Category::InaccessibleDynamic" << std::endl; break;
	// }
	// std::cout << "Transient: " << _transient << std::endl;
	if (m_dataType->isValueType())
	{
		if (m_dataType->category() != Type::Category::Function)
			solAssert(m_dataType->storageSize() == m_dataType->sizeOnStack(), "");
		solAssert(m_dataType->storageSize() == 1, "Invalid storage size.");
	}
}

void StorageItem::retrieveValue(SourceLocation const&, bool _remove) const
{
	// stack: storage_key storage_offset
	if (!m_dataType->isValueType())
	{
		solAssert(m_dataType->sizeOnStack() == 1, "Invalid storage ref size.");
		if (_remove)
			m_context << Instruction::POP; // remove byte offset
		else
			m_context << Instruction::DUP2;
		return;
	}
	if (!_remove)
		CompilerUtils(m_context).copyToStackTop(sizeOnStack(), sizeOnStack());
	if (m_dataType->storageBytes() == 32)
		m_context << Instruction::POP << LoadInstr(m_transient);
	else
	{
		Type const* type = m_dataType;
		if (type->category() == Type::Category::UserDefinedValueType)
			type = type->encodingType();
		bool cleaned = false;
		m_context
			<< Instruction::SWAP1 << LoadInstr(m_transient) << Instruction::SWAP1
			<< u256(0x100) << Instruction::EXP << Instruction::SWAP1 << Instruction::DIV;
		if (type->category() == Type::Category::FixedPoint)
			// implementation should be very similar to the integer case.
			solUnimplemented("Not yet implemented - FixedPointType.");
		else if (FunctionType const* fun = dynamic_cast<decltype(fun)>(type))
		{
			if (fun->kind() == FunctionType::Kind::External)
			{
				CompilerUtils(m_context).splitExternalFunctionType(false);
				cleaned = true;
			}
			else if (fun->kind() == FunctionType::Kind::Internal)
			{
				m_context << Instruction::DUP1 << Instruction::ISZERO;
				CompilerUtils(m_context).pushZeroValue(*fun);
				m_context << Instruction::MUL << Instruction::OR;
			}
		}
		else if (type->leftAligned())
		{
			CompilerUtils(m_context).leftShiftNumberOnStack(256 - 8 * type->storageBytes());
			cleaned = true;
		}
		else if (
			type->category() == Type::Category::Integer &&
			dynamic_cast<IntegerType const&>(*type).isSigned()
		)
		{
			m_context << u256(type->storageBytes() - 1) << Instruction::SIGNEXTEND;
			cleaned = true;
		}

		if (!cleaned)
		{
			solAssert(type->sizeOnStack() == 1, "");
			m_context << ((u256(0x1) << (8 * type->storageBytes())) - 1) << Instruction::AND;
		}
	}
}

void StorageItem::storeValue(Type const& _sourceType, SourceLocation const& _location, bool _move) const
{
	CompilerUtils utils(m_context);
	solAssert(m_dataType, "");
	// std::cout << "StorageItem::storeValue: m_dataType->richIdentifier() " << m_dataType->richIdentifier() << std::endl;

	// stack: value storage_key storage_offset
	if (m_dataType->isValueType())
	{
		solAssert(m_dataType->storageBytes() <= 32, "Invalid storage bytes size.");
		solAssert(m_dataType->storageBytes() > 0, "Invalid storage bytes size.");
		if (m_dataType->storageBytes() == 32)
		{
			solAssert(m_dataType->sizeOnStack() == 1, "Invalid stack size.");
			// offset should be zero
			m_context << Instruction::POP;
			if (!_move)
				m_context << Instruction::DUP2 << Instruction::SWAP1;

			m_context << Instruction::SWAP1;
			utils.convertType(_sourceType, *m_dataType, true);
			m_context << Instruction::SWAP1;

			m_context << StoreInstr(m_transient);
		}
		else
		{
			// OR the value into the other values in the storage slot
			m_context << u256(0x100) << Instruction::EXP;
			// stack: value storage_ref multiplier
			// fetch old value
			m_context << Instruction::DUP2 << LoadInstr(m_transient);
			// stack: value storage_ref multiplier old_full_value
			// clear bytes in old value
			m_context
				<< Instruction::DUP2 << ((u256(1) << (8 * m_dataType->storageBytes())) - 1)
				<< Instruction::MUL;
			m_context << Instruction::NOT << Instruction::AND << Instruction::SWAP1;
			// stack: value storage_ref cleared_value multiplier
			utils.copyToStackTop(3 + m_dataType->sizeOnStack(), m_dataType->sizeOnStack());
			// stack: value storage_ref cleared_value multiplier value
			if (auto const* fun = dynamic_cast<FunctionType const*>(m_dataType))
			{
				solAssert(
					_sourceType.isImplicitlyConvertibleTo(*m_dataType),
					"function item stored but target is not implicitly convertible to source"
				);
				solAssert(!fun->hasBoundFirstArgument(), "");
				if (fun->kind() == FunctionType::Kind::External)
				{
					solAssert(fun->sizeOnStack() == 2, "");
					// Combine the two-item function type into a single stack slot.
					utils.combineExternalFunctionType(false);
				}
				else
				{
					solAssert(fun->sizeOnStack() == 1, "");
					m_context <<
						((u256(1) << (8 * m_dataType->storageBytes())) - 1) <<
						Instruction::AND;
				}
			}
			else if (m_dataType->leftAligned())
			{
				solAssert(_sourceType.category() == Type::Category::FixedBytes || (
					_sourceType.encodingType() &&
					_sourceType.encodingType()->category() == Type::Category::FixedBytes
				), "source not fixed bytes");
				CompilerUtils(m_context).rightShiftNumberOnStack(256 - 8 * m_dataType->storageBytes());
			}
			else
			{
				solAssert(m_dataType->sizeOnStack() == 1, "Invalid stack size for opaque type.");
				// remove the higher order bits
				utils.convertType(_sourceType, *m_dataType, true, true);
			}
			m_context  << Instruction::MUL << Instruction::OR;
			// stack: value storage_ref updated_value
			m_context << Instruction::SWAP1 << StoreInstr(m_transient);
			if (_move)
				utils.popStackElement(*m_dataType);
		}
	}
	else
	{
		solAssert(
			_sourceType.category() == m_dataType->category(),
			"Wrong type conversation for assignment."
		);
		if (m_dataType->category() == Type::Category::Array)
		{
			m_context << Instruction::POP; // remove byte offset
			ArrayUtils(m_context).copyArrayToStorage(
				dynamic_cast<ArrayType const&>(*m_dataType),
				dynamic_cast<ArrayType const&>(_sourceType)
			);
			if (_move)
				m_context << Instruction::POP;
		}
		else if (m_dataType->category() == Type::Category::Struct)
		{
			// stack layout: source_ref target_ref target_offset
			// note that we have structs, so offset should be zero and are ignored
			m_context << Instruction::POP;
			auto const& structType = dynamic_cast<StructType const&>(*m_dataType);
			auto const& sourceType = dynamic_cast<StructType const&>(_sourceType);
			solAssert(
				structType.structDefinition() == sourceType.structDefinition(),
				"Struct assignment with conversion."
			);
			solAssert(!structType.containsNestedMapping(), "");
			if (sourceType.location() == DataLocation::CallData)
			{
				solAssert(sourceType.sizeOnStack() == 1, "");
				solAssert(structType.sizeOnStack() == 1, "");
				m_context << Instruction::DUP2 << Instruction::DUP2;
				m_context.callYulFunction(
					m_context.utilFunctions().updateStorageValueFunction(
						sourceType,
						ArrayType(m_transient ? DataLocation::TransientStorage : DataLocation::Storage, m_dataType),
						0
					),
					2,
					0
				);
			}
			else
			{
				for (auto const& member: structType.members(nullptr))
				{
					// assign each member that can live outside of storage
					Type const* memberType = member.type;
					solAssert(memberType->nameable(), "");
					Type const* sourceMemberType = sourceType.memberType(member.name);
					if (sourceType.dataStoredInAnyOf({ DataLocation::Storage, DataLocation::TransientStorage }))
					{
						// stack layout: source_ref target_ref
						std::pair<u256, unsigned> const& offsets = sourceType.storageOffsetsOfMember(member.name);
						m_context << offsets.first << Instruction::DUP3 << Instruction::ADD;
						m_context << u256(offsets.second);
						// stack: source_ref target_ref source_member_ref source_member_off
						StorageItem(m_context, *sourceMemberType, sourceType.dataStoredIn({ DataLocation::TransientStorage })).retrieveValue(_location, true);
						// stack: source_ref target_ref source_value...
					}
					else
					{
						solAssert(sourceType.location() == DataLocation::Memory, "");
						// stack layout: source_ref target_ref
						m_context << sourceType.memoryOffsetOfMember(member.name);
						m_context << Instruction::DUP3 << Instruction::ADD;
						MemoryItem(m_context, *sourceMemberType).retrieveValue(_location, true);
						// stack layout: source_ref target_ref source_value...
					}
					unsigned stackSize = sourceMemberType->sizeOnStack();
					std::pair<u256, unsigned> const& offsets = structType.storageOffsetsOfMember(member.name);
					m_context << dupInstruction(1 + stackSize) << offsets.first << Instruction::ADD;
					m_context << u256(offsets.second);
					// stack: source_ref target_ref target_off source_value... target_member_ref target_member_byte_off
					StorageItem(m_context, *memberType, m_transient).storeValue(*sourceMemberType, _location, true);
				}
			}
			// stack layout: source_ref target_ref
			solAssert(sourceType.sizeOnStack() == 1, "Unexpected source size.");
			if (_move)
				utils.popStackSlots(2);
			else
				m_context << Instruction::SWAP1 << Instruction::POP;
		}
		else
			BOOST_THROW_EXCEPTION(
				InternalCompilerError()
					<< errinfo_sourceLocation(_location)
					<< util::errinfo_comment("Invalid non-value type for assignment."));
	}
}

void StorageItem::setToZero(SourceLocation const&, bool _removeReference) const
{
	if (m_dataType->category() == Type::Category::Array)
	{
		if (!_removeReference)
			CompilerUtils(m_context).copyToStackTop(sizeOnStack(), sizeOnStack());
		ArrayUtils(m_context).clearArray(dynamic_cast<ArrayType const&>(*m_dataType));
	}
	else if (m_dataType->category() == Type::Category::Struct)
	{
		// stack layout: storage_key storage_offset
		// @todo this can be improved: use StorageItem for non-value types, and just store 0 in
		// all slots that contain value types later.
		auto const& structType = dynamic_cast<StructType const&>(*m_dataType);
		for (auto const& member: structType.members(nullptr))
		{
			// zero each member that is not a mapping
			Type const* memberType = member.type;
			if (memberType->category() == Type::Category::Mapping)
				continue;
			std::pair<u256, unsigned> const& offsets = structType.storageOffsetsOfMember(member.name);
			m_context
				<< offsets.first << Instruction::DUP3 << Instruction::ADD
				<< u256(offsets.second);
			StorageItem(m_context, *memberType, m_transient).setToZero();
		}
		if (_removeReference)
			m_context << Instruction::POP << Instruction::POP;
	}
	else
	{
		solAssert(m_dataType->isValueType(), "Clearing of unsupported type requested: " + m_dataType->toString());
		if (!_removeReference)
			CompilerUtils(m_context).copyToStackTop(sizeOnStack(), sizeOnStack());
		if (m_dataType->storageBytes() == 32)
		{
			// offset should be zero
			m_context
				<< Instruction::POP << u256(0)
				<< Instruction::SWAP1 << StoreInstr(m_transient);
		}
		else
		{
			m_context << u256(0x100) << Instruction::EXP;
			// stack: storage_ref multiplier
			// fetch old value
			m_context << Instruction::DUP2 << LoadInstr(m_transient);
			// stack: storage_ref multiplier old_full_value
			// clear bytes in old value
			m_context
				<< Instruction::SWAP1 << ((u256(1) << (8 * m_dataType->storageBytes())) - 1)
				<< Instruction::MUL;
			m_context << Instruction::NOT << Instruction::AND;
			// stack: storage_ref cleared_value
			m_context << Instruction::SWAP1 << StoreInstr(m_transient);
		}
	}
}

StorageByteArrayElement::StorageByteArrayElement(CompilerContext& _compilerContext):
	LValue(_compilerContext, TypeProvider::byte()),
	m_transient(false) // [Amxx] TODO: set in construction
{
}

void StorageByteArrayElement::retrieveValue(SourceLocation const&, bool _remove) const
{
	// stack: ref byte_number
	if (_remove)
		m_context << Instruction::SWAP1 << LoadInstr(m_transient)
			<< Instruction::SWAP1 << Instruction::BYTE;
	else
		m_context << Instruction::DUP2 << LoadInstr(m_transient)
			<< Instruction::DUP2 << Instruction::BYTE;
	m_context << (u256(1) << (256 - 8)) << Instruction::MUL;
}

void StorageByteArrayElement::storeValue(Type const&, SourceLocation const&, bool _move) const
{
	// stack: value ref byte_number
	m_context << u256(31) << Instruction::SUB << u256(0x100) << Instruction::EXP;
	// stack: value ref (1<<(8*(31-byte_number)))
	m_context << Instruction::DUP2 << LoadInstr(m_transient);
	// stack: value ref (1<<(8*(31-byte_number))) old_full_value
	// clear byte in old value
	m_context << Instruction::DUP2 << u256(0xff) << Instruction::MUL
		<< Instruction::NOT << Instruction::AND;
	// stack: value ref (1<<(32-byte_number)) old_full_value_with_cleared_byte
	m_context << Instruction::SWAP1;
	m_context << (u256(1) << (256 - 8)) << Instruction::DUP5 << Instruction::DIV
		<< Instruction::MUL << Instruction::OR;
	// stack: value ref new_full_value
	m_context << Instruction::SWAP1 << StoreInstr(m_transient);
	if (_move)
		m_context << Instruction::POP;
}

void StorageByteArrayElement::setToZero(SourceLocation const&, bool _removeReference) const
{
	// stack: ref byte_number
	solAssert(_removeReference, "");
	m_context << u256(31) << Instruction::SUB << u256(0x100) << Instruction::EXP;
	// stack: ref (1<<(8*(31-byte_number)))
	m_context << Instruction::DUP2 << LoadInstr(m_transient);
	// stack: ref (1<<(8*(31-byte_number))) old_full_value
	// clear byte in old value
	m_context << Instruction::SWAP1 << u256(0xff) << Instruction::MUL;
	m_context << Instruction::NOT << Instruction::AND;
	// stack: ref old_full_value_with_cleared_byte
	m_context << Instruction::SWAP1 << StoreInstr(m_transient);
}

TupleObject::TupleObject(
	CompilerContext& _compilerContext,
	std::vector<std::unique_ptr<LValue>>&& _lvalues
):
	LValue(_compilerContext), m_lvalues(std::move(_lvalues))
{
}

unsigned TupleObject::sizeOnStack() const
{
	unsigned size = 0;
	for (auto const& lv: m_lvalues)
		if (lv)
			size += lv->sizeOnStack();
	return size;
}

void TupleObject::retrieveValue(SourceLocation const&, bool) const
{
	solAssert(false, "Tried to retrieve value of tuple.");
}

void TupleObject::storeValue(Type const& _sourceType, SourceLocation const& _location, bool) const
{
	// values are below the lvalue references
	unsigned valuePos = sizeOnStack();
	TypePointers const& valueTypes = dynamic_cast<TupleType const&>(_sourceType).components();
	solAssert(valueTypes.size() == m_lvalues.size(), "");
	// valuePos .... refPos ...
	// We will assign from right to left to optimize stack layout.
	for (size_t i = 0; i < m_lvalues.size(); ++i)
	{
		std::unique_ptr<LValue> const& lvalue = m_lvalues[m_lvalues.size() - i - 1];
		Type const* valType = valueTypes[valueTypes.size() - i - 1];
		unsigned stackHeight = m_context.stackHeight();
		solAssert(!valType == !lvalue, "");
		if (!lvalue)
			continue;
		valuePos += valType->sizeOnStack();
		// copy value to top
		CompilerUtils(m_context).copyToStackTop(valuePos, valType->sizeOnStack());
		// move lvalue ref above value
		CompilerUtils(m_context).moveToStackTop(valType->sizeOnStack(), lvalue->sizeOnStack());
		lvalue->storeValue(*valType, _location, true);
		valuePos += m_context.stackHeight() - stackHeight;
	}
	// As the type of an assignment to a tuple type is the empty tuple, we always move.
	CompilerUtils(m_context).popStackElement(_sourceType);
}

void TupleObject::setToZero(SourceLocation const&, bool) const
{
	solAssert(false, "Tried to delete tuple.");
}
