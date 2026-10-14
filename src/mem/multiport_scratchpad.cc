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

#include "mem/multiport_scratchpad.hh"

#include <algorithm>

#include "base/trace.hh"
#include "debug/Drain.hh"
#include "debug/MultiPortScratchpad.hh"

namespace gem5
{

namespace memory
{

MultiPortScratchpad::MultiPortScratchpad(
        const MultiPortScratchpadParams &p) :
    AbstractMemory(p),
    loadPorts(p.load_ports), storePorts(p.store_ports),
    accessPorts(p.access_ports),
    latency(p.latency), window(0),
    retryEvent([this]{ sendRetries(); }, name() + ".retry"),
    stats(*this)
{
    fatal_if(loadPorts == 0 || storePorts == 0 || accessPorts == 0,
             "%s: load_ports, store_ports and access_ports must be at "
             "least 1", name());
    for (int i = 0; i < p.port_port_connection_count; ++i) {
        ports.emplace_back(new MemoryPort(
            csprintf("%s.port[%d]", name(), i), *this, i));
    }
}

void
MultiPortScratchpad::init()
{
    AbstractMemory::init();
    for (auto &port : ports) {
        if (port->isConnected())
            port->sendRangeChange();
    }
}

void
MultiPortScratchpad::rollWindow()
{
    Cycles now = curCycle();
    if (now == window)
        return;
    if (loadsThisCycle || storesThisCycle) {
        stats.loadsPerCycle.sample(loadsThisCycle);
        stats.storesPerCycle.sample(storesThisCycle);
        stats.accessesPerCycle.sample(loadsThisCycle + storesThisCycle);
    }
    loadsThisCycle = storesThisCycle = 0;
    window = now;
}

bool
MultiPortScratchpad::recvTimingReq(PacketPtr pkt, MemoryPort &port)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");
    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see reads and writes at a scratchpad, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    // A port already promised a retry waits for it.
    if (port.needsRetry)
        return false;

    rollWindow();
    const bool is_write = pkt->isWrite();
    unsigned &used = is_write ? storesThisCycle : loadsThisCycle;
    const bool kind_full = used >= (is_write ? storePorts : loadPorts);
    const bool access_full =
        loadsThisCycle + storesThisCycle >= accessPorts;
    if (kind_full || access_full) {
        if (kind_full && is_write)
            stats.storeRefusals++;
        else if (kind_full)
            stats.loadRefusals++;
        else
            stats.accessRefusals++;
        DPRINTF(MultiPortScratchpad, "port[%d] %s %#llx refused, %d %s "
                "and %d access ports taken this cycle\n", port.id,
                pkt->cmdString(), pkt->getAddr(), used,
                is_write ? "store" : "load",
                loadsThisCycle + storesThisCycle);
        port.needsRetry = true;
        if (!retryEvent.scheduled())
            schedule(retryEvent, clockEdge(Cycles(1)));
        return false;
    }
    used++;
    if (is_write)
        stats.stores++;
    else
        stats.loads++;

    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;

    const bool needs_response = pkt->needsResponse();
    access(pkt);
    if (!needs_response) {
        pendingDelete.reset(pkt);
        return true;
    }
    assert(pkt->isResponse());

    Tick when = curTick() + receive_delay + cyclesToTicks(latency);
    DPRINTF(MultiPortScratchpad, "port[%d] %s %#llx accepted (%d/%d), "
            "response at %llu\n", port.id, pkt->cmdString(), pkt->getAddr(),
            used, is_write ? storePorts : loadPorts, when);

    // In tick order, never ahead of an earlier response to the same address.
    auto i = port.responses.end();
    while (i != port.responses.begin()) {
        auto prev = std::prev(i);
        if (when >= prev->tick || prev->pkt->matchAddr(pkt))
            break;
        i = prev;
    }
    port.responses.insert(i, Deferred{when, pkt});

    if (!port.respBlocked) {
        Tick t = std::max(port.responses.front().tick, curTick());
        if (!port.sendEvent.scheduled())
            schedule(port.sendEvent, t);
        else if (port.sendEvent.when() > t)
            reschedule(port.sendEvent, t);
    }
    return true;
}

void
MultiPortScratchpad::sendResponses(MemoryPort &port)
{
    while (!port.responses.empty() &&
           port.responses.front().tick <= curTick()) {
        if (!port.sendTimingResp(port.responses.front().pkt)) {
            port.respBlocked = true;
            return;
        }
        port.responses.pop_front();
    }
    if (!port.responses.empty() && !port.sendEvent.scheduled())
        schedule(port.sendEvent, port.responses.front().tick);
    checkDrained();
}

void
MultiPortScratchpad::sendRetries()
{
    std::vector<MemoryPort *> waiting;
    for (auto &port : ports) {
        if (port->needsRetry) {
            port->needsRetry = false;
            waiting.push_back(port.get());
        }
    }
    for (auto *port : waiting)
        port->sendRetryReq();
}

void
MultiPortScratchpad::checkDrained()
{
    if (drainState() != DrainState::Draining)
        return;
    for (auto &port : ports) {
        if (!port->responses.empty())
            return;
    }
    DPRINTF(Drain, "Draining of MultiPortScratchpad complete\n");
    signalDrainDone();
}

DrainState
MultiPortScratchpad::drain()
{
    for (auto &port : ports) {
        if (!port->responses.empty())
            return DrainState::Draining;
    }
    return DrainState::Drained;
}

Port &
MultiPortScratchpad::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port")
        return AbstractMemory::getPort(if_name, idx);
    fatal_if(idx < 0 || idx >= (PortID)ports.size(),
             "%s: no port[%d]", name(), idx);
    return *ports[idx];
}

MultiPortScratchpad::MemoryPort::MemoryPort(const std::string &_name,
                                            MultiPortScratchpad &_mem,
                                            PortID _id)
    : ResponsePort(_name), mem(_mem), id(_id),
      sendEvent([this]{ mem.sendResponses(*this); }, _name + ".send")
{ }

AddrRangeList
MultiPortScratchpad::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

Tick
MultiPortScratchpad::MemoryPort::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");
    mem.access(pkt);
    return mem.cyclesToTicks(mem.latency);
}

Tick
MultiPortScratchpad::MemoryPort::recvAtomicBackdoor(
        PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    Tick lat = recvAtomic(pkt);
    mem.getBackdoor(_backdoor);
    return lat;
}

void
MultiPortScratchpad::MemoryPort::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(mem.name());
    mem.functionalAccess(pkt);
    bool done = false;
    for (auto &port : mem.ports) {
        for (auto &d : port->responses) {
            if ((done = pkt->trySatisfyFunctional(d.pkt)))
                break;
        }
        if (done)
            break;
    }
    pkt->popLabel();
}

void
MultiPortScratchpad::MemoryPort::recvMemBackdoorReq(
        const MemBackdoorReq &req, MemBackdoorPtr &backdoor)
{
    mem.getBackdoor(backdoor);
}

bool
MultiPortScratchpad::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return mem.recvTimingReq(pkt, *this);
}

void
MultiPortScratchpad::MemoryPort::recvRespRetry()
{
    assert(respBlocked);
    respBlocked = false;
    mem.sendResponses(*this);
}

MultiPortScratchpad::ScratchpadStats::ScratchpadStats(
        MultiPortScratchpad &_mem)
    : statistics::Group(&_mem), mem(_mem),
      ADD_STAT(loads, statistics::units::Count::get(),
               "Reads accepted"),
      ADD_STAT(stores, statistics::units::Count::get(),
               "Writes accepted"),
      ADD_STAT(loadRefusals, statistics::units::Count::get(),
               "Reads refused: the cycle's load ports were taken"),
      ADD_STAT(storeRefusals, statistics::units::Count::get(),
               "Writes refused: the cycle's store ports were taken"),
      ADD_STAT(accessRefusals, statistics::units::Count::get(),
               "Reads and writes refused with their kind's ports free: "
               "the cycle's access ports were taken"),
      ADD_STAT(loadsPerCycle, statistics::units::Count::get(),
               "Reads accepted per cycle, over cycles with any access"),
      ADD_STAT(storesPerCycle, statistics::units::Count::get(),
               "Writes accepted per cycle, over cycles with any access"),
      ADD_STAT(accessesPerCycle, statistics::units::Count::get(),
               "Reads and writes accepted per cycle, over cycles with any "
               "access")
{
}

void
MultiPortScratchpad::ScratchpadStats::regStats()
{
    statistics::Group::regStats();
    loadsPerCycle.init(0, mem.loadPorts, 1);
    storesPerCycle.init(0, mem.storePorts, 1);
    accessesPerCycle.init(0, mem.accessPorts, 1);
}

} // namespace memory
} // namespace gem5
