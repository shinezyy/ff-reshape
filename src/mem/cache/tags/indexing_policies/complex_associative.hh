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
 * Declaration of a associative indexing policy simulating the intel complex addressing.
 */

/* Author: Chuanqi Zhang.
*/


#ifndef __MEM_CACHE_INDEXING_POLICIES_COMPLEX_ASSOCIATIVE_HH__
#define __MEM_CACHE_INDEXING_POLICIES_COMPLEX_ASSOCIATIVE_HH__

#include <vector>

#include "mem/cache/tags/indexing_policies/base.hh"
#include "params/ComplexAssociative.hh"

class ReplaceableEntry;

/**
 * A complex associative indexing policy.
 * @sa  \ref gem5MemorySystem "gem5 Memory System"
 *
 * The complex indexing policy has a variable mapping based on a hash function,
 * so a value x can be mapped to different slices.
 */
class ComplexAssociative : public BaseIndexingPolicy
{
  private:

    /**
     * The complex bits offset in set index
     */
    const int complex_bits;

    /**
     * size of memory, used to pre-calculate address mapping
     */
    uint64_t mem_size;

    /**
     *  size_of the pre-computed complex address mapping
     */
    uint64_t map_size;

    /**
     * the pre-computed complex address mapping
     */
    std::vector<uint32_t> complex_map_vec;

    /**
     * The hash function itself. Uses the hash function H, as described in
     * "Reverse Engineering Intel Last-Level Cache Complex Addressing Using
     * Performance Counters", from Maurice et al.
     *
     * @param tag The extended full tag to be hashed.
     * @param The hashed index.
     */
    uint32_t hash(const Addr tag) const;

    /* Initializes the cache index mapping.
     */
    void initIndexing();

  public:
    /** Convenience typedef. */
     typedef ComplexAssociativeParams Params;

    /**
     * Construct and initialize this policy.
     */
    ComplexAssociative(const Params &p);

    /**
     * Destructor.
     */
    ~ComplexAssociative() {};

    /**
     * Find all possible entries for insertion and replacement of an address.
     * Should be called immediately before ReplacementPolicy's findVictim()
     * not to break cache resizing.
     *
     * @param addr The addr to a find possible entries for.
     * @return The possible entries.
     */
    std::vector<ReplaceableEntry*> getPossibleEntries(const Addr addr) const
                                                                   override;

    /**
     * Generates the tag as line-addr, from the given byte-addr.
     *
     * @param addr The address to get the tag from.
     * @return The tag (line-addr) of the address.
     */
    Addr extractTag(const Addr addr) const override
    {
      return (addr >> setShift);
    }

    /**
     * Use pre-compute hash function result to calculate address' set.
     *
     * @param addr The address to calculate the set for.
     * @return The set index for given combination of address.
     */
    uint32_t extractSet(const Addr addr) const override;

    /**
     * Regenerate an entry's address from its tag and assigned set and way.
     * Uses the inverse of the skewing function.
     *
     * @param tag The tag bits.
     * @param entry The entry.
     * @return the entry's address.
     */
    Addr regenerateAddr(const Addr tag, const ReplaceableEntry* entry) const
                                                                   override
    {
      Addr addr = tag << setShift;
      return addr;
    }
};

#endif //__MEM_CACHE_INDEXING_POLICIES_SKEWED_ASSOCIATIVE_HH__
