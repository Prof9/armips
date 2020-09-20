#include "Archs/GB/CGameboyInstruction.h"
#include "Archs/GB/GameboyOpcodes.h"
#include "Core/Common.h"
#include "Core/Expression.h"
#include "Core/FileManager.h"
#include "Core/Misc.h"

CGameboyInstruction::CGameboyInstruction(const tGameboyOpcode& sourceOpcode, GameboyOpcodeVariables& vars)
{
	this->Opcode = sourceOpcode;
	this->Vars = vars;
}

bool CGameboyInstruction::Validate(const ValidateState& state)
{
	Vars.Length = Opcode.length;
	Vars.Encoding = Opcode.encoding;
	Vars.WritePrefix = Opcode.flags & GB_PREFIX;
	Vars.WriteImmediate8 = Opcode.flags & (GB_IMMEDIATE_S8 | GB_IMMEDIATE_U8);
	Vars.WriteImmediate16 = Opcode.flags & GB_IMMEDIATE_U16;

	// ld (hl),(hl) equivalent to halt
	if (Opcode.flags & GB_LOAD_REG8_REG8)
	{
		if (Vars.LeftParam.num == GB_REG8_MEMHL && Vars.RightParam.num == GB_REG8_MEMHL)
		{
			Logger::queueError(Logger::Error, L"ld (hl),(hl) not allowed");
			return false;
		}
	}

	// Evaluate immediate
	if (Vars.WriteImmediate8 || Vars.WriteImmediate16)
	{
		if (!Vars.ImmediateExpression.evaluateInteger(Vars.Immediate))
		{
			Logger::queueError(Logger::Error, L"Invalid expression");
			return false;
		}
		if (Vars.IsNegative)
		{
			Vars.Immediate = -Vars.Immediate;
		}

		int64_t min = 0;
		int64_t max = 0;
		if (Opcode.flags & GB_IMMEDIATE_U8)
		{
			min = 0;
			max = 255;
			Vars.WriteImmediate8 = true;
			Vars.WriteImmediate16 = false;
		}
		else if (Opcode.flags & GB_IMMEDIATE_S8)
		{
			min = -128;
			max = 127;
			Vars.WriteImmediate8 = true;
			Vars.WriteImmediate16 = false;
		}
		else if (Opcode.flags & GB_IMMEDIATE_U16)
		{
			min = 0;
			max = 65535;
			Vars.WriteImmediate8 = false;
			Vars.WriteImmediate16 = true;
		}

		// add <-> sub
		if ((Opcode.flags & (GB_ADD_IMMEDIATE | GB_SUB_IMMEDIATE)) && Vars.Immediate < 0)
		{
			// Change opcode
			Vars.Encoding ^= 0x10;
			Vars.Immediate = -Vars.Immediate;
		}
		if (Opcode.flags & GB_NEGATE_IMM)
		{
			Vars.Immediate = -Vars.Immediate;
		}
		// add a,1 -> inc a
		if ((Opcode.flags & GB_ADD_IMMEDIATE) && Vars.LeftParam.num == GB_REG8_A && Vars.Immediate == 1)
		{
			// Change opcode
			Vars.Encoding = 0x3C;
			Vars.Length = 1;
			Vars.LeftParam.num = 0;
			Vars.WriteImmediate8 = false;
			Vars.WriteImmediate16 = false;
		}
		// sub a,1 -> dec a
		if ((Opcode.flags & GB_SUB_IMMEDIATE) && Vars.LeftParam.num == GB_REG8_A && Vars.Immediate == 1)
		{
			// Change opcode
			Vars.Encoding = 0x3D;
			Vars.Length = 1;
			Vars.LeftParam.num = 0;
			Vars.WriteImmediate8 = false;
			Vars.WriteImmediate16 = false;
		}

		// Special loads in range 0xFF00 - 0xFFFF
		if (Vars.RightParam.num == GB_REG8_A && Vars.Immediate >= 0xFF00)
		{
			// ld (0xFF00+u8),a can be encoded as E0 XX instead
			Vars.Encoding = 0xE0;
			Vars.Length = 2;
			Vars.Immediate &= 0xFF;
			Vars.RightParam.num = 0;
			Vars.WriteImmediate8 = true;
			Vars.WriteImmediate16 = false;
		}
		else if (Vars.LeftParam.num == GB_REG8_A && Vars.Immediate >= 0xFF00)
		{
			// ld a,(0xFF00+u8) can be encoded as F0 XX instead
			Vars.Encoding = 0xF0;
			Vars.Length = 2;
			Vars.Immediate &= 0xFF;
			Vars.LeftParam.num = 0;
			Vars.WriteImmediate8 = true;
			Vars.WriteImmediate16 = false;
		}

		if (Vars.Immediate < min || Vars.Immediate > max)
		{
			Logger::queueError(Logger::Error, L"Immediate %i out of range", Vars.Immediate);
			return false;
		}
	}

	g_fileManager->advanceMemory(Vars.Length);

	return false;
}

void CGameboyInstruction::Encode() const
{
	unsigned char encoding = Vars.Encoding;

	if (Vars.WritePrefix)
	{
		g_fileManager->writeU8(0xCB);
	}

	if (Opcode.lhs && Opcode.lhsShift >= 0)
	{
		encoding |= Vars.LeftParam.num << Opcode.lhsShift;
	}
	if (Opcode.rhs && Opcode.rhsShift >= 0)
	{
		encoding |= Vars.RightParam.num << Opcode.rhsShift;
	}

	g_fileManager->writeU8(encoding);


	if (Vars.WriteImmediate16)
	{
		g_fileManager->writeU16((uint16_t)(Vars.Immediate & 0xFFFF));
	}
	else if (Vars.WriteImmediate8)
	{
		g_fileManager->writeU8((uint8_t)(Vars.Immediate & 0xFF));
	}
	else if (Opcode.flags & GB_STOP)
	{
		g_fileManager->writeU8(0x00);
	}
}

void CGameboyInstruction::writeTempData(TempData& tempData) const
{
}