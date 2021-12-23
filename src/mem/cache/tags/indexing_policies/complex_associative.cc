/*
 * Copyright (c) 2018 Inria
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
 * Adapted from Original Implementation of Indexing Policy by Authors: Daniel Carvalho
 */

/**
 * @file
 * Definitions of a associative indexing policy simulating the intel complex addressing.
 */
/* Author: Chuanqi Zhang.
*/

#include "mem/cache/tags/indexing_policies/complex_associative.hh"

#include "base/bitfield.hh"
#include "base/intmath.hh"
#include "base/logging.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"

ComplexAssociative::ComplexAssociative(const Params &p)
    : BaseIndexingPolicy(p), complex_bits(ceilLog2(p.num_slices)),
    mem_size(p.mem_size),map_size(p.mem_size>>setShift)
{
  initIndexing();
}

void
ComplexAssociative::initIndexing()
{
  complex_map_vec.resize(map_size);
  for (uint64_t i = 0; i < map_size; i++)
  {
    complex_map_vec[i]=hash(i);
  }
}

uint32_t
ComplexAssociative::hash(const Addr tag) const
{
  // const uint64_t slice_addr_mask[3] = {
  //   0x1b5f575440,
  //   0x2eb5faa880,
  //   0x3cccc93100
  // };
  const uint64_t slice_mask[3] = {
    0x6d7d5d51,
    0xbad7eaa2,
    0xf33324c4
  };

  Addr temp_tag = tag;

  for (size_t i = 0; i < complex_bits ; i++)
  {
    uint8_t xor_bit = popCount(tag&slice_mask[i])&1;
    temp_tag = insertBits<Addr,uint8_t>(temp_tag,i,xor_bit);
  }

  return temp_tag & setMask;
}

uint32_t
ComplexAssociative::extractSet(const Addr addr) const
{

  Addr line_addr = addr >> setShift;
  Addr line_index = line_addr % map_size;

  return  complex_map_vec[line_index];
}

std::vector<ReplaceableEntry*>
ComplexAssociative::getPossibleEntries(const Addr addr) const
{
  return sets[extractSet(addr)];
}
