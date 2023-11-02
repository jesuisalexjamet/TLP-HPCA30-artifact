#include <internals/simulator.hh>
#
#include <plugins/prefetchers/l1d_berti/helpers.hh>
#include <plugins/prefetchers/l1d_berti/l1d_berti.hh>
#
#include <boost/property_tree/json_parser.hpp>

#define CONTINUE_BURST
#define PREFETCH_FOR_LONG_REUSE

#define LINNEA
#define WARMUP_NEW_PAGES

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l1d_berti::l1d_berti() {}

cp::l1d_berti::l1d_berti(const cp::l1d_berti& o) : iprefetcher(o) {}

/**
 * Destructor of the class.
 */
cp::l1d_berti::~l1d_berti() {}

/**
 * @brief This method performs the actual operation of the prefetcher. This
 * method is meant to be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch
 * request.
 */
void cp::l1d_berti::operate(const cp::prefetch_request_descriptor& desc) {
    assert(desc.access_type == cc::cache::load ||
           desc.access_type == cc::cache::rfo);

    cache_accesses++;
    if (!desc.hit) cache_misses++;

    uint64_t line_addr = desc.addr >> LOG2_BLOCK_SIZE;
    uint64_t page_addr = line_addr >> L1D_PAGE_BLOCKS_BITS;
    uint64_t offset = line_addr & L1D_PAGE_OFFSET_MASK;
    uint64_t ip_index = desc.ip & L1D_IP_TABLE_INDEX_MASK;
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(desc.cpu);

    int last_berti = 0;
    int berti = 0;
    bool linnea_hits = false;
    bool first_access = false;
    bool full_access = false;
    int stride = 0;
    bool short_reuse = true;
    uint64_t count_reuse = 0;

    // Find the entry in the current page table
    uint64_t index = l1d_get_current_pages_entry(page_addr);

    bool recently_accessed = false;
    if (index < L1D_CURRENT_PAGES_TABLE_ENTRIES) {  // Hit in current page table
        recently_accessed =
            l1d_offset_requested_current_pages_table(index, offset);
    }

    if (index < L1D_CURRENT_PAGES_TABLE_ENTRIES  // Hit in current page table
        && l1d_current_pages_table[index].u_vector != 0) {  // Used before

        // Within the same page we always predict the same
        last_berti = l1d_current_pages_table[index].current_berti;
        berti = last_berti;

        // Update accessed block vector
        l1d_update_current_pages_table(index, offset);

        // Update berti
        if (desc.hit) {  // missed update it when resolved
            uint64_t latency = l1d_get_latency_latencies_table(index, offset);
            if (latency != 0) {
                // Find berti distance from pref_latency cycles before
                int berti[L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS];
                unsigned
                    saved_cycles[L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS];
                // cout << "Hit ";
                l1d_get_berti_prev_requests_table(
                    index, offset, latency, berti, saved_cycles,
                    curr_cpu->current_core_cycle());
                /* if (ip_index == 0x10f) { */
                /*   cout << "ADD BERTI HIT " << index << " <" << offset << ">
                 * "; */
                /*   for (int i = 0; berti[i] != 0; i++) { */
                /*     cout << berti[i] << " " << saved_cycles[i] << ", "; */
                /*   } */
                /*   cout << endl; */
                /* } */

                if (!recently_accessed) {  // If not accessed recently
                    l1d_add_berti_current_pages_table(index, berti,
                                                      saved_cycles);
                }
                // Eliminate a prev prefetch since it has been used // Better do
                // it on evict
                // l1d_reset_entry_latencies_table(index, offset);
            }
        }
    } else {  // First access to a new page

        first_access = true;

        // Find Berti and Linnea

        // Check IP table
        if (l1d_ip_table[ip_index]
                .current) {  // Here we check for Berti and Linnea

            int ip_pointer = l1d_ip_table[ip_index].berti_or_pointer;
            assert(ip_pointer < L1D_CURRENT_PAGES_TABLE_ENTRIES);
            // It will be a change of page for the IP

            // Get the last berti the IP is using and new berti to use
            last_berti = l1d_current_pages_table[ip_pointer].current_berti;
            berti = l1d_get_berti_current_pages_table(ip_pointer);

            // Get if all blocks for a potential burst were accessed
            full_access = l1d_all_last_berti_accessed_bit_vector(
                l1d_current_pages_table[ip_pointer].u_vector, berti);

            // Make the link (linnea)
            uint64_t last_page_addr =
                l1d_current_pages_table[ip_pointer].page_addr;

            /* if (ip_index == 0x28c) { cout << "LINK " << hex << last_page_addr
             * << " " << page_addr << " " << dec << ip_pointer << " " <<
             * l1d_count_bit_vector(l1d_current_pages_table[ip_pointer].u_vector)
             * << " "; */
            /*   l1d_print_bit_vector(l1d_current_pages_table[ip_pointer].u_vector);
             */
            /*   cout << endl; */
            /* } */

            count_reuse = l1d_count_bit_vector(
                l1d_current_pages_table[ip_pointer].u_vector);
            short_reuse = (count_reuse > LONG_REUSE_LIMIT);
            if (short_reuse) {
                if (berti > 0 && last_page_addr + 1 == page_addr) {
                    l1d_ip_table[ip_index].consecutive = true;
                } else if (berti < 0 && last_page_addr == page_addr + 1) {
                    l1d_ip_table[ip_index].consecutive = true;
                } else {  // Only add to record if not consecutive
                    l1d_ip_table[ip_index].consecutive = false;
                    l1d_add_record_pages_table(last_page_addr, page_addr);
                }
            } else {
                if (l1d_current_pages_table[ip_pointer].short_reuse) {
                    l1d_current_pages_table[ip_pointer].short_reuse = false;
                }
                uint64_t record_index =
                    l1d_get_entry_record_pages_table(last_page_addr);
                // if (ip_index == 0x28c) cout << "LONG REUSE SECOND " << hex <<
                // last_page_addr << "->" <<
                // l1d_get_entry_record_pages_table(last_page_addr) << " " <<
                // page_addr << "->" <<
                // l1d_get_entry_record_pages_table(page_addr) << dec << endl;
                if (record_index < L1D_RECORD_PAGES_TABLE_ENTRIES &&
                    !l1d_record_pages_table[record_index].short_reuse &&
                    l1d_record_pages_table[record_index].linnea == page_addr) {
                    stride = l1d_calculate_stride(
                        l1d_record_pages_table[record_index].last_offset,
                        offset);
                    // if (ip_index == 0x28c) cout << "LONG REUSE MATCH " << hex
                    // << last_page_addr << " " << page_addr << " " << dec <<
                    // l1d_record_pages_table[record_index].last_offset <<
                    // " " << offset << " " << stride << endl;
                }
                // if (ip_index == 0x28c) cout << "LONG REUSE ADD RECORD " <<
                // hex << last_page_addr << " " << page_addr << " " << dec <<
                // offset << " " << short_reuse << endl;

                if (!recently_accessed) {  // If not accessed recently
                    l1d_add_record_pages_table(last_page_addr, page_addr,
                                               offset, short_reuse);
                }
                // if (ip_index == 0x28c) cout << "LONG REUSE RECORDED " << hex
                // << last_page_addr << "->" <<
                // l1d_get_entry_record_pages_table(last_page_addr) << dec <<
                // endl;
            }

        } else {
            berti = l1d_ip_table[ip_index].berti_or_pointer;
        }

        if (index ==
            L1D_CURRENT_PAGES_TABLE_ENTRIES) {  // Miss in current page table

            // Not found (linnea did not work or was not used -- berti == 0)

            // Add new page entry evicting a previous one.
            index = l1d_evict_lru_current_page_entry();
            l1d_add_current_pages_table(index, page_addr);

        } else {  // First access, but linnea worked and blocks of the page have
                  // been prefetched
            linnea_hits = true;
        }

        // Update accessed block vector
        l1d_update_current_pages_table(index, offset);
    }

    // Set the new berti
    if (!recently_accessed) {  // If not accessed recently
        if (short_reuse) {
            l1d_current_pages_table[index].current_berti = berti;
        } else {
            l1d_current_pages_table[index].stride = stride;
        }
        l1d_current_pages_table[index].short_reuse = short_reuse;

        l1d_ip_table[ip_index].current = true;
        l1d_ip_table[ip_index].berti_or_pointer = index;
    }

    // Add the request in the history buffer
    if (l1d_find_prev_request_entry(index, offset) ==
        L1D_PREV_REQUESTS_TABLE_ENTRIES) {  // Not in prev
        l1d_add_prev_requests_table(index, offset,
                                    curr_cpu->current_core_cycle());
    } else {
        if (!desc.hit && !l1d_ongoing_request(index, offset)) {
            l1d_add_prev_requests_table(index, offset,
                                        curr_cpu->current_core_cycle());
        }
    }

    // Add miss in the latency table
    if (!recently_accessed && !desc.hit) {  // If not accessed recently
        // l1d_reset_entry_latencies_table(index, offset); // If completed, add
        // a new one
        l1d_add_latencies_table(index, offset, curr_cpu->current_core_cycle());
    }

    int status = -1;
    if (desc.hit) {
        status = 0;
        l1d_ip_hits[ip_index]++;
    } else if (l1d_ongoing_request(index, offset)) {
        status = 1;
        l1d_ip_late[ip_index]++;
    } else if (l1d_is_request(index, offset)) {
        status = 3;
        l1d_ip_early[ip_index]++;
    } else {
        status = 2;
        l1d_ip_misses[ip_index]++;
    }

    if (berti != 0) {
        // Burst mode
        if ((first_access && full_access) ||
            l1d_current_pages_table[index].continue_burst) {
            int burst_init = 0;
            int burst_end = 0;
            int burst_it = 0;
            if (!linnea_hits ||
                l1d_current_pages_table[index]
                    .continue_burst) {  // Linnea missed: full burst
                l1d_current_pages_table[index].continue_burst = false;
                if (berti > 0) {
                    burst_init = offset + 1;
                    burst_end = offset + berti;
                    burst_it = 1;
                } else {
                    burst_init = offset - 1;
                    burst_end = offset + berti;
                    burst_it = -1;
                }
            } else if (last_berti > 0 && berti > 0 &&
                       berti > last_berti) {  // larger abs berti: semi burst
                burst_init = last_berti;
                burst_end = berti;
                burst_it = 1;
            } else if (last_berti < 0 && berti < 0 &&
                       berti < last_berti) {  // larger abs berti: semi burst
                burst_init = L1D_PAGE_OFFSET_MASK + last_berti;
                burst_end = L1D_PAGE_OFFSET_MASK + berti;
                burst_it = -1;
            }
            int bursts = 0;
            // if (ip_index == 0x10f) cout << "BURST " << burst_init << " " <<
            // burst_end << endl;
            for (int i = burst_init; i != burst_end; i += burst_it) {
                // if (i < 0 || i >= L1D_PAGE_BLOCKS) cout << i << " " <<
                // burst_init << " " << burst_end << " " << burst_it << endl;
                if (i >= 0 &&
                    i < L1D_PAGE_BLOCKS) {  // Burst are for the current page
                    uint64_t pf_line_addr =
                        (page_addr << L1D_PAGE_BLOCKS_BITS) | i;
                    uint64_t pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
                    uint64_t pf_offset = pf_line_addr & L1D_PAGE_OFFSET_MASK;
                    // We are doing the berti here. Do not leave space for it
                    if (!this->_cache_inst->prefetch_queue()->is_full() &&
                        bursts < L1D_BURST_THROTTLING) {
                        // if (ip_index == 0x10f) cout << "BURST PREFETCH " <<
                        // hex << page_addr << dec << " <" << pf_offset << ">"
                        // << endl;
                        bool prefetched = this->_cache_inst->prefetch_line(
                            desc.cpu, BLOCK_SIZE, desc.ip, desc.addr, pf_addr,
                            cc::cache::fill_l1, 0);
                        assert(prefetched);
                        l1d_add_latencies_table(index, pf_offset,
                                                curr_cpu->current_core_cycle());
                        bursts++;
                    } else {  // record last burst
#ifdef CONTINUE_BURST
                        if (!recently_accessed) {  // If not accessed recently
                            l1d_current_pages_table[index].continue_burst =
                                true;
                        }
#endif
                        break;
                    }
                }
            }
        }

        // Berti mode
        for (int i = 1; i <= L1D_BERTI_THROTTLING; i++) {
            // If the prefetcher will be done
            if (!this->_cache_inst->prefetch_queue()->is_full()) {
                uint64_t pf_line_addr = line_addr + (berti * i);
                uint64_t pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
                uint64_t pf_page_addr = pf_line_addr >> L1D_PAGE_BLOCKS_BITS;
                uint64_t pf_offset = pf_line_addr & L1D_PAGE_OFFSET_MASK;

                // Same page, prefetch standard
                if (pf_page_addr == page_addr) {
                    // if (ip_index == 0x10f) cout << "BERTI PREFETCH " << hex
                    // << page_addr << dec << " <" << pf_offset << ">" << endl;
                    // bool prefetched =
                    //     prefetch_line(ip, addr, pf_addr, FILL_L1, 1);
                    bool prefetched = this->_cache_inst->prefetch_line(
                        desc.cpu, BLOCK_SIZE, desc.ip, desc.addr, pf_addr,
                        cc::cache::fill_l1, 0);
                    assert(prefetched);
                    l1d_add_latencies_table(index, pf_offset,
                                            curr_cpu->current_core_cycle());

                    // Out of page, try consecutive first
                } else if (l1d_ip_table[ip_index].consecutive && berti != 0) {
                    uint64_t new_page;
                    if (berti < 0) {
                        new_page = page_addr - 1;
                    } else {
                        new_page = page_addr + 1;
                    }

                    // Need to add the linnea page to current pages
                    uint64_t new_index = l1d_get_current_pages_entry(new_page);

                    if (new_index == L1D_CURRENT_PAGES_TABLE_ENTRIES) {
                        // Add new page entry evicting a previous one.
                        new_index = l1d_evict_lru_current_page_entry();
                        l1d_add_current_pages_table(new_index, new_page);
                    }

                    uint64_t pf_offset = (offset + berti + L1D_PAGE_BLOCKS) &
                                         L1D_PAGE_OFFSET_MASK;
                    uint64_t new_line = new_page << L1D_PAGE_BLOCKS_BITS;
                    uint64_t new_pf_line = new_line | pf_offset;
                    uint64_t new_addr = new_line << LOG2_BLOCK_SIZE;
                    uint64_t new_pf_addr = new_pf_line << LOG2_BLOCK_SIZE;

                    // cout << "CONSECUTIVE " << hex << new_page << " " << dec
                    // << pf_offset << hex << " " << " " << new_line << " " <<
                    // new_pf_line << " " << new_addr << " " << new_pf_addr <<
                    // dec << endl;

                    // if (ip_index == 0x10f) cout << "CONSECUTIVE PREFETCH " <<
                    // hex << new_page << dec << " <" << pf_offset << ">" <<
                    // endl;
                    // bool prefetched =
                    //     prefetch_line(ip, new_addr, new_pf_addr, FILL_L1, 1);
                    bool prefetched = this->_cache_inst->prefetch_line(
                        desc.cpu, BLOCK_SIZE, desc.ip, new_addr, new_pf_addr,
                        cc::cache::fill_l1, 0);
                    assert(prefetched);
                    l1d_add_latencies_table(new_index, pf_offset,
                                            curr_cpu->current_core_cycle());

                } else {  // Out of page, try Linnea
#ifdef LINNEA
                    uint64_t index_record =
                        l1d_get_entry_record_pages_table(page_addr);
                    if (index_record <
                        L1D_RECORD_PAGES_TABLE_ENTRIES) {  // Linnea found

                        uint64_t new_page =
                            l1d_record_pages_table[index_record].linnea;

                        // Need to add the linnea page to current pages
                        uint64_t new_index =
                            l1d_get_current_pages_entry(new_page);

                        if (new_index == L1D_CURRENT_PAGES_TABLE_ENTRIES) {
                            // Add new page entry evicting a previous one.
                            new_index = l1d_evict_lru_current_page_entry();
                            l1d_add_current_pages_table(new_index, new_page);
                        }

                        uint64_t pf_offset =
                            (offset + berti + L1D_PAGE_BLOCKS) &
                            L1D_PAGE_OFFSET_MASK;
                        uint64_t new_line = new_page << L1D_PAGE_BLOCKS_BITS;
                        uint64_t new_pf_line = new_line | pf_offset;
                        uint64_t new_addr = new_line << LOG2_BLOCK_SIZE;
                        uint64_t new_pf_addr = new_pf_line << LOG2_BLOCK_SIZE;

                        // cout << "LINNEA " << hex << new_page << " " << dec <<
                        // pf_offset << hex << " " << " " << new_line << " " <<
                        // new_pf_line << " " << new_addr << " " << new_pf_addr
                        // << dec << endl;

                        // if (ip_index == 0x10f) cout << "LINNEA PREFETCH " <<
                        // hex << new_page << dec << " <" << pf_offset << ">" <<
                        // endl;
                        // bool prefetched = prefetch_line(
                        //     ip, new_addr, new_pf_addr, FILL_L1, 1);
                        bool prefetched = this->_cache_inst->prefetch_line(
                            desc.cpu, BLOCK_SIZE, desc.ip, new_addr,
                            new_pf_addr, cc::cache::fill_l1, 0);
                        assert(prefetched);
                        l1d_add_latencies_table(new_index, pf_offset,
                                                curr_cpu->current_core_cycle());
                    }
#endif
                }
            }
        }
    }

#ifdef PREFETCH_FOR_LONG_REUSE
    if (!short_reuse) {  // Use stride as it is a long reuse ip

        assert(!l1d_ip_table[ip_index].short_reuse ||
               !l1d_current_pages_table[index].short_reuse);

        // If the prefetcher will be done
        if (!this->_cache_inst->prefetch_queue()->is_full()) {
            uint64_t index_record = l1d_get_entry_record_pages_table(page_addr);
            if (index_record <
                L1D_RECORD_PAGES_TABLE_ENTRIES) {  // Linnea found

                uint64_t new_page = l1d_record_pages_table[index_record].linnea;
                uint64_t new_offset =
                    l1d_record_pages_table[index_record].last_offset;
                int new_stride;
                int where;
                if (!l1d_current_pages_table[index].short_reuse) {
                    new_stride = l1d_current_pages_table[index].stride;
                    where = 1;
                } else {
                    assert(!l1d_ip_table[ip_index].short_reuse);
                    where = 2;
                    new_stride = l1d_ip_table[ip_index].berti_or_pointer;
                }
                // if (ip_index == 0x10f) cout << "LONG REUSE PREFETCH " << hex
                // << page_addr << "->" << new_page << " " << ip_index << dec <<
                // " " << new_offset << " " << new_stride << " " << where <<
                // endl;

                // Need to add the linnea page to current pages
                uint64_t new_index = l1d_get_current_pages_entry(new_page);

                if (new_index == L1D_CURRENT_PAGES_TABLE_ENTRIES) {
                    // Add new page entry evicting a previous one.
                    new_index = l1d_evict_lru_current_page_entry();
                    l1d_add_current_pages_table(new_index, new_page);
                }

                uint64_t pf_offset = new_offset + new_stride;
                if (pf_offset >= 0 && pf_offset < L1D_PAGE_BLOCKS) {
                    uint64_t new_line = new_page << L1D_PAGE_BLOCKS_BITS;
                    uint64_t new_pf_line = new_line | pf_offset;
                    uint64_t new_addr = new_line << LOG2_BLOCK_SIZE;
                    uint64_t new_pf_addr = new_pf_line << LOG2_BLOCK_SIZE;

                    // cout << "LINNEA " << hex << new_page << " " << dec <<
                    // pf_offset << hex << " " << " " << new_line << " " <<
                    // new_pf_line << " " << new_addr << " " << new_pf_addr <<
                    // dec << endl;

                    // if (ip_index == 0x10f) cout << "STRIDE PREFETCH " << hex
                    // << new_page << dec << " <" << pf_offset << ">" << endl;
                    // bool prefetched =
                    //     prefetch_line(ip, new_addr, new_pf_addr, FILL_L1,
                    //                   (count_reuse < 3) ? 0 : 1);
                    bool prefetched = this->_cache_inst->prefetch_line(
                        desc.cpu, BLOCK_SIZE, desc.ip, new_addr, new_pf_addr,
                        cc::cache::fill_l1, 0);
                    assert(prefetched);
                    l1d_add_latencies_table(new_index, pf_offset,
                                            curr_cpu->current_core_cycle());
                }
            }
        }
    }
#endif
}

/**
 * @brief This method performs updates on the prefetcher on the event of a
 * fill in the cache.
 * @param A descriptor with information regarding the cahce block filled.
 */
void cp::l1d_berti::fill(
    const champsim::helpers::cache_access_descriptor& desc) {
    uint64_t line_addr = (desc.full_addr >> LOG2_BLOCK_SIZE);
    uint64_t page_addr = line_addr >> L1D_PAGE_BLOCKS_BITS;
    uint64_t offset = line_addr & L1D_PAGE_OFFSET_MASK;

    uint64_t pointer_prev = l1d_get_current_pages_entry(page_addr);

    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(desc.cpu);

    if (pointer_prev <
        L1D_CURRENT_PAGES_TABLE_ENTRIES) {  // look in prev requests

        // First look in prefetcher, since if there is a hit, it is the time the
        // miss started
        uint64_t latency = l1d_get_and_set_latency_latencies_table(
            pointer_prev, offset, curr_cpu->current_core_cycle());

        if (latency != 0) {
            // Find berti (distance from pref_latency + demand_latency cycles
            // before
            int berti[L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS];
            unsigned saved_cycles[L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS];
            l1d_get_berti_prev_requests_table(pointer_prev, offset, latency,
                                              berti, saved_cycles, 0);
            /* if (page_addr == 0x28e837ca4) { */
            /* 	cout << "@" << current_core_cycle << " (" << latency << ")
             * ADD BERTI MISS " << pointer_prev << " <" << offset << "> "; */
            /* 	for (int i = 0; berti[i] != 0; i++) { */
            /* 	  cout << berti[i] << " " << saved_cycles[i] << ", "; */
            /* 	} */
            /* 	cout << endl; */
            /* 	l1d_print_prev_requests_table(pointer_prev); */
            /* } */
            l1d_add_berti_current_pages_table(pointer_prev, berti,
                                              saved_cycles);

        }  // If not found, berti will not be found neither
    }      // If not found, not entry in prev requests

    // If the replacement is in the prev req, invalidate the entry (not usefull
    // anymore)
    uint64_t evicted_page =
        (desc.victim_addr >> LOG2_BLOCK_SIZE) >> L1D_PAGE_BLOCKS_BITS;
    uint64_t evicted_index = l1d_get_current_pages_entry(evicted_page);
    if (evicted_index < L1D_CURRENT_PAGES_TABLE_ENTRIES) {
        uint64_t evicted_offset =
            (desc.victim_addr >> LOG2_BLOCK_SIZE) & L1D_PAGE_OFFSET_MASK;
        l1d_reset_entry_latencies_table(evicted_index, evicted_offset);
    }
}

void cp::l1d_berti::clear_stats() { this->_stats = berti_stats(); }

void cp::l1d_berti::dump_stats() {
    std::cout << this->_stats << std::endl << std::endl;
}

cp::l1d_berti* cp::l1d_berti::clone() { return new l1d_berti(*this); }

/**
 * This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cp::iprefetcher* cp::l1d_berti::create_prefetcher() {
    return new cp::l1d_berti();
}

void cp::l1d_berti::_init(const pt::ptree& props, cc::cache* cache_inst) {
    // Calling the version of the parent class first.
    cp::iprefetcher::_init(props, cache_inst);

    l1d_init_current_pages_table();
    l1d_init_prev_requests_table();
    l1d_init_latencies_table();
    l1d_init_record_pages_table();
    l1d_init_ip_table();
}

std::ostream& operator<<(std::ostream& os,
                         const cp::l1d_berti::berti_stats& bs) {
    return os;
}