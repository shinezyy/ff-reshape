/*
 * Copyright (c) 2013 - 2015 ARM Limited
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
 *
 * Authors: Radhika Jagtap
 *          Andreas Hansson
 *          Thomas Grass
 */

/**
 * @file This file describes a trace component which is a cpu probe listener
 * used to generate elastic cpu traces. It registers listeners to probe points
 * in the fetch, rename, iew and commit stages of the O3CPU. It processes the
 * dependency graph of the cpu execution and writes out a protobuf trace. It
 * also generates a protobuf trace of the instruction fetch requests.
 */

#ifndef __CPU_O3_PROBE_BRANCH_TRACE_HH__
#define __CPU_O3_PROBE_BRANCH_TRACE_HH__

#include <proto/branch_outcome.pb.h>

#include <set>
#include <unordered_map>
#include <utility>

#include "params/BranchTrace.hh"
#include "proto/packet.pb.h"
#include "proto/protoio.hh"
#include "sim/sim_object.hh"

/**
 * The elastic trace is a type of probe listener and listens to probe points
 * in multiple stages of the O3CPU. The notify method is called on a probe
 * point typically when an instruction successfully progresses through that
 * stage.
 *
 * As different listener methods mapped to the different probe points execute,
 * relevant information about the instruction, e.g. timestamps and register
 * accesses, are captured and stored in temporary data structures. When the
 * instruction progresses through the commit stage, the timing as well as
 * dependency information about the instruction is finalised and encapsulated in
 * a struct called TraceInfo. TraceInfo objects are collected in a list instead
 * of writing them out to the trace file one a time. This is required as the
 * trace is processed in chunks to evaluate order dependencies and computational
 * delay in case an instruction does not have any register dependencies. By this
 * we achieve a simpler algorithm during replay because every record in the
 * trace can be hooked onto a record in its past. The trace is written out as
 * a protobuf format output file.
 *
 * The output trace can be read in and played back by the TraceCPU.
 */
class BranchTrace : public SimObject
{

  public:

    /** Constructor */
    BranchTrace(const BranchTraceParams *params);

    /** Protobuf output stream for branch trace. */
    ProtoOutputStream* branchTraceStream;

    /** Returns the name of the trace probe listener. */
    const std::string name() const;

    const std::string _name;

    void writeRecord(uint32_t branch_count, bool taken, Addr branch, Addr target);
    /**
     * Process any outstanding trace records, flush them out to the protobuf
     * output streams and delete the streams at simulation exit.
     */
    void flushTraces();

};
#endif//__CPU_O3_PROBE_BRANCH_TRACE_HH__
