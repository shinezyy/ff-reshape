/*
 * Copyright (c) 2012-2014 ARM Limited
 * All rights reserved.
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
 * Copyright (c) 2003-2005,2014 The Regents of The University of Michigan
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
 * @file
 * Definitions of a conventional tag store.
 */

#include "mem/cache/tags/base_set_assoc.hh"

#include <sstream>
#include <string>

#include "base/intmath.hh"
#include "sim/core.hh"

BaseSetAssoc::BaseSetAssoc(const Params &p)
    :BaseTags(p), allocAssoc(p.assoc), blks(p.size / p.block_size),
     sequentialAccess(p.sequential_access),
     replacementPolicy(p.replacement_policy),
     dataWaySrlFmt(p.data_way_srl_fmt),
     tagWaySrlFmt(p.tag_way_srl_fmt),
     dataBankSrlFmt(p.data_bank_srl_fmt),
     tagBankSrlFmt(p.tag_bank_srl_fmt),
     numDataBanks(p.num_data_banks),
     numTagBanks(p.num_tag_banks),
     numDataSRAMBlocks(p.num_data_sram_blocks),
     cacheLevel(p.cache_level),
     cacheName(p.cache_name),
     beatSize(p.beatSize)
{
    // There must be a indexing policy
    fatal_if(!p.indexing_policy, "An indexing policy is required");

    // Check parameters
    if (blkSize < 4 || !isPowerOf2(blkSize)) {
        fatal("Block size must be at least 4 and a power of 2");
    }
}

void
BaseSetAssoc::tagsInit()
{
    // Initialize all blocks
    for (unsigned blk_index = 0; blk_index < numBlocks; blk_index++) {
        // Locate next cache block
        CacheBlk* blk = &blks[blk_index];

        // Link block to indexing policy
        indexingPolicy->setEntry(blk, blk_index);

        // Associate a data chunk to the block
        blk->data = &dataBlks[blkSize*blk_index];

        // Associate a replacement data entry to the block
        blk->replacementData = replacementPolicy->instantiateEntry();
    }
}

void
BaseSetAssoc::invalidate(CacheBlk *blk)
{
    BaseTags::invalidate(blk);

    // Decrease the number of tags in use
    stats.tagsInUse--;

    // Invalidate replacement data
    replacementPolicy->invalidate(blk->replacementData);
}

void
BaseSetAssoc::moveBlock(CacheBlk *src_blk, CacheBlk *dest_blk)
{
    BaseTags::moveBlock(src_blk, dest_blk);

    // Since the blocks were using different replacement data pointers,
    // we must touch the replacement data of the new entry, and invalidate
    // the one that is being moved.
    replacementPolicy->invalidate(src_blk->replacementData);
    replacementPolicy->reset(dest_blk->replacementData);
}

BaseSetAssoc::FDMap
BaseSetAssoc::createFDMap(std::string prefix, int way_srl_fmt,
        int bank_srl_fmt, unsigned num_banks, unsigned num_sram_blocks)
{
    // data name format:
    // |way org|set bank org|bank id|[way id]|block id|
    // | bank_name                  |way name|block name|
    FDMap fd_map;
    for (unsigned b = 0; b < num_banks; b++) {
        std::stringstream ss;
        ss << prefix << "_"
           << Enums::WaySerializeFormatStrings[way_srl_fmt] << "_"
           << Enums::BankSerializeFormatStrings[bank_srl_fmt]
           << "_bank" << b;
        std::string bank_name = ss.str();
        for (unsigned bl = 0; bl < num_sram_blocks; bl++) {
            std::stringstream block_stream;
            block_stream << "_block" << bl << ".txt";
            std::string block_name = block_stream.str();
            if (way_srl_fmt == Enums::WaySerializeFormat::splitway) {
                for (unsigned w = 0; w < allocAssoc; w++) {
                    std::stringstream way_stream;
                    way_stream << "_way" << w;
                    std::string way_name = way_stream.str();
                    std::string whole = bank_name + way_name + block_name;
                    // std::cerr << whole << std::endl;
                    auto fd = fopen(whole.c_str(), "w");
                    fd_map[{w, b, bl}] = fd;
                    dumpFiles.insert(fd);
                }
            } else {
                std::string whole = bank_name + block_name;
                // std::cerr << whole << std::endl;
                auto fd = fopen(whole.c_str(), "w");
                dumpFiles.insert(fd);
                for (unsigned w = 0; w < allocAssoc; w++) {
                    fd_map[{w, b, bl}] = fd;
                }
            }
        }
    }
    return fd_map;
}

std::pair<Addr, CacheBlk*>
BaseSetAssoc::getPhyAddr(int way_fmt, int bank_fmt, unsigned bank_id,
        unsigned way_id, unsigned set_id, unsigned block_id)
{

    const std::vector<ReplaceableEntry*> entries = indexingPolicy->getPossibleEntries(set_id * blkSize);
    for (auto &e: entries) {
        if (e->getWay() == way_id) {
        }
    }
    panic("Unexpected way");
}

void
BaseSetAssoc::dumpStates()
{
    if (cacheLevel < 1) {
        return;
    }

    char prefix[128];
    sprintf(prefix, "./non-trivial-caches/l%i_%s_tag", cacheLevel, cacheName.c_str());
    tagFDMap = createFDMap(std::string(prefix), tagWaySrlFmt, tagBankSrlFmt, numTagBanks, numTagSRAMBlocks);

    sprintf(prefix, "./trivial-caches/l%i_%s_tag", cacheLevel, cacheName.c_str());
    trivialTagFDMap = createFDMap(std::string(prefix), tagWaySrlFmt, tagBankSrlFmt, numTagBanks, numTagSRAMBlocks);

    sprintf(prefix, "./non-trivial-caches/l%i_%s_data", cacheLevel, cacheName.c_str());
    dataFDMap = createFDMap(std::string(prefix), dataWaySrlFmt, dataBankSrlFmt, numDataBanks, numDataSRAMBlocks);

    sprintf(prefix, "./non-trivial-caches/debug_l%i_%s_data", cacheLevel, cacheName.c_str());
    dataDebugFDMap = createFDMap(std::string(prefix), dataWaySrlFmt, dataBankSrlFmt, numDataBanks, numDataSRAMBlocks);

    unsigned num_sets = numBlocks / allocAssoc;

    assert(numTagSRAMBlocks == 1); // not seen yet
    unsigned phy_blk_size = blkSize / numDataSRAMBlocks;
    unsigned offset_bits = ceilLog2(blkSize);
    unsigned set_bits = ceilLog2(num_sets);
    unsigned tag_bits = 40 - set_bits - offset_bits;

    const uint8_t *real_mem = get_nemu_pmem();

    Addr dir_read = cacheLevel > 1 ? 0x4 : 0x2; // 0x4 == 0.0.10.0 == Truck
    Addr dir_write = cacheLevel > 1 ? 0x2 : 0x0;  // 0x6 == 0.0.11.0 == Tip
    Addr dir_client = cacheLevel > 1 ? 0x1 : 0x0;  // 0x6 == 0.0.00.1 == has a inner client
    unsigned dir_bits = cacheLevel > 1 ? 5 : 2;
    auto total_bits = dir_bits + tag_bits;
    assert(blkSize % beatSize == 0);
    unsigned num_beats = blkSize / beatSize;
    if (num_beats > 1) {
        assert (numDataSRAMBlocks == 1);
    }
    std::cerr << "L" << cacheLevel << " cache: total bits: " << total_bits
              << " dir bits: " << dir_bits << " tag bits: " << tag_bits << std::endl;

    for (unsigned way = 0; way < allocAssoc; way++) {
        for (unsigned logic_set = 0; logic_set < num_sets; logic_set++) {
            assert(dataBankSrlFmt == tagBankSrlFmt); // It should not use different formats
            unsigned tag_phy_bank;
            if (dataBankSrlFmt == Enums::BankSerializeFormat::Senk) {
                tag_phy_bank = logic_set % numTagBanks;
            } else {
                panic("Not tested yet");
                tag_phy_bank = logic_set / numTagBanks;
            }
            const std::vector<ReplaceableEntry *> entries =
            indexingPolicy->getPossibleEntries(logic_set * blkSize);
            auto entry = getWay(way, entries);
            CacheBlk *blk = static_cast<CacheBlk*>(entry);
            auto tfd = getTagFD(tag_phy_bank, way, 0);
            auto trivial_tfd = getTrivialTagFD(tag_phy_bank, way, 0);

            // carefully tag format
            auto hex_len = (total_bits / 4) + (total_bits % 4 != 0);
            auto hex_mask = 0xf;
            Addr dir_payload = dir_read;
            if (blk->getTip()) {
                dir_payload |= dir_write;
            } else {
                dir_payload |= dir_client;
            }

            unsigned data_phy_bank;
            // std::cerr << "L" << cacheLevel << " cache\n";
            if (cacheLevel == 1) {
                data_phy_bank = logic_set % numDataBanks;
            } else if (cacheLevel == 2) {
                data_phy_bank = (logic_set & 0x1) << 1;
            } else {
                assert(cacheLevel == 3);
                unsigned outer_bits = logic_set & 0x3;
                unsigned inner_bit_high = (logic_set >> 2) & 0x1;
                // data_phy_bank = outer_bits << 1 | inner_bit_high;
                data_phy_bank = outer_bits | (inner_bit_high << 3);
            }

            auto tag_payload = blk->isValid() ? blk->getTag() | (dir_payload << tag_bits) : 0;

            // big endian for readmemh:
            // dump tag
            for (int shamt = (hex_len - 1) * 4; shamt >= 0; shamt -= 4) {
                fprintf(tfd, "%lx", (tag_payload >> shamt) & hex_mask);
            }
            fprintf(tfd, "%c", '\n');
            fprintf(trivial_tfd, "%016lx\n", 0L);
            // dump data
            if (numDataSRAMBlocks > 1) {
                for (unsigned bl = 0; bl < numDataSRAMBlocks; bl++) {
                    Addr phy_blk_addr = ((blk->getTag() * num_sets) + logic_set) * blkSize + bl * phy_blk_size;
                    // std::cerr << "Phy addr:" << std::hex << phy_blk_addr << ", tag: " << blk->getTag()
                    // << ", set: " << logic_set << ", offset: " << bl*phy_blk_size << ";  bank: " << data_phy_bank
                    // << ", way: " << blk->getWay() << std::endl;
                    auto dfd = getDataFD(data_phy_bank, way, bl);
                    for (unsigned i = 0; i < phy_blk_size; i += 8) {
                        unsigned offset = phy_blk_size - 8 - i;
                        uint64_t data_payload = blk->isValid() ?
                            *((uint64_t *)&real_mem[phy_blk_addr + offset - 0x80000000]): 0;
                        fprintf(dfd, "%016lx", data_payload);
                    }
                    fprintf(dfd, "%c", '\n');
                }
            } else {
                for (unsigned beat = 0; beat < num_beats; beat++) {
                    unsigned data_phy_bank_x;
                    if (cacheLevel == 1) {
                        data_phy_bank_x = data_phy_bank * num_beats + beat;
                    } else if (cacheLevel == 2){
                        data_phy_bank_x = data_phy_bank | beat;
                    } else {
                        assert(cacheLevel == 3);
                        data_phy_bank_x = data_phy_bank | (beat << 2);
                    }
                    // std::cerr << "Bank: " << data_phy_bank_x << std::endl;
                    auto dfd = getDataFD(data_phy_bank_x, way, 0);
                    Addr beat_addr = ((blk->getTag() * num_sets) + logic_set) * blkSize + beat * beatSize;
                    for (unsigned i = 0; i < beatSize; i += 8) {
                        unsigned offset = beatSize - 8 - i;
                        uint64_t data_payload = blk->isValid() ?
                            *((uint64_t *)&real_mem[beat_addr + offset - 0x80000000]) : 0;
                        fprintf(dfd, "%016lx", data_payload);
                    }
                    fprintf(dfd, "%c", '\n');

                    auto ddfd = getDataDebugFD(data_phy_bank_x, way, 0);
                    fprintf(ddfd, "%016lx, way: %u, logic set: %u, beat: %u\n", beat_addr, way, logic_set, beat);
                }
            }
        }
    }
    for (auto &fd: dumpFiles) {
        fclose(fd);
    }
}

ReplaceableEntry *
BaseSetAssoc::getWay(unsigned way, const std::vector<ReplaceableEntry *> &set_entries) const
{
    for (auto entry : set_entries) {
        if (way == entry->getWay()) {
            return entry;
        }
    }
    panic("Way not found\n");
}

