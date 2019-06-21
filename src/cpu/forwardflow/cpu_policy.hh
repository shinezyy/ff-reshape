/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
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
 *
 * Authors: Kevin Lim
 */

#ifndef __CPU_FF_CPU_POLICY_HH__
#define __CPU_FF_CPU_POLICY_HH__

#include "cpu/forwardflow/arch_state.hh"
#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/commit.hh"
#include "cpu/forwardflow/dataflow_queue.hh"
#include "cpu/forwardflow/decode.hh"
#include "cpu/forwardflow/fetch.hh"
#include "cpu/forwardflow/free_list.hh"
#include "cpu/forwardflow/fu_wrapper.hh"
#include "cpu/forwardflow/iew.hh"
#include "cpu/forwardflow/inst_queue.hh"
#include "cpu/forwardflow/lsq.hh"
#include "cpu/forwardflow/lsq_unit.hh"
#include "cpu/forwardflow/mem_dep_unit.hh"
#include "cpu/forwardflow/regfile.hh"
#include "cpu/forwardflow/rename.hh"
#include "cpu/forwardflow/rename_map.hh"
#include "cpu/forwardflow/rob.hh"
#include "cpu/forwardflow/store_set.hh"

/**
 * Struct that defines the key classes to be used by the CPU.  All
 * classes use the typedefs defined here to determine what are the
 * classes of the other stages and communication buffers.  In order to
 * change a structure such as the IQ, simply change the typedef here
 * to use the desired class instead, and recompile.  In order to
 * create a different CPU to be used simultaneously with this one, see
 * the alpha_impl.hh file for instructions.
 */
namespace FF{

template<class Impl>
struct SimpleCPUPolicy
{
    /** Typedef for the freelist of registers. */
    typedef FF::UnifiedFreeList FreeList;
    /** Typedef for the rename map. */
    typedef FF::UnifiedRenameMap RenameMap;
    /** Typedef for the ROB. */
    typedef FF::ROB<Impl> ROB;
    /** Typedef for the instruction queue/scheduler. */
    typedef FF::InstructionQueue<Impl> IQ;
    /** Typedef for the memory dependence unit. */
    typedef FF::MemDepUnit<StoreSet, Impl> MemDepUnit;
    /** Typedef for the LSQ. */
    typedef FF::LSQ<Impl> LSQ;
    /** Typedef for the thread-specific LSQ units. */
    typedef FF::LSQUnit<Impl> LSQUnit;

    /** Typedef for fetch. */
    typedef FF::DefaultFetch<Impl> Fetch;
    /** Typedef for decode. */
    typedef FF::DefaultDecode<Impl> Decode;
    /** Typedef for rename. */
    typedef FF::DefaultRename<Impl> Rename;
    /** Typedef for Issue/Execute/Writeback. */
    typedef FF::DefaultIEW<Impl> IEW;
    /** Typedef for commit. */
    typedef FF::DefaultCommit<Impl> Commit;

    /** The struct for communication between fetch and decode. */
    typedef FF::DefaultFetchDefaultDecode<Impl> FetchStruct;

    /** The struct for communication between decode and rename. */
    typedef FF::DefaultDecodeDefaultRename<Impl> DecodeStruct;

    /** The struct for communication between rename and IEW. */
    typedef FF::DefaultRenameDefaultIEW<Impl> RenameStruct;

    /** The struct for communication between IEW and commit. */
    typedef FF::DefaultIEWDefaultCommit<Impl> IEWStruct;

    /** The struct for communication within the IEW stage. */
    typedef FF::IssueStruct<Impl> IssueStruct;

    /** The struct for all backwards communication. */
    typedef FF::TimeBufStruct<Impl> TimeStruct;

    /** The struct for dq timing communication. */
    typedef FF::DQOut<Impl> DQStruct;

    typedef FF::DataflowQueueBank<Impl> DataflowQueueBank;

    typedef FF::DataflowQueues<Impl> DataflowQueues;

    typedef FF::FUWrapper<Impl> FUWrapper;

    /** The struct for communication between decode and allocation. */
    typedef FF::Decode2Allocation<Impl> FFDecodeStruct;

    /** The struct for communication between allocation and diewc. */
    typedef FF::Allocation2DIEWC<Impl> FFAllocationStruct;

    typedef FF::DIEWC2DIEWC<Impl> DIEWC2DIEWC;

    typedef FF::ArchState<Impl> ArchState;
};

}
#endif //__CPU_FF_CPU_POLICY_HH__
