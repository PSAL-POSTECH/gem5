# Copyright (c) 2026 POSTECH PSAL
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.objects.AbstractMemory import *
from m5.params import *


class MultiPortScratchpad(AbstractMemory):
    # A scratchpad with a number of load and store ports per cycle of its
    # clock: any address, a fixed latency, refused only when this cycle's
    # ports of the access's kind are taken (retried at the next edge).
    type = "MultiPortScratchpad"
    cxx_header = "mem/multiport_scratchpad.hh"
    cxx_class = "gem5::memory::MultiPortScratchpad"

    port = VectorResponsePort(
        "Requestors: each port is served alike, the per-cycle ports are shared"
    )
    load_ports = Param.Unsigned(3, "Reads accepted per cycle")
    store_ports = Param.Unsigned(2, "Writes accepted per cycle")
    latency = Param.Cycles(1, "Request to response, in cycles of clk_domain")
