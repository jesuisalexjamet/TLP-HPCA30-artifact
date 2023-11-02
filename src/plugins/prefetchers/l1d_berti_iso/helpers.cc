#include <plugins/prefetchers/l1d_berti/helpers.hh>

namespace cp = champsim::prefetchers;

uint64_t cp::l1d_get_latency(uint64_t cycle, uint64_t cycle_prev) {
    uint64_t cycle_masked = cycle & L1D_TIME_MASK;
    uint64_t cycle_prev_masked = cycle_prev & L1D_TIME_MASK;
    if (cycle_prev_masked > cycle_masked) {
        return (cycle_masked + L1D_TIME_OVERFLOW) - cycle_prev_masked;
    }
    return cycle_masked - cycle_prev_masked;
}

int cp::l1d_calculate_stride(uint64_t prev_offset, uint64_t current_offset) {
    assert(prev_offset < L1D_PAGE_BLOCKS);
    assert(current_offset < L1D_PAGE_BLOCKS);
    int stride;
    if (current_offset > prev_offset) {
        stride = current_offset - prev_offset;
    } else {
        stride = prev_offset - current_offset;
        stride *= -1;
    }
    assert(stride > (0 - L1D_PAGE_BLOCKS) && stride < L1D_PAGE_BLOCKS);
    return stride;
}

uint64_t cp::l1d_count_bit_vector(uint64_t vector) {
    uint64_t count = 0;
    for (int i = 0; i < L1D_PAGE_BLOCKS; i++) {
        if (vector & ((uint64_t)1 << i)) {
            count++;
        }
    }
    return count;
}

uint64_t cp::l1d_count_wrong_berti_bit_vector(uint64_t vector, int berti) {
    uint64_t wrong = 0;
    for (int i = 0; i < L1D_PAGE_BLOCKS; i++) {
        if (vector & ((uint64_t)1 << i)) {
            if (i + berti >= 0 && i + berti < L1D_PAGE_BLOCKS &&
                !(vector & ((uint64_t)1 << (i + berti)))) {
                wrong++;
            }
        }
    }
    return wrong;
}

uint64_t cp::l1d_count_lost_berti_bit_vector(uint64_t vector, int berti) {
    uint64_t lost = 0;
    if (berti > 0) {
        for (int i = 0; i < berti; i++) {
            if (vector & ((uint64_t)1 << i)) {
                lost++;
            }
        }
    } else if (berti < 0) {
        for (int i = L1D_PAGE_OFFSET_MASK; i > L1D_PAGE_OFFSET_MASK + berti;
             i--) {
            if (vector & ((uint64_t)1 << i)) {
                lost++;
            }
        }
    }
    return lost;
}

bool cp::l1d_all_last_berti_accessed_bit_vector(uint64_t vector, int berti) {
    unsigned count_yes = 0;
    unsigned count_no = 0;
    if (berti < 0) {
        for (int i = 0; i < (0 - berti); i++) {
            (vector & ((uint64_t)1 << i)) ? count_yes++ : count_no++;
        }
    } else if (berti > 0) {
        for (int i = L1D_PAGE_OFFSET_MASK; i > L1D_PAGE_OFFSET_MASK - berti;
             i--) {
            (vector & ((uint64_t)1 << i)) ? count_yes++ : count_no++;
        }
    } else
        return true;
    // cout << "COUNT: " << count_yes << " " << count_no << " " <<
    // ((double)count_yes / (double)(count_yes + count_no)) << " " <<
    // (((double)count_yes / (double)(count_yes + count_no)) >
    // L1D_BURST_THRESHOLD) << endl;
    if (count_yes == 0) return false;
    // return (count_no == 0);
    return ((double)count_yes / (double)(count_yes + count_no)) >
           L1D_BURST_THRESHOLD;
}

void cp::l1d_init_current_pages_table() {
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_ENTRIES; i++) {
        l1d_current_pages_table[i].page_addr = 0;
        l1d_current_pages_table[i].u_vector = 0;  // not valid
        for (int j = 0; j < L1D_CURRENT_PAGES_TABLE_NUM_BERTI; j++) {
            l1d_current_pages_table[i].berti[j] = 0;
        }
        l1d_current_pages_table[i].current_berti = 0;
        l1d_current_pages_table[i].stride = 0;
        l1d_current_pages_table[i].short_reuse = true;
        l1d_current_pages_table[i].continue_burst = false;
        l1d_current_pages_table[i].lru = i;
    }
}

uint64_t cp::l1d_get_current_pages_entry(uint64_t page_addr) {
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_ENTRIES; i++) {
        if (l1d_current_pages_table[i].page_addr == page_addr) return i;
    }
    return L1D_CURRENT_PAGES_TABLE_ENTRIES;
}

void cp::l1d_update_lru_current_pages_table(uint64_t index) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_ENTRIES; i++) {
        if (l1d_current_pages_table[i].lru <
            l1d_current_pages_table[index].lru) {  // Found
            l1d_current_pages_table[i].lru++;
        }
    }
    l1d_current_pages_table[index].lru = 0;
}

uint64_t cp::l1d_get_lru_current_pages_entry() {
    uint64_t lru = L1D_CURRENT_PAGES_TABLE_ENTRIES;
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_ENTRIES; i++) {
        l1d_current_pages_table[i].lru++;
        if (l1d_current_pages_table[i].lru == L1D_CURRENT_PAGES_TABLE_ENTRIES) {
            l1d_current_pages_table[i].lru = 0;
            lru = i;
        }
    }
    assert(lru != L1D_CURRENT_PAGES_TABLE_ENTRIES);
    return lru;
}

void cp::l1d_add_current_pages_table(uint64_t index, uint64_t page_addr) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);
    l1d_current_pages_table[index].page_addr = page_addr;
    l1d_current_pages_table[index].u_vector = 0;
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_NUM_BERTI; i++) {
        l1d_current_pages_table[index].berti[i] = 0;
    }
    l1d_current_pages_table[index].continue_burst = false;
}

void cp::l1d_update_current_pages_table(uint64_t index, uint64_t offset) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);
    l1d_current_pages_table[index].u_vector |= (uint64_t)1 << offset;
    l1d_update_lru_current_pages_table(index);
}

void cp::l1d_remove_offset_current_pages_table(uint64_t index,
                                               uint64_t offset) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);
    l1d_current_pages_table[index].u_vector &= !((uint64_t)1 << offset);
}

void cp::l1d_add_berti_current_pages_table(uint64_t index, int *berti,
                                           unsigned *saved_cycles) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);

    // for each berti collected
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS; i++) {
        if (berti[i] == 0) break;
        // assert(abs(berti[i]) < L1D_PAGE_BLOCKS);

        for (int j = 0; j < L1D_CURRENT_PAGES_TABLE_NUM_BERTI; j++) {
            if (l1d_current_pages_table[index].berti[j] == 0) {
                l1d_current_pages_table[index].berti[j] = berti[i];
#ifdef BERTI_LATENCIES
                l1d_current_pages_table[index].berti_score[j] = saved_cycles[i];
#else
                l1d_current_pages_table[index].berti_score[j] = 1;
#endif
                break;
            } else if (l1d_current_pages_table[index].berti[j] == berti[i]) {
#ifdef BERTI_LATENCIES
                l1d_current_pages_table[index].berti_score[j] +=
                    saved_cycles[i];
#else
                l1d_current_pages_table[index].berti_score[j]++;
                // assert(l1d_current_pages_table[index].berti_score[j]
                // < L1D_PAGE_BLOCKS);
#endif
#ifdef WARMUP_NEW_PAGES
                // For first time accessed pages. No wait until it is evicted to
                // predict
                if (l1d_current_pages_table[index].current_berti == 0 &&
                    l1d_current_pages_table[index].berti_score[j] > 2) {
                    l1d_current_pages_table[index].current_berti = berti[i];
                }
#endif
                break;
            }
        }
    }
    l1d_update_lru_current_pages_table(index);
}

void cp::l1d_sub_berti_current_pages_table(uint64_t index, int distance) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);

    // for each berti
    for (int j = distance; j < L1D_CURRENT_PAGES_TABLE_NUM_BERTI; j++) {
        if (l1d_current_pages_table[index].berti[j] == 0) {
            break;
        }
#ifdef BERTI_LATENCIES
        if (l1d_current_pages_table[index].berti_score[j] >= 100) {
            l1d_current_pages_table[index].berti_score[j] -= 100;
        }
#else
        if (l1d_current_pages_table[index].berti_score[j] > 0) {
            l1d_current_pages_table[index].berti_score[j]--;
        }
#endif
    }
}

int cp::l1d_get_berti_current_pages_table(uint64_t index) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);
    uint64_t vector = l1d_current_pages_table[index].u_vector;
    int max_score = 0;
    uint64_t berti = 0;
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_NUM_BERTI; i++) {
        int curr_berti = l1d_current_pages_table[index].berti[i];
        if (curr_berti != 0) {
            // For every miss reduce next level access latency
            int score = l1d_current_pages_table[index].berti_score[i];
#if defined(BERTI_LATENCIES) || defined(JUST_BERTI)
            int neg_score = 0;  // l1d_count_wrong_berti_bit_vector(vector,
                                // curr_berti) * LLC_LATENCY;
#else
            int neg_score = 0 - abs(curr_berti);
            // ((abs(curr_berti) >> 1) + (abs(curr_berti) >> 2));
            // l1d_count_wrong_berti_bit_vector(vector, curr_berti) -
            // l1d_count_lost_berti_bit_vector(vector, curr_berti);
#endif
            // Modify score based on bad prefetches
            if (score < neg_score) {
                score = 0;
            } else {
                score -= neg_score;
            }
            if (score >= max_score) {  // In case of a draw we choose the
                                       // larger, since we have bursts
                berti = curr_berti;
                max_score = score;
            }
        }
    }
    return berti;
}

bool cp::l1d_offset_requested_current_pages_table(uint64_t index,
                                                  uint64_t offset) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);
    assert(offset < L1D_PAGE_BLOCKS);
    return l1d_current_pages_table[index].u_vector & ((uint64_t)1 << offset);
}

void cp::l1d_init_prev_requests_table() {
    l1d_prev_requests_table_head = 0;
    for (int i = 0; i < L1D_PREV_REQUESTS_TABLE_ENTRIES; i++) {
        l1d_prev_requests_table[i].page_addr_pointer =
            L1D_PREV_REQUESTS_TABLE_NULL_POINTER;
    }
}

uint64_t cp::l1d_find_prev_request_entry(uint64_t pointer, uint64_t offset) {
    for (int i = 0; i < L1D_PREV_REQUESTS_TABLE_ENTRIES; i++) {
        if (l1d_prev_requests_table[i].page_addr_pointer == pointer &&
            l1d_prev_requests_table[i].offset == offset)
            return i;
    }
    return L1D_PREV_REQUESTS_TABLE_ENTRIES;
}

void cp::l1d_add_prev_requests_table(uint64_t pointer, uint64_t offset,
                                     uint64_t cycle) {
    // First find for coalescing
    if (l1d_find_prev_request_entry(pointer, offset) !=
        L1D_PREV_REQUESTS_TABLE_ENTRIES)
        return;

    // Allocate a new entry (evict old one if necessary)
    l1d_prev_requests_table[l1d_prev_requests_table_head].page_addr_pointer =
        pointer;
    l1d_prev_requests_table[l1d_prev_requests_table_head].offset = offset;
    l1d_prev_requests_table[l1d_prev_requests_table_head].time =
        cycle & L1D_TIME_MASK;
    l1d_prev_requests_table_head =
        (l1d_prev_requests_table_head + 1) & L1D_PREV_REQUESTS_TABLE_MASK;
}

void cp::l1d_reset_pointer_prev_requests(uint64_t pointer) {
    for (int i = 0; i < L1D_PREV_REQUESTS_TABLE_ENTRIES; i++) {
        if (l1d_prev_requests_table[i].page_addr_pointer == pointer) {
            l1d_prev_requests_table[i].page_addr_pointer =
                L1D_PREV_REQUESTS_TABLE_NULL_POINTER;
        }
    }
}

void cp::l1d_get_berti_prev_requests_table(uint64_t pointer, uint64_t offset,
                                           uint64_t latency, int *berti,
                                           unsigned *saved_cycles,
                                           uint64_t req_time) {
    int my_pos = 0;
    uint64_t extra_time = 0;
    uint64_t last_time =
        l1d_prev_requests_table[(l1d_prev_requests_table_head +
                                 L1D_PREV_REQUESTS_TABLE_MASK) &
                                L1D_PREV_REQUESTS_TABLE_MASK]
            .time;
    // cout << "Latency " << latency << " " << pointer << " " << offset << " ";
    // l1d_print_prev_requests_table(pointer);
    for (uint64_t i =
             (l1d_prev_requests_table_head + L1D_PREV_REQUESTS_TABLE_MASK) &
             L1D_PREV_REQUESTS_TABLE_MASK;
         i != l1d_prev_requests_table_head;
         i = (i + L1D_PREV_REQUESTS_TABLE_MASK) &
             L1D_PREV_REQUESTS_TABLE_MASK) {
        // Against the time overflow
        if (last_time < l1d_prev_requests_table[i].time) {
            extra_time = L1D_TIME_OVERFLOW;
        }
        last_time = l1d_prev_requests_table[i].time;
        if (l1d_prev_requests_table[i].page_addr_pointer ==
            pointer) {                                          // Same page
            if (l1d_prev_requests_table[i].offset == offset) {  // Its me
                req_time = l1d_prev_requests_table[i].time;
            } else if (req_time) {  // Not me (check only older than me)
                if (l1d_prev_requests_table[i].time <=
                    req_time + extra_time - latency) {
                    berti[my_pos] = l1d_calculate_stride(
                        l1d_prev_requests_table[i].offset, offset);
                    saved_cycles[my_pos] = latency;
                    // cout << "pos1 " << my_pos << ": " << berti[my_pos] <<
                    // "->" << saved_cycles[my_pos] << endl;
                    my_pos++;
                } else if (req_time + extra_time -
                               l1d_prev_requests_table[i].time >
                           0) {  // Only if some savings
#ifdef BERTI_LATENCIES
                    berti[my_pos] = l1d_calculate_stride(
                        l1d_prev_requests_table[i].offset, offset);
                    saved_cycles[my_pos] =
                        req_time + extra_time - l1d_prev_requests_table[i].time;
                    // cout << "pos2 " << my_pos << ": " << berti[my_pos] <<
                    // "->" << saved_cycles[my_pos] << " " << extra_time << " "
                    // << l1d_prev_requests_table[i].time << endl;
                    my_pos++;
#endif
                }
                if (my_pos == L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS) {
                    berti[my_pos] = 0;
                    return;
                }
            }
        }
    }
    berti[my_pos] = 0;
}

void cp::l1d_init_latencies_table() {
    l1d_latencies_table_head = 0;
    for (int i = 0; i < L1D_LATENCIES_TABLE_ENTRIES; i++) {
        l1d_latencies_table[i].page_addr_pointer =
            L1D_LATENCIES_TABLE_NULL_POINTER;
    }
}

uint64_t cp::l1d_find_latency_entry(uint64_t pointer, uint64_t offset) {
    for (int i = 0; i < L1D_LATENCIES_TABLE_ENTRIES; i++) {
        if (l1d_latencies_table[i].page_addr_pointer == pointer &&
            l1d_latencies_table[i].offset == offset)
            return i;
    }
    return L1D_LATENCIES_TABLE_ENTRIES;
}

void cp::l1d_add_latencies_table(uint64_t pointer, uint64_t offset,
                                 uint64_t cycle) {
    // First find for coalescing
    if (l1d_find_latency_entry(pointer, offset) != L1D_LATENCIES_TABLE_ENTRIES)
        return;

    // Allocate a new entry (evict old one if necessary)
    l1d_latencies_table[l1d_latencies_table_head].page_addr_pointer = pointer;
    l1d_latencies_table[l1d_latencies_table_head].offset = offset;
    l1d_latencies_table[l1d_latencies_table_head].time_lat =
        cycle & L1D_TIME_MASK;
    l1d_latencies_table[l1d_latencies_table_head].completed = false;
    l1d_latencies_table_head =
        (l1d_latencies_table_head + 1) & L1D_LATENCIES_TABLE_MASK;
}

void cp::l1d_reset_pointer_latencies(uint64_t pointer) {
    for (int i = 0; i < L1D_LATENCIES_TABLE_ENTRIES; i++) {
        if (l1d_latencies_table[i].page_addr_pointer == pointer) {
            l1d_latencies_table[i].page_addr_pointer =
                L1D_LATENCIES_TABLE_NULL_POINTER;
        }
    }
}

void cp::l1d_reset_entry_latencies_table(uint64_t pointer, uint64_t offset) {
    uint64_t index = l1d_find_latency_entry(pointer, offset);
    if (index != L1D_LATENCIES_TABLE_ENTRIES) {
        l1d_latencies_table[index].page_addr_pointer =
            L1D_LATENCIES_TABLE_NULL_POINTER;
    }
}

uint64_t cp::l1d_get_and_set_latency_latencies_table(uint64_t pointer,
                                                     uint64_t offset,
                                                     uint64_t cycle) {
    uint64_t index = l1d_find_latency_entry(pointer, offset);
    if (index == L1D_LATENCIES_TABLE_ENTRIES) return 0;
    if (!l1d_latencies_table[index].completed) {
        l1d_latencies_table[index].time_lat =
            l1d_get_latency(cycle, l1d_latencies_table[index].time_lat);
        l1d_latencies_table[index].completed = true;
    }
    return l1d_latencies_table[index].time_lat;
}

uint64_t cp::l1d_get_latency_latencies_table(uint64_t pointer,
                                             uint64_t offset) {
    uint64_t index = l1d_find_latency_entry(pointer, offset);
    if (index == L1D_LATENCIES_TABLE_ENTRIES) return 0;
    if (!l1d_latencies_table[index].completed) return 0;
    return l1d_latencies_table[index].time_lat;
}

bool cp::l1d_ongoing_request(uint64_t pointer, uint64_t offset) {
    uint64_t index = l1d_find_latency_entry(pointer, offset);
    if (index == L1D_LATENCIES_TABLE_ENTRIES) return false;
    if (l1d_latencies_table[index].completed) return false;
    return true;
}

bool cp::l1d_is_request(uint64_t pointer, uint64_t offset) {
    uint64_t index = l1d_find_latency_entry(pointer, offset);
    if (index == L1D_LATENCIES_TABLE_ENTRIES) return false;
    return true;
}

void cp::l1d_init_record_pages_table() {
    for (int i = 0; i < L1D_RECORD_PAGES_TABLE_ENTRIES; i++) {
        l1d_record_pages_table[i].page_addr = 0;
        l1d_record_pages_table[i].linnea = 0;
        l1d_record_pages_table[i].last_offset = 0;
        l1d_record_pages_table[i].short_reuse = true;
        l1d_record_pages_table[i].lru = i;
    }
}

uint64_t cp::l1d_get_lru_record_pages_entry() {
    uint64_t lru = L1D_RECORD_PAGES_TABLE_ENTRIES;
    for (int i = 0; i < L1D_RECORD_PAGES_TABLE_ENTRIES; i++) {
        l1d_record_pages_table[i].lru++;
        if (l1d_record_pages_table[i].lru == L1D_RECORD_PAGES_TABLE_ENTRIES) {
            l1d_record_pages_table[i].lru = 0;
            lru = i;
        }
    }
    assert(lru != L1D_RECORD_PAGES_TABLE_ENTRIES);
    return lru;
}

void cp::l1d_update_lru_record_pages_table(uint64_t index) {
    assert(index < L1D_RECORD_PAGES_TABLE_ENTRIES);
    for (int i = 0; i < L1D_RECORD_PAGES_TABLE_ENTRIES; i++) {
        if (l1d_record_pages_table[i].lru <
            l1d_record_pages_table[index].lru) {  // Found
            l1d_record_pages_table[i].lru++;
        }
    }
    l1d_record_pages_table[index].lru = 0;
}

uint64_t cp::l1d_get_entry_record_pages_table(uint64_t page_addr) {
    uint64_t trunc_page_addr = page_addr & L1D_TRUNCATED_PAGE_ADDR_MASK;
    for (int i = 0; i < L1D_RECORD_PAGES_TABLE_ENTRIES; i++) {
        if (l1d_record_pages_table[i].page_addr == trunc_page_addr) {  // Found
            return i;
        }
    }
    return L1D_RECORD_PAGES_TABLE_ENTRIES;
}

void cp::l1d_add_record_pages_table(uint64_t page_addr, uint64_t new_page_addr,
                                    uint64_t last_offset, bool short_reuse) {
    uint64_t index = l1d_get_entry_record_pages_table(page_addr);
    if (index < L1D_RECORD_PAGES_TABLE_ENTRIES) {
        l1d_update_lru_record_pages_table(index);
    } else {
        index = l1d_get_lru_record_pages_entry();
        l1d_record_pages_table[index].page_addr =
            page_addr & L1D_TRUNCATED_PAGE_ADDR_MASK;
    }
    l1d_record_pages_table[index].linnea = new_page_addr;
    l1d_record_pages_table[index].last_offset = last_offset;
    l1d_record_pages_table[index].short_reuse = short_reuse;
}

void cp::l1d_init_ip_table() {
    for (int i = 0; i < L1D_IP_TABLE_ENTRIES; i++) {
        l1d_ip_table[i].current = false;
        l1d_ip_table[i].berti_or_pointer = 0;
        l1d_ip_table[i].consecutive = false;
        l1d_ip_table[i].short_reuse = true;

        l1d_ip_misses[i] = 0;
        l1d_ip_hits[i] = 0;
        l1d_ip_late[i] = 0;
        l1d_ip_early[i] = 0;
    }
    l1d_stats_pref_addr = 0;
    l1d_stats_pref_ip = 0;
    l1d_stats_pref_current = 0;
    cache_accesses = 0;
    cache_misses = 0;
}

void cp::l1d_update_ip_table(int pointer, int berti, int stride,
                             bool short_reuse) {
    for (int i = 0; i < L1D_IP_TABLE_ENTRIES; i++) {
        if (l1d_ip_table[i].current &&
            l1d_ip_table[i].berti_or_pointer == pointer) {
            l1d_ip_table[i].current = false;
            if (short_reuse) {
                l1d_ip_table[i].berti_or_pointer = berti;
            } else {
                l1d_ip_table[i].berti_or_pointer = stride;
            }
            l1d_ip_table[i].short_reuse = short_reuse;
        }
    }
}

uint64_t cp::l1d_evict_lru_current_page_entry() {
    // Find victim and clear pointers to it
    uint64_t victim_index =
        l1d_get_lru_current_pages_entry();  // already updates lru
    assert(victim_index < L1D_CURRENT_PAGES_TABLE_ENTRIES);

    // From all timely delta found, we record the best
    if (l1d_current_pages_table[victim_index].u_vector) {  // Accessed entry

        // Update any IP pointing to it
        l1d_update_ip_table(victim_index,
                            l1d_get_berti_current_pages_table(victim_index),
                            l1d_current_pages_table[victim_index].stride,
                            l1d_current_pages_table[victim_index].short_reuse);
    }

    l1d_reset_pointer_prev_requests(victim_index);  // Not valid anymore
    l1d_reset_pointer_latencies(victim_index);      // Not valid anymore

    return victim_index;
}

void cp::l1d_evict_current_page_entry(uint64_t index) {
    assert(index < L1D_CURRENT_PAGES_TABLE_ENTRIES);

    // From all timely delta found, we record the best
    if (l1d_current_pages_table[index].u_vector) {  // Accessed entry

        // Update any IP pointing to it
        l1d_update_ip_table(index, l1d_get_berti_current_pages_table(index),
                            l1d_current_pages_table[index].stride,
                            l1d_current_pages_table[index].short_reuse);
    }

    l1d_reset_pointer_prev_requests(index);  // Not valid anymore
    l1d_reset_pointer_latencies(index);      // Not valid anymore
}

void cp::l1d_remove_current_table_entry(uint64_t index) {
    l1d_current_pages_table[index].page_addr = 0;
    l1d_current_pages_table[index].u_vector = 0;
    for (int i = 0; i < L1D_CURRENT_PAGES_TABLE_NUM_BERTI; i++) {
        l1d_current_pages_table[index].berti[i] = 0;
    }
}