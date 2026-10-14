/*
 * Copyright (c) 2026 POSTECH PSAL
 * All rights reserved.
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
 * A scratchpad with load_ports reads, store_ports writes and access_ports
 * accesses in total per cycle, shared by all its response ports, answered
 * after a fixed latency. It refuses only when one of those caps is reached.
 */

#ifndef __MEM_MULTIPORT_SCRATCHPAD_HH__
#define __MEM_MULTIPORT_SCRATCHPAD_HH__

#include <deque>
#include <memory>
#include <vector>

#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "mem/port.hh"
#include "params/MultiPortScratchpad.hh"
#include "sim/eventq.hh"

namespace gem5
{

namespace memory
{

class MultiPortScratchpad : public AbstractMemory
{
  private:
    struct Deferred
    {
        Tick tick;
        PacketPtr pkt;
    };

    class MemoryPort : public ResponsePort
    {
      public:
        MemoryPort(const std::string &_name, MultiPortScratchpad &_mem,
                   PortID _id);

        MultiPortScratchpad &mem;
        const PortID id;
        bool needsRetry = false;
        bool respBlocked = false;
        std::deque<Deferred> responses;
        EventFunctionWrapper sendEvent;

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        Tick recvAtomicBackdoor(PacketPtr pkt,
                                MemBackdoorPtr &_backdoor) override;
        void recvFunctional(PacketPtr pkt) override;
        void recvMemBackdoorReq(const MemBackdoorReq &req,
                                MemBackdoorPtr &backdoor) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    std::vector<std::unique_ptr<MemoryPort>> ports;

    const unsigned loadPorts;
    const unsigned storePorts;
    const unsigned accessPorts;
    const Cycles latency;

    /** The cycle the counts below belong to. */
    Cycles window;
    unsigned loadsThisCycle = 0;
    unsigned storesThisCycle = 0;

    EventFunctionWrapper retryEvent;
    std::unique_ptr<Packet> pendingDelete;

    void rollWindow();
    bool recvTimingReq(PacketPtr pkt, MemoryPort &port);
    void sendResponses(MemoryPort &port);
    void sendRetries();
    void checkDrained();

    struct ScratchpadStats : public statistics::Group
    {
        ScratchpadStats(MultiPortScratchpad &mem);
        void regStats() override;

        MultiPortScratchpad &mem;
        statistics::Scalar loads;
        statistics::Scalar stores;
        statistics::Scalar loadRefusals;
        statistics::Scalar storeRefusals;
        statistics::Scalar accessRefusals;
        statistics::Distribution loadsPerCycle;
        statistics::Distribution storesPerCycle;
        statistics::Distribution accessesPerCycle;
    } stats;

  public:
    MultiPortScratchpad(const MultiPortScratchpadParams &p);

    DrainState drain() override;
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_MULTIPORT_SCRATCHPAD_HH__
