#include <internals/cache.h>
#include <internals/champsim.h>
#include <internals/fnv.h>
#
#include <internals/simulator.hh>

// RANDOM champsim_rand(champsim_seed);

uint32_t helper::PAGE_TABLE_LATENCY = 0, helper::SWAP_LATENCY = 0;

uint8_t helper::MAX_INSTR_DESTINATIONS = NUM_INSTR_DESTINATIONS,
        helper::knob_cloudsuite = 0, helper::knob_low_bandwidth = 0;

uint64_t helper::last_drc_read_mode, helper::last_drc_write_mode,
    helper::drc_blocks, helper::champsim_seed;

queue<uint64_t> helper::page_queue;

map<uint64_t, uint64_t> helper::page_table, helper::inverse_table,
    helper::recent_page, helper::unique_cl[NUM_CPUS];

uint64_t helper::previous_ppage, helper::num_adjacent_page,
    helper::num_cl[NUM_CPUS], helper::allocated_pages,
    helper::num_page[NUM_CPUS], helper::minor_fault[NUM_CPUS],
    helper::major_fault[NUM_CPUS];

RANDOM helper::champsim_rand(0ULL);

int lg2(int n) {
    int i, m = n, c = -1;
    for (i = 0; m; i++) {
        m /= 2;
        c++;
    }
    return c;
}

uint64_t rotl64(uint64_t n, unsigned int c) {
    const unsigned int mask = (CHAR_BIT * sizeof(n) - 1);

    assert((c <= mask) && "rotate by type width or more");
    c &= mask;  // avoid undef behaviour with NDEBUG.  0 overhead for most types
                // / compilers
    return (n << c) | (n >> ((-c) & mask));
}

uint64_t rotr64(uint64_t n, unsigned int c) {
    const unsigned int mask = (CHAR_BIT * sizeof(n) - 1);

    assert((c <= mask) && "rotate by type width or more");
    c &= mask;  // avoid undef behaviour with NDEBUG.  0 overhead for most types
                // / compilers
    return (n >> c) | (n << ((-c) & mask));
}

uint64_t va_to_pa(uint32_t cpu, uint64_t instr_id, uint64_t va,
                  uint64_t unique_vpage, uint8_t is_code) {
#ifdef SANITY_CHECK
    if (va == 0) assert(0);
#endif

    uint8_t swap = 0;
    uint64_t high_bit_mask = rotr64(cpu, lg2(NUM_CPUS)),
             unique_va = va | high_bit_mask;
    // uint64_t vpage = unique_va >> LOG2_PAGE_SIZE,
    uint64_t vpage = unique_vpage | high_bit_mask,
             voffset = unique_va & ((1 << LOG2_PAGE_SIZE) - 1);

    // smart random number generator
    uint64_t random_ppage;

    map<uint64_t, uint64_t>::iterator pr = helper::page_table.begin();
    map<uint64_t, uint64_t>::iterator ppage_check =
        helper::inverse_table.begin();

    O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(cpu);

    // check unique cache line footprint
    map<uint64_t, uint64_t>::iterator cl_check =
        helper::unique_cl[cpu].find(unique_va >> LOG2_BLOCK_SIZE);
    if (cl_check == helper::unique_cl[cpu]
                        .end()) {  // we've never seen this cache line before
        helper::unique_cl[cpu].insert(
            make_pair(unique_va >> LOG2_BLOCK_SIZE, 0));
        helper::num_cl[cpu]++;
    } else
        cl_check->second++;

    pr = helper::page_table.find(vpage);
    if (pr == helper::page_table.end()) {  // no VA => PA translation found

        if (helper::allocated_pages >= DRAM_PAGES) {  // not enough memory

            // TODO: elaborate page replacement algorithm
            // here, ChampSim randomly selects a page that is not recently used
            // and we only track 32K recently accessed pages
            uint8_t found_NRU = 0;
            uint64_t NRU_vpage = 0;  // implement it
            map<uint64_t, uint64_t>::iterator pr2 = helper::recent_page.begin();
            for (pr = helper::page_table.begin();
                 pr != helper::page_table.end(); pr++) {
                NRU_vpage = pr->first;
                if (helper::recent_page.find(NRU_vpage) ==
                    helper::recent_page.end()) {
                    found_NRU = 1;
                    break;
                }
            }
#ifdef SANITY_CHECK
            if (found_NRU == 0) assert(0);

            if (pr == helper::page_table.end()) assert(0);
#endif
            DP(if (warmup_complete[cpu]) {
                cout << "[SWAP] update page table NRU_vpage: " << hex
                     << pr->first << " new_vpage: " << vpage
                     << " ppage: " << pr->second << dec << endl;
            });

            // update page table with new VA => PA mapping
            // since we cannot change the key value already inserted in a map
            // structure, we need to erase the old node and add a new node
            uint64_t mapped_ppage = pr->second;
            helper::page_table.erase(pr);
            helper::page_table.insert(make_pair(vpage, mapped_ppage));

            // update inverse table with new PA => VA mapping
            ppage_check = helper::inverse_table.find(mapped_ppage);
#ifdef SANITY_CHECK
            if (ppage_check == helper::inverse_table.end()) assert(0);
#endif
            ppage_check->second = vpage;

            DP(if (warmup_complete[cpu]) {
                cout << "[SWAP] update inverse table NRU_vpage: " << hex
                     << NRU_vpage << " new_vpage: ";
                cout << ppage_check->second << " ppage: " << ppage_check->first
                     << dec << endl;
            });

            // update page_queue
            helper::page_queue.pop();
            helper::page_queue.push(vpage);

            // invalidate corresponding vpage and ppage from the cache hierarchy
            curr_cpu->ITLB.invalidate_entry(NRU_vpage);
            curr_cpu->DTLB.invalidate_entry(NRU_vpage);
            curr_cpu->STLB.invalidate_entry(NRU_vpage);
            for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
                uint64_t cl_addr = (mapped_ppage << 6) | i;
                // curr_cpu->L1I.invalidate_entry(cl_addr);
                // curr_cpu->L1D.invalidate_entry(cl_addr);
                // curr_cpu->L2C.invalidate_entry(cl_addr);
                // uncore.LLC.invalidate_entry(cl_addr);
            }

            // swap complete
            swap = 1;
        } else {
            uint8_t fragmented = 0;
            if (helper::num_adjacent_page > 0)
                random_ppage = ++helper::previous_ppage;
            else {
                random_ppage = helper::champsim_rand.draw_rand();
                fragmented = 1;
            }

            // encoding cpu number
            // this allows ChampSim to run homogeneous multi-programmed
            // workloads without VA => PA aliasing (e.g., cpu0: astar  cpu1:
            // astar  cpu2: astar  cpu3: astar...) random_ppage &=
            // (~((NUM_CPUS-1)<< (32-LOG2_PAGE_SIZE))); random_ppage |=
            // (cpu<<(32-LOG2_PAGE_SIZE));

            while (1) {  // try to find an empty physical page number
                ppage_check = helper::inverse_table.find(
                    random_ppage);  // check if this page can be allocated
                if (ppage_check !=
                    helper::inverse_table
                        .end()) {  // random_ppage is not available
                    DP(if (warmup_complete[cpu]) {
                        cout << "vpage: " << hex << ppage_check->first
                             << " is already mapped to ppage: " << random_ppage
                             << dec << endl;
                    });

                    if (helper::num_adjacent_page > 0) fragmented = 1;

                    // try one more time
                    random_ppage = helper::champsim_rand.draw_rand();

                    // encoding cpu number
                    // random_ppage &= (~((NUM_CPUS-1)<<(32-LOG2_PAGE_SIZE)));
                    // random_ppage |= (cpu<<(32-LOG2_PAGE_SIZE));
                } else
                    break;
            }

            // insert translation to page tables
            // printf("Insert  num_adjacent_page: %u  vpage: %lx  ppage: %lx\n",
            // num_adjacent_page, vpage, random_ppage);
            helper::page_table.insert(make_pair(vpage, random_ppage));
            helper::inverse_table.insert(make_pair(random_ppage, vpage));
            helper::page_queue.push(vpage);
            helper::previous_ppage = random_ppage;
            helper::num_adjacent_page--;
            helper::num_page[cpu]++;
            helper::allocated_pages++;

            // try to allocate pages contiguously
            if (fragmented) {
                helper::num_adjacent_page = 1 << (rand() % 10);
                DP(if (warmup_complete[cpu]) {
                    cout << "Recalculate num_adjacent_page: "
                         << num_adjacent_page << endl;
                });
            }
        }

        if (swap)
            helper::major_fault[cpu]++;
        else
            helper::minor_fault[cpu]++;
    } else {
        // printf("Found  vpage: %lx  random_ppage: %lx\n", vpage, pr->second);
    }

    pr = helper::page_table.find(vpage);
#ifdef SANITY_CHECK
    if (pr == helper::page_table.end()) assert(0);
#endif
    uint64_t ppage = pr->second;

    uint64_t pa = ppage << LOG2_PAGE_SIZE;
    pa |= voffset;

    DP(if (warmup_complete[cpu]) {
        cout << "[PAGE_TABLE] instr_id: " << instr_id << " vpage: " << hex
             << vpage;
        cout << " => ppage: " << (pa >> LOG2_PAGE_SIZE)
             << " vadress: " << unique_va << " paddress: " << pa << dec << endl;
    });

    // as a hack for code prefetching, code translations are magical and do not
    // pay these penalties if(!is_code) actually, just disable this stall
    // feature entirely if it's data, pay these penalties if (swap)
    //   stall_cycle[cpu] = (is_irreg_data) ? current_core_cycle[cpu] +
    //   SWAP_LATENCY : current_core_cycle[cpu];
    // else
    //   stall_cycle[cpu] = (is_irreg_data) ? current_core_cycle[cpu] +
    //   PAGE_TABLE_LATENCY : current_core_cycle[cpu];
    if (swap)
        curr_cpu->stall_cycle() =
            curr_cpu->current_core_cycle() + helper::SWAP_LATENCY;
    else
        curr_cpu->stall_cycle() =
            curr_cpu->current_core_cycle() + helper::PAGE_TABLE_LATENCY;

    // cout << "cpu: " << cpu << " allocated unique_vpage: " << hex <<
    // unique_vpage << " to ppage: " << ppage << dec << endl;

    return pa;
}

uint32_t folded_xor(uint64_t value, uint32_t num_folds) {
    assert(num_folds > 1);
    assert((num_folds & (num_folds - 1)) == 0); /* has to be power of 2 */
    uint32_t mask = 0;
    uint32_t bits_in_fold = 64 / num_folds;
    if (num_folds == 2) {
        mask = 0xffffffff;
    } else {
        mask = (1ul << bits_in_fold) - 1;
    }
    uint32_t folded_value = 0;
    for (uint32_t fold = 0; fold < num_folds; ++fold) {
        folded_value = folded_value ^ ((value >> (fold * bits_in_fold)) & mask);
    }
    return folded_value;
}

uint64_t jenkins_hash(uint64_t key) {
    // Robert Jenkins' 32 bit mix function
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    key = (key >> 3) * 2654435761;

    return key;
}

uint64_t fnv1a64(uint64_t key) {
    return FNV::fnv1a((void *)&key, sizeof(key));
}