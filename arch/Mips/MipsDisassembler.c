//===- MipsDisassembler.cpp - Disassembler for Mips -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the Mips Disassembler.
//
//===----------------------------------------------------------------------===//

/* Capstone Disassembler Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2013> */

#include <stdio.h>
#include <string.h>

#include <stdbool.h>

#include <inttypes.h> 

#include "../../utils.h"

#include "../../MCInst.h"
#include "../../MCRegisterInfo.h"
#include "../../SStream.h"

#include "../../MathExtras.h"

//#include "Mips.h"
//#include "MipsRegisterInfo.h"
//#include "MipsSubtarget.h"
#include "../../MCFixedLenDisassembler.h"
#include "../../MCInst.h"
//#include "llvm/MC/MCSubtargetInfo.h"
#include "../../MCRegisterInfo.h"
#include "../../MCDisassembler.h"

// Forward declare these because the autogenerated code will reference them.
// Definitions are further down.
static DecodeStatus DecodeGPR64RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeCPU16RegsRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeGPR32RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodePtrRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeDSPRRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeFGR64RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeFGR32RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeFGRH32RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeCCRRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeFCCRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeHWRegsRegisterClass(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeAFGR64RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeACC64DSPRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeHI32DSPRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeLO32DSPRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMSA128BRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMSA128HRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMSA128WRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMSA128DRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMSACtrlRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeBranchTarget(MCInst *Inst,
		unsigned Offset, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeJumpTarget(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

// DecodeBranchTargetMM - Decode microMIPS branch offset, which is
// shifted left by 1 bit.
static DecodeStatus DecodeBranchTargetMM(MCInst *Inst,
		unsigned Offset, uint64_t Address, MCRegisterInfo *Decoder);

// DecodeJumpTargetMM - Decode microMIPS jump target, which is
// shifted left by 1 bit.
static DecodeStatus DecodeJumpTargetMM(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMem(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMSA128Mem(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMemMMImm12(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeMemMMImm16(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeFMem(MCInst *Inst, unsigned Insn,
		uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeSimm16(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

// Decode the immediate field of an LSA instruction which
// is off by one.
static DecodeStatus DecodeLSAImm(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeInsSize(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

static DecodeStatus DecodeExtSize(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder);

#define GET_SUBTARGETINFO_ENUM
#include "MipsGenSubtargetInfo.inc"

// Hacky: enable all features for disassembler
static uint64_t getFeatureBits(int mode)
{
	uint64_t Bits = (uint64_t)-1;	// include every features by default

	// ref: MipsGenDisassemblerTables.inc::checkDecoderPredicate()
	// some features are mutually execlusive
	if (mode & CS_MODE_16) {
		Bits &= ~Mips_FeatureMips32r2;
		Bits &= ~Mips_FeatureMips32;
		Bits &= ~Mips_FeatureFPIdx;
		Bits &= ~Mips_FeatureBitCount;
		Bits &= ~Mips_FeatureSwap;
		Bits &= ~Mips_FeatureSEInReg;
		Bits &= ~Mips_FeatureMips64r2;
		Bits &= ~Mips_FeatureFP64Bit;
	} else if (mode & CS_MODE_32) {
		Bits &= ~Mips_FeatureMips16;
		Bits &= ~Mips_FeatureFP64Bit;
	} else if (mode & CS_MODE_64) {
		Bits &= ~Mips_FeatureMips16;
	}

	if (mode & CS_MODE_MICRO)
		Bits |= Mips_FeatureMicroMips;
	else
		Bits &= ~Mips_FeatureMicroMips;

	return Bits;
}

#include "MipsGenDisassemblerTables.inc"

#define GET_REGINFO_ENUM
#include "MipsGenRegisterInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MipsGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "MipsGenInstrInfo.inc"

void Mips_init(MCRegisterInfo *MRI)
{
	// InitMCRegisterInfo(MipsRegDesc, 317,
	// RA, PC,
	// MipsMCRegisterClasses, 34,
	// MipsRegUnitRoots, 196,
	// MipsRegDiffLists,
	// MipsRegStrings,
	// MipsSubRegIdxLists, 12,
	// MipsSubRegIdxRanges,   MipsRegEncodingTable);
	MCRegisterInfo_InitMCRegisterInfo(MRI, MipsRegDesc, 317,
			0, 0, 
			MipsMCRegisterClasses, 34,
			0, 0, 
			MipsRegDiffLists,
			0, 
			MipsSubRegIdxLists, 12,
			0);
}

/// readInstruction - read four bytes from the MemoryObject
/// and return 32 bit word sorted according to the given endianess
static DecodeStatus readInstruction32(unsigned char *code, uint32_t *insn, bool isBigEndian, bool isMicroMips)
{
	// We want to read exactly 4 Bytes of data.
	if (isBigEndian) {
		// Encoded as a big-endian 32-bit word in the stream.
		*insn = (code[3] <<  0) |
			(code[2] <<  8) |
			(code[1] << 16) |
			(code[0] << 24);
	} else {
		// Encoded as a small-endian 32-bit word in the stream.
		// Little-endian byte ordering:
		//   mips32r2:   4 | 3 | 2 | 1
		//   microMIPS:  2 | 1 | 4 | 3
		if (isMicroMips) {
			*insn = (code[2] <<  0) |
				(code[3] <<  8) |
				(code[0] << 16) |
				(code[1] << 24);
		} else {
			*insn = (code[0] <<  0) |
				(code[1] <<  8) |
				(code[2] << 16) |
				(code[3] << 24);
		}
	}

	return MCDisassembler_Success;
}

static DecodeStatus MipsDisassembler_getInstruction(int mode, MCInst *instr,
		const uint8_t *code, size_t code_len,
		uint16_t *Size,
		uint64_t Address, bool isBigEndian, MCRegisterInfo *MRI)
{
	uint32_t Insn;
	DecodeStatus Result;

	if (code_len < 4)
		// not enough data
		return MCDisassembler_Fail;

	Result = readInstruction32((unsigned char*)code, &Insn, isBigEndian,
			mode & CS_MODE_MICRO);
	if (Result == MCDisassembler_Fail)
		return MCDisassembler_Fail;

	if (mode & CS_MODE_MICRO) {
		// Calling the auto-generated decoder function.
		Result = decodeInstruction(DecoderTableMicroMips32, instr, Insn, Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 4;
			return Result;
		}
		return MCDisassembler_Fail;
	}

	// Calling the auto-generated decoder function.
	Result = decodeInstruction(DecoderTableMips32, instr, Insn, Address, MRI, mode);
	if (Result != MCDisassembler_Fail) {
		*Size = 4;
		return Result;
	}

	return MCDisassembler_Fail;
}

bool Mips_getInstruction(csh ud, const uint8_t *code, size_t code_len, MCInst *instr,
		uint16_t *size, uint64_t address, void *info)
{
	cs_struct *handle = (cs_struct *)(uintptr_t)ud;

	DecodeStatus status = MipsDisassembler_getInstruction(handle->mode, instr,
			code, code_len,
			size,
			address, handle->big_endian, (MCRegisterInfo *)info);

	return status == MCDisassembler_Success;
}

static DecodeStatus Mips64Disassembler_getInstruction(int mode, MCInst *instr,
		const uint8_t *code, size_t code_len,
		uint16_t *Size,
		uint64_t Address, bool isBigEndian, MCRegisterInfo *MRI)
{
	uint32_t Insn;

	DecodeStatus Result = readInstruction32((unsigned char*)code, &Insn, isBigEndian, false);
	if (Result == MCDisassembler_Fail)
		return MCDisassembler_Fail;

	// Calling the auto-generated decoder function.
	Result = decodeInstruction(DecoderTableMips6432, instr, Insn, Address, MRI, mode);
	if (Result != MCDisassembler_Fail) {
		*Size = 4;
		return Result;
	}
	// If we fail to decode in Mips64 decoder space we can try in Mips32
	Result = decodeInstruction(DecoderTableMips32, instr, Insn, Address, MRI, mode);
	if (Result != MCDisassembler_Fail) {
		*Size = 4;
		return Result;
	}

	return MCDisassembler_Fail;
}

bool Mips64_getInstruction(csh ud, const uint8_t *code, size_t code_len, MCInst *instr,
		uint16_t *size, uint64_t address, void *info)
{
	cs_struct *handle = (cs_struct *)(uintptr_t)ud;

	DecodeStatus status = Mips64Disassembler_getInstruction(handle->mode, instr,
			code, code_len,
			size,
			address, handle->big_endian, (MCRegisterInfo *)info);

	return status == MCDisassembler_Success;
}

static unsigned getReg(MCRegisterInfo *MRI, unsigned RC, unsigned RegNo)
{
	//MipsDisassemblerBase *Dis = static_cast<const MipsDisassemblerBase*>(D);
	//return *(Dis->getRegInfo()->getRegClass(RC).begin() + RegNo);
	MCRegisterClass *rc = MCRegisterInfo_getRegClass(MRI, RC);
	return rc->RegsBegin[RegNo];
}

static DecodeStatus DecodeCPU16RegsRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
	return MCDisassembler_Fail;
}

static DecodeStatus DecodeGPR64RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_GPR64RegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeGPR32RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;
	Reg = getReg(Decoder, Mips_GPR32RegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodePtrRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
	if (Inst->csh->mode & CS_MODE_N64)
		return DecodeGPR64RegisterClass(Inst, RegNo, Address, Decoder);

	return DecodeGPR32RegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeDSPRRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
	return DecodeGPR32RegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeFGR64RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_FGR64RegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeFGR32RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_FGR32RegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeFGRH32RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_FGRH32RegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeCCRRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_CCRRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeFCCRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 7)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_FCCRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeMem(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	int Offset = SignExtend32(Insn & 0xffff, 16);
	unsigned Reg = fieldFromInstruction(Insn, 16, 5);
	unsigned Base = fieldFromInstruction(Insn, 21, 5);

	Reg = getReg(Decoder, Mips_GPR32RegClassID, Reg);
	Base = getReg(Decoder, Mips_GPR32RegClassID, Base);

	if(MCInst_getOpcode(Inst) == Mips_SC){
		MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	}

	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	MCInst_addOperand(Inst, MCOperand_CreateReg(Base));
	MCInst_addOperand(Inst, MCOperand_CreateImm(Offset));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMSA128Mem(MCInst *Inst, unsigned Insn,
		uint64_t Address, MCRegisterInfo *Decoder)
{
	int Offset = SignExtend32(fieldFromInstruction(Insn, 16, 10), 10);
	unsigned Reg = fieldFromInstruction(Insn, 6, 5);
	unsigned Base = fieldFromInstruction(Insn, 11, 5);

	Reg = getReg(Decoder, Mips_MSA128BRegClassID, Reg);
	Base = getReg(Decoder, Mips_GPR32RegClassID, Base);

	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	MCInst_addOperand(Inst, MCOperand_CreateReg(Base));
	// MCInst_addOperand(Inst, MCOperand_CreateImm(Offset));

	// The immediate field of an LD/ST instruction is scaled which means it must
	// be multiplied (when decoding) by the size (in bytes) of the instructions'
	// data format.
	// .b - 1 byte
	// .h - 2 bytes
	// .w - 4 bytes
	// .d - 8 bytes
	switch(MCInst_getOpcode(Inst))
	{
		default:
			//assert (0 && "Unexpected instruction");
			return MCDisassembler_Fail;
			break;
		case Mips_LD_B:
		case Mips_ST_B:
			MCInst_addOperand(Inst, MCOperand_CreateImm(Offset));
			break;
		case Mips_LD_H:
		case Mips_ST_H:
			MCInst_addOperand(Inst, MCOperand_CreateImm(Offset << 1));
			break;
		case Mips_LD_W:
		case Mips_ST_W:
			MCInst_addOperand(Inst, MCOperand_CreateImm(Offset << 2));
			break;
		case Mips_LD_D:
		case Mips_ST_D:
			MCInst_addOperand(Inst, MCOperand_CreateImm(Offset << 3));
			break;
	}

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMemMMImm12(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	int Offset = SignExtend32(Insn & 0x0fff, 12);
	unsigned Reg = fieldFromInstruction(Insn, 21, 5);
	unsigned Base = fieldFromInstruction(Insn, 16, 5);

	Reg = getReg(Decoder, Mips_GPR32RegClassID, Reg);
	Base = getReg(Decoder, Mips_GPR32RegClassID, Base);

	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	MCInst_addOperand(Inst, MCOperand_CreateReg(Base));
	MCInst_addOperand(Inst, MCOperand_CreateImm(Offset));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMemMMImm16(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	int Offset = SignExtend32(Insn & 0xffff, 16);
	unsigned Reg = fieldFromInstruction(Insn, 21, 5);
	unsigned Base = fieldFromInstruction(Insn, 16, 5);

	Reg = getReg(Decoder, Mips_GPR32RegClassID, Reg);
	Base = getReg(Decoder, Mips_GPR32RegClassID, Base);

	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	MCInst_addOperand(Inst, MCOperand_CreateReg(Base));
	MCInst_addOperand(Inst, MCOperand_CreateImm(Offset));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeFMem(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	int Offset = SignExtend32(Insn & 0xffff, 16);
	unsigned Reg = fieldFromInstruction(Insn, 16, 5);
	unsigned Base = fieldFromInstruction(Insn, 21, 5);

	Reg = getReg(Decoder, Mips_FGR64RegClassID, Reg);
	Base = getReg(Decoder, Mips_GPR32RegClassID, Base);

	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	MCInst_addOperand(Inst, MCOperand_CreateReg(Base));
	MCInst_addOperand(Inst, MCOperand_CreateImm(Offset));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeHWRegsRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
	// Currently only hardware register 29 is supported.
	if (RegNo != 29)
		return  MCDisassembler_Fail;
	MCInst_addOperand(Inst, MCOperand_CreateReg(Mips_HWR29));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeAFGR64RegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 30 || RegNo %2)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_AFGR64RegClassID, RegNo /2);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeACC64DSPRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo >= 4)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_ACC64DSPRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeHI32DSPRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo >= 4)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_HI32DSPRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeLO32DSPRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo >= 4)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_LO32DSPRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeMSA128BRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_MSA128BRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMSA128HRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_MSA128HRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMSA128WRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_MSA128WRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMSA128DRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 31)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_MSA128DRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeMSACtrlRegisterClass(MCInst *Inst,
		unsigned RegNo, uint64_t Address, MCRegisterInfo *Decoder)
{
    unsigned Reg = 0;
	if (RegNo > 7)
		return MCDisassembler_Fail;

	Reg = getReg(Decoder, Mips_MSACtrlRegClassID, RegNo);
	MCInst_addOperand(Inst, MCOperand_CreateReg(Reg));

	return MCDisassembler_Success;
}

static DecodeStatus DecodeBranchTarget(MCInst *Inst,
		unsigned Offset, uint64_t Address, MCRegisterInfo *Decoder)
{
	unsigned BranchOffset = Offset & 0xffff;
	BranchOffset = SignExtend32(BranchOffset << 2, 18) + 4;
	MCInst_addOperand(Inst, MCOperand_CreateImm(BranchOffset));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeJumpTarget(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	unsigned JumpOffset = fieldFromInstruction(Insn, 0, 26) << 2;
	MCInst_addOperand(Inst, MCOperand_CreateImm(JumpOffset));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeBranchTargetMM(MCInst *Inst,
		unsigned Offset, uint64_t Address, MCRegisterInfo *Decoder)
{
	unsigned BranchOffset = Offset & 0xffff;
	BranchOffset = SignExtend32(BranchOffset << 1, 18);
	MCInst_addOperand(Inst, MCOperand_CreateImm(BranchOffset));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeJumpTargetMM(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	unsigned JumpOffset = fieldFromInstruction(Insn, 0, 26) << 1;
	MCInst_addOperand(Inst, MCOperand_CreateImm(JumpOffset));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeSimm16(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	MCInst_addOperand(Inst, MCOperand_CreateImm(SignExtend32(Insn, 16)));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeLSAImm(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	// We add one to the immediate field as it was encoded as 'imm - 1'.
	MCInst_addOperand(Inst, MCOperand_CreateImm(Insn + 1));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeInsSize(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	// First we need to grab the pos(lsb) from MCInst.
	int Pos = (int)MCOperand_getImm(MCInst_getOperand(Inst, 2));
	int Size = (int) Insn - Pos + 1;
	MCInst_addOperand(Inst, MCOperand_CreateImm(SignExtend32(Size, 16)));
	return MCDisassembler_Success;
}

static DecodeStatus DecodeExtSize(MCInst *Inst,
		unsigned Insn, uint64_t Address, MCRegisterInfo *Decoder)
{
	int Size = (int) Insn  + 1;
	MCInst_addOperand(Inst, MCOperand_CreateImm(SignExtend32(Size, 16)));
	return MCDisassembler_Success;
}
