/* A VCIX instruction (SiFive Vector Coprocessor Interface, opcode custom-2).
 * gem5 computes nothing for it: its timing is asked of a loaded model. */

#ifndef __ARCH_RISCV_INSTS_VCIX_HH__
#define __ARCH_RISCV_INSTS_VCIX_HH__

#include <memory>
#include <string>

#include "arch/riscv/faults.hh"
#include "arch/riscv/insts/static_inst.hh"
#include "arch/riscv/isa.hh"
#include "arch/riscv/regs/float.hh"
#include "arch/riscv/regs/int.hh"
#include "arch/riscv/regs/misc.hh"
#include "arch/riscv/regs/vector.hh"
#include "arch/riscv/utility.hh"
#include "base/cprintf.hh"
#include "cpu/exec_context.hh"
#include "cpu/op_class.hh"
#include "cpu/static_inst.hh"

namespace gem5
{

namespace RiscvISA
{

class Vcix : public RiscvStaticInst
{
  private:
    /* Three vector operands of up to eight registers each */
    RegId srcRegIdxArr[24];
    RegId destRegIdxArr[8];

    /* funct6[5:2] says which of the vd / vs2 fields are registers */
    enum Shape { NoVs2 = 0x0, Vs2 = 0x2, VdVs2 = 0xa, WideVdVs2 = 0xf };

    /* funct3 says what the rs1 field is */
    enum Rs1Kind { Vector = 0x0, Immediate = 0x3, Integer = 0x4, Float = 0x5 };

    /* A vector operand is a group of LMUL registers starting at reg */
    void
    addVecSrc(unsigned reg, unsigned count)
    {
        for (unsigned i = 0; i < count && reg + i < 32; i++)
            setSrcRegIdx(_numSrcRegs++, vecRegClass[reg + i]);
    }

    void
    addVecDest(unsigned reg, unsigned count)
    {
        for (unsigned i = 0; i < count && reg + i < 32; i++) {
            setDestRegIdx(_numDestRegs++, vecRegClass[reg + i]);
            _numTypedDestRegs[VecRegClass]++;
        }
    }

  public:
    Vcix(ExtMachInst _machInst)
        : RiscvStaticInst("vcix", _machInst, VcixAccelOp)
    {
        setRegIdxArrays(
            reinterpret_cast<RegIdArrayPtr>(
                &std::remove_pointer_t<decltype(this)>::srcRegIdxArr),
            reinterpret_cast<RegIdArrayPtr>(
                &std::remove_pointer_t<decltype(this)>::destRegIdxArr));

        const uint32_t bits = _machInst.instBits;
        const unsigned shape = (bits >> 28) & 0xf;
        const bool has_dest = ((bits >> 25) & 0x1) == 0;
        const unsigned rs1_kind = (bits >> 12) & 0x7;
        const unsigned vd = (bits >> 7) & 0x1f;
        const unsigned rs1 = (bits >> 15) & 0x1f;
        const unsigned vs2 = (bits >> 20) & 0x1f;

        const unsigned vlmul = _machInst.vtype8.vlmul;
        const unsigned group = vlmul < 4 ? 1u << vlmul : 1;
        const unsigned vd_group =
            shape == WideVdVs2 && vlmul < 3 ? 2 * group : group;

        _numSrcRegs = 0;
        _numDestRegs = 0;

        if (has_dest)
            addVecDest(vd, vd_group);
        if (shape == VdVs2 || shape == WideVdVs2)
            addVecSrc(vd, vd_group);
        if (shape != NoVs2)
            addVecSrc(vs2, group);

        if (rs1_kind == Vector)
            addVecSrc(rs1, group);
        else if (rs1_kind == Integer)
            setSrcRegIdx(_numSrcRegs++, intRegClass[rs1]);
        else if (rs1_kind == Float)
            setSrcRegIdx(_numSrcRegs++, floatRegClass[rs1]);

        flags[IsVector] = true;
    }

    /* The checks gem5's own vector and floating-point instructions make */
    Fault
    execute(ExecContext *xc, trace::InstRecord *) const override
    {
        if (((machInst.instBits >> 12) & 0x7) == Float) {
            Fault fault = updateFPUStatus(xc, machInst, false);
            if (fault != NoFault)
                return fault;
        }
        return updateVPUStatus(xc, machInst, _numDestRegs > 0, true);
    }

    std::string
    generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override
    {
        std::string text = csprintf("vcix %#010x", machInst.instBits);

        for (int i = 0; i < _numDestRegs; i++)
            text += (i ? "," : " -> ") + registerName(destRegIdx(i));
        for (int i = 0; i < _numSrcRegs; i++)
            text += (i ? "," : " <- ") + registerName(srcRegIdx(i));
        return text;
    }
};

/* An instruction in the custom-1 opcode: taken to be R-type on x registers */
class AccelCustom1 : public RiscvStaticInst
{
  private:
    RegId srcRegIdxArr[2];
    RegId destRegIdxArr[1];

  public:
    AccelCustom1(ExtMachInst _machInst)
        : RiscvStaticInst("custom1", _machInst, VcixAccelOp)
    {
        setRegIdxArrays(
            reinterpret_cast<RegIdArrayPtr>(
                &std::remove_pointer_t<decltype(this)>::srcRegIdxArr),
            reinterpret_cast<RegIdArrayPtr>(
                &std::remove_pointer_t<decltype(this)>::destRegIdxArr));

        const uint32_t bits = _machInst.instBits;

        _numSrcRegs = 0;
        _numDestRegs = 0;
        setSrcRegIdx(_numSrcRegs++, intRegClass[(bits >> 15) & 0x1f]);
        setSrcRegIdx(_numSrcRegs++, intRegClass[(bits >> 20) & 0x1f]);
    }

    Fault
    execute(ExecContext *, trace::InstRecord *) const override
    {
        return NoFault;
    }

    std::string
    generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override
    {
        return csprintf("custom1 %#010x <- %s,%s", machInst.instBits,
            registerName(srcRegIdx(0)), registerName(srcRegIdx(1)));
    }
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_INSTS_VCIX_HH__
