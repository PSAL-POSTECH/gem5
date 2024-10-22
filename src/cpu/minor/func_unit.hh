/*
 * Copyright (c) 2013-2014 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *
 *  Execute function unit descriptions and pipeline implementations.
 */

#ifndef __CPU_MINOR_FUNC_UNIT_HH__
#define __CPU_MINOR_FUNC_UNIT_HH__

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "base/types.hh"
#include "cpu/func_unit.hh"
#include "cpu/minor/buffers.hh"
#include "cpu/minor/dyn_inst.hh"
#include "cpu/timing_expr.hh"
#include "params/MinorFU.hh"
#include "params/MinorFUPool.hh"
#include "params/MinorOpClass.hh"
#include "params/MinorOpClassSet.hh"
#include "sim/clocked_object.hh"
#include "sim/sim_object.hh"

#include "debug/SystolicArray.hh"
#include "debug/PyTorchSim.hh"

#define VALID_DATA 1
#define INVALID_DATA 0

namespace gem5
{

/** Boxing for MinorOpClass to get around a build problem with C++11 but
 *  also allow for future additions to op class checking */
class MinorOpClass : public SimObject
{
  public:
    OpClass opClass;

  public:
    MinorOpClass(const MinorOpClassParams &params) :
        SimObject(params),
        opClass(params.opClass)
    { }
};

/** Wrapper for a matchable set of op classes */
class MinorOpClassSet : public SimObject
{
  public:
    std::vector<MinorOpClass *> opClasses;

    /** Convenience packing of opClasses into a bit vector for easier
     *  testing */
    std::vector<bool> capabilityList;

  public:
    MinorOpClassSet(const MinorOpClassSetParams &params);

  public:
    /** Does this set support the given op class */
    bool provides(OpClass op_class) { return capabilityList[op_class]; }
};

/** Extra timing capability to allow individual ops to have their source
 *  register dependency latencies tweaked based on the ExtMachInst of the
 *  source instruction.
 */
class MinorFUTiming: public SimObject
{
  public:
    /** Mask off the ExtMachInst of an instruction before comparing with
     *  match */
    uint64_t mask;
    uint64_t match;

    /** Textual description of the decode's purpose */
    std::string description;

    /** If true, instructions matching this mask/match should *not* be
     *  issued in this FU */
    bool suppress;

    /** Extra latency that the instruction should spend at the end of
     *  the pipeline */
    Cycles extraCommitLat;
    TimingExpr *extraCommitLatExpr;

    /** Extra delay that results should show in the scoreboard after
     *  leaving the pipeline.  If set to Cycles(0) for memory references,
     *  an 'unpredictable' return time will be set in the scoreboard
     *  blocking following dependent instructions from issuing */
    Cycles extraAssumedLat;

    /** Cycle offsets from the scoreboard delivery times of register values
     *  for each of this instruction's source registers (in srcRegs order).
     *  The offsets are subtracted from the scoreboard returnCycle times.
     *  For example, for an instruction type with 3 source registers,
     *  [2, 1, 2] will allow the instruction to issue upto 2 cycles early
     *  for dependencies on the 1st and 3rd register and upto 1 cycle early
     *  on the 2nd. */
    std::vector<Cycles> srcRegsRelativeLats;

    /** Extra opClasses check (after the FU one) */
    MinorOpClassSet *opClasses;

  public:
    MinorFUTiming(const MinorFUTimingParams &params);

  public:
    /** Does the extra decode in this object support the given op class */
    bool provides(OpClass op_class) { return opClasses->provides(op_class); }
};

/** A functional unit that can execute any of opClasses operations with a
 *  single op(eration)Lat(ency) and issueLat(ency) associated with the unit
 *  rather than each operation (as in src/FuncUnit).
 *
 *  This is very similar to cpu/func_unit but replicated here to allow
 *  the Minor functional units to change without having to disturb the common
 *  definition.
 */
class MinorFU : public SimObject
{
  public:
    MinorOpClassSet *opClasses;

    /** Delay from issuing the operation, to it reaching the
     *  end of the associated pipeline */
    Cycles opLat;

    /** Delay after issuing an operation before the next
     *  operation can be issued */
    Cycles issueLat;

    /** FUs which this pipeline can't receive a forwarded (i.e. relative
     *  latency != 0) result from */
    std::vector<unsigned int> cantForwardFromFUIndices;

    /** Extra timing info to give timings to individual ops */
    std::vector<MinorFUTiming *> timings;

	std::string unitType;
	int systolicArrayWidth;
	int systolicArrayHeight;

  public:
    MinorFU(const MinorFUParams &params) :
        SimObject(params),
        opClasses(params.opClasses),
        opLat(params.opLat),
        issueLat(params.issueLat),
        cantForwardFromFUIndices(params.cantForwardFromFUIndices),
        timings(params.timings),
		unitType(params.unitType),
		systolicArrayWidth(params.systolicArrayWidth),
		systolicArrayHeight(params.systolicArrayHeight)
    { }

	virtual ~MinorFU() = default;

    void setCycle(Cycles lat) {
		opLat = lat;
    }
};

class SystolicArrayFU : public MinorFU
{
  public:
	bool is_oqueue_empty;
	bool is_processing;
  private:
	bool trigger = false;
	bool w_Trigger = false;
	int serializerSize = 256;
	int saSize;
	uint64_t i_Trigger;

	std::queue<int> wQueue;
	std::queue<int> iQueue;
	std::queue<int> SAQueue;
	std::queue<int> oQueue;

	int process_cycle;

	int index;

	uint64_t last_cycle;
  public:
	SystolicArrayFU(const MinorFUParams &params) :
		MinorFU(params),
		is_oqueue_empty(true),
		is_processing(false),
		saSize(params.systolicArrayWidth + params.systolicArrayHeight - 1),
		process_cycle(0),
		index(0),
		last_cycle(0)
    { }

	void pushWeight(int size, uint64_t cycle) {
		if (!w_Trigger) {
			DPRINTF(SystolicArray, "systolicarray.pushWeight: First push: %d\n", cycle);
			DPRINTF(SystolicArray, "systolicarray.pushWeight: input push avail: %d\n", cycle+255);
			w_Trigger = true;
			i_Trigger = cycle + 255;
		}

		for (int i = 0; i < size; i++) {
			wQueue.push(VALID_DATA);
		}
	}

	void pushInput(int size, uint64_t cycle) {
		assert(iQueue.size() + size <= serializerSize);

		DPRINTF(SystolicArray, "systolicarray.pushInput: current iQueueSize: %d\n", iQueue.size());
		DPRINTF(SystolicArray, "systolicarray.pushInput: input size: %d\n", size);

		for (int i = 0; i < size; i++) {
			iQueue.push(VALID_DATA);
		}

		if (!trigger) {
			last_cycle = cycle;
			trigger = true;
		}
	}

	void compute(int cycle) {
//		assert(!iQueue.empty());

		process_cycle += cycle;
		is_processing = (process_cycle > 0);
		DPRINTF(SystolicArray, "systolicarray.compute: remaining processing cycle: %d\n", process_cycle);
	}

	void run_systolicArray(uint64_t cycle) {
		// check if SystolicArray & DeSerializer are both full.
		// this situation means that SystolicArray cannot process untill vpop occurs.
		// therefore, SystolicArray should be halted.
		if (!trigger)
			return;

		DPRINTF(SystolicArray, "systolic.run: Run for %d times\n", cycle - last_cycle);
		for (uint64_t i = last_cycle; i < cycle; i++) {
			if (oQueue.size() == serializerSize) {
				DPRINTF(SystolicArray, "systolic.run: DeSerializer is full! Needs to be popped by vpop instruction\n");
				return;
			}

			// SystolicArray -> DeSerializer
			// if SystolicArray is full, pop front element
			// and if the front() is VALID_DATA, push it into DeSerializer
			if (SAQueue.size() == saSize) {
				DPRINTF(SystolicArray, "systolic.run: SystolicArray is all warmed up!\n");
				if (SAQueue.front() != INVALID_DATA) {
					DPRINTF(SystolicArray, "systolic.run: Valid data %d is processed to DeSerializer.\n", SAQueue.front());
					oQueue.push(SAQueue.front());
				}
				SAQueue.pop();
			}

			// Serializer -> SystolicArray
			// push a single element to SystolicArray from iQueue
			// if iQueue is empty, push a invalid_data(0)
			if (iQueue.empty()) {
				DPRINTF(SystolicArray, "systolic.run: Serializer is empty. INVALID\n");
				SAQueue.push(INVALID_DATA);
			} else {
				DPRINTF(SystolicArray, "systolic.run: Serializer[%d/%d] has data. VALID\n", iQueue.size(), serializerSize);
				SAQueue.push(iQueue.front());
				iQueue.pop();
			}
		}

		last_cycle = cycle;
	}

	void process() {
		if (process_cycle <= 0) return;

		// Check if SAQueue and oQueue is full together.
		bool sa_full = (SAQueue.size() == saSize);
		bool oQ_full = (oQueue.size() == serializerSize);
		if (sa_full && oQ_full) {
			// TODO: handle this condition later. SA should be stalled.
			DPRINTF(SystolicArray, "systolicarray.process: SA is full, output Queue is full\n");
			return;
		}
		DPRINTF(SystolicArray, "systolicarray.process: SA %d/%d\n", SAQueue.size(), saSize);

		if (iQueue.empty()) {
			SAQueue.push(-1);
		} else {
			SAQueue.push(iQueue.front());
			iQueue.pop();
		}

		if (SAQueue.size() > saSize) {
			if (!oQ_full) {
				DPRINTF(SystolicArray, "systolicarray.process: %d is pushed into DeSerializer\n", SAQueue.front());
				oQueue.push(SAQueue.front());
				SAQueue.pop();
			} else {
				assert(0);
			}
		}

		process_cycle--;
		is_oqueue_empty = oQueue.empty();
		is_processing = (process_cycle > 0);
		DPRINTF(SystolicArray, "systolicarray.process: remaining processing cycle: %d\n", process_cycle);
	}

	void vpop(int size) {
		assert(oQueue.size() >= size);

		DPRINTF(SystolicArray, "systolicarray.vpop: DeSerializer Size before vpop(%d): %d\n", size, oQueue.size());
		for (int i = 0; i < size; i++) {
			oQueue.pop();
		}
		is_oqueue_empty = oQueue.empty();

		DPRINTF(SystolicArray, "systolicarray.vpop: DeSerializer Size after vpop: %d\n", oQueue.size());
	}

	bool is_input_pushable(uint64_t cycle) {
		return (cycle >= i_Trigger);
	}

	bool is_popable(int size) {
		return (oQueue.size() >= size);
	}

	int ready_size() {
		return oQueue.size();
	}
};

class SparseAccelFU : public MinorFU
{
  public:
	SparseAccelFU(const MinorFUParams &params) :
		MinorFU(params)
	{ }
};

/** A collection of MinorFUs */
class MinorFUPool : public SimObject
{
  public:
    std::vector<MinorFU *> funcUnits;

  public:
    MinorFUPool(const MinorFUPoolParams &params) :
        SimObject(params),
        funcUnits(params.funcUnits)
    { }
};

namespace minor
{

/** Container class to box instructions in the FUs to make those
 *  queues have correct bubble behaviour when stepped */
class QueuedInst
{
  public:
    MinorDynInstPtr inst;

  public:
    QueuedInst(MinorDynInstPtr inst_ = MinorDynInst::bubble()) :
        inst(inst_)
    { }

  public:
    /** Report and bubble interfaces */
    void reportData(std::ostream &os) const;
    bool isBubble() const { return inst->isBubble(); }

    static QueuedInst bubble()
    { return QueuedInst(MinorDynInst::bubble()); }
};

/** Functional units have pipelines which stall when an inst gets to
 *  their ends allowing Execute::commit to pick up timing-completed insts
 *  when it feels like it */
typedef SelfStallingPipeline<QueuedInst,
    ReportTraitsAdaptor<QueuedInst> > FUPipelineBase;

/** A functional unit configured from a MinorFU object */
class FUPipeline : public FUPipelineBase, public FuncUnit
{
  public:
    /** Functional unit description that this pipeline implements */
    const MinorFU &description;

    /** An FUPipeline needs access to curCycle, use this timing source */
    ClockedObject &timeSource;

    /** Set of operation classes supported by this FU */
    std::bitset<Num_OpClasses> capabilityList;

    /** FUs which this pipeline can't receive a forwarded (i.e. relative
     *  latency != 0) result from */
    std::vector<bool> cantForwardFromFUIndices;

  public:
    /** When can a new instruction be inserted into the pipeline?  This is
     *  an absolute cycle time unless it is 0 in which case the an
     *  instruction can be pushed straightaway */
    Cycles nextInsertCycle;

  public:
    FUPipeline(const std::string &name, const MinorFU &description_,
        ClockedObject &timeSource_);

  public:
    /** How many cycles must from curCycle before insertion into the
     *  pipeline is allowed */
    Cycles cyclesBeforeInsert();

    /** Can an instruction be inserted now? */
    bool canInsert() const;

    /** Find the extra timing information for this instruction.  Returns
     *  NULL if no decode info. is found */
    MinorFUTiming *findTiming(const StaticInstPtr &inst);

    /** Step the pipeline.  Allow multiple steps? */
    void advance();
};

} // namespace minor
} // namespace gem5

#endif /* __CPU_MINOR_FUNC_UNIT_HH__ */
