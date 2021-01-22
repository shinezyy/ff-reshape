/*
 * Copyright 2020 Google Inc.
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

#ifndef __ARCH_RISCV_SE_WORKLOAD_HH__
#define __ARCH_RISCV_SE_WORKLOAD_HH__

#include "arch/riscv/registers.hh"
#include "params/RiscvSEWorkload.hh"
#include "sim/se_workload.hh"
#include "sim/syscall_abi.hh"
#include "sim/syscall_desc.hh"

namespace RiscvISA
{

class SEWorkload : public ::SEWorkload
{
  public:
    using Params = RiscvSEWorkloadParams;

  protected:
    const Params &_params;

  public:
    const Params &params() const { return _params; }

    SEWorkload(const Params &p) : ::SEWorkload(p), _params(p) {}

    ::Loader::Arch getArch() const override { return ::Loader::Riscv64; }

    //FIXME RISCV needs to handle 64 bit arguments in its 32 bit ISA.
    struct SyscallABI : public GenericSyscallABI64
    {
        static const std::vector<int> ArgumentRegs;
    };
};

} // namespace RiscvISA

namespace GuestABI
{

template <>
struct Result<RiscvISA::SEWorkload::SyscallABI, SyscallReturn>
{
    static void
    store(ThreadContext *tc, const SyscallReturn &ret)
    {
        if (ret.suppressed() || ret.needsRetry())
            return;

        if (ret.successful()) {
            // no error
            tc->setIntReg(RiscvISA::ReturnValueReg, ret.returnValue());
        } else {
            // got an error, return details
            tc->setIntReg(RiscvISA::ReturnValueReg, ret.encodedValue());
        }
    }
};

} // namespace GuestABI

#endif // __ARCH_RISCV_SE_WORKLOAD_HH__
