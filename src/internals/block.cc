#include <stdexcept>
#
#include "block.h"
#
#include <internals/components/cache.hh>
#include <internals/simulator.hh>

/**
 * @brief Adds a cache to the fill path stack. Typically, this function would be
 * used on a miss in a cache right before propagating this packet to a lower
 * level memory component.
 *
 * @param c A pointer to the cache to be added to the fill path stack.
 */
void PACKET::push_fill_path(cc::cache *c) {
    // We add the provided pointer to a cache to the end of the list.
    this->fill_path.push(c);
}

/**
 * @brief Retrieve the last added cache of the fill path stack.
 *
 * @return cc::cache* A pointer to the next cache on the fill path for this
 * packet.
 * */
cc::cache *PACKET::pop_fill_path() {
    cc::cache *c = nullptr;

    // We can only pop the last element of the stack if it is not empty.
    if (this->fill_path.empty()) {
        throw std::runtime_error(
            "It not allowed to unpack a cache from an empty fill path stack.");
    }

    // We are sure that the fill path stack is not empty. We can safely unpack
    // the last element.
    c = this->fill_path.top();
    this->fill_path.pop();

    return c;
}

/**
 * @brief Unstack fill path targets until the predicate cmp(this->fill_path) is
 * true.
 *
 * @param cmp A predicate that returns false until we stop unstaking elements.
 */
void PACKET::pop_fill_path_until(
    const std::function<bool(const fill_path_t &)> &cmp) {
    while (!cmp(this->fill_path)) {
        this->pop_fill_path();

        if (this->fill_path.empty()) break;
    }
}

/**
 * @brief Merges the fill path of the provided packet o into the fill path of
 * this packet.
 *
 * @param o The other packet from which the fill path must be merged.
 * @param modified A pointer to a boolean that will indicate whether the fill
 * path has been modified.
 */
void PACKET::merge_fill_path(const PACKET &o, bool *modified) {
    std::deque<cc::cache *> new_fp;
    PACKET::fill_path_t new_fp_s;

    /**
     * @brief Helper function to do the actual job of inserting in the
     * appropriate place the different caches constituting the fill paths.
     *
     * @param fp The fill path over which we will be looping to insert its
     * elements in the final fill path. (n.b.: Here we use a copy such that we
     * can unpack the stack without disturbing the actual fill paths).
     */
    auto _helper = [&new_fp](PACKET::fill_path_t fp) -> void {
        cc::cache *cc = nullptr;

        while (!fp.empty()) {
            // Unpacking the top element of the stack.
            cc = fp.top();
            fp.pop();

            // We simply add cc if it does not belong to new_fp.
            if (std::find(new_fp.begin(), new_fp.end(), cc) == new_fp.end()) {
                new_fp.push_back(cc);
            }
        }
    };

    // TODO: We must have some sort of sanity checks here.
#ifndef NDEBUG
    // Sanity check: each fill level can only be represented once.
    std::map<cc::cache::fill_levels, uint8_t> counts;

    for (const auto &e : new_fp) {
        counts[static_cast<cc::cache::fill_levels>(
            e->fill_level())]++;  // Incrementing the counter of that fill
                                  // level.
    }

    // None of the counts must be above one.
    assert(std::none_of(counts.begin(), counts.end(),
                        [](const auto &e) -> bool { return e.second > 1; }));
#endif  // NDEBUG

    // Using the helper function on both this->fill_path and o.fill_path.
    _helper(this->fill_path);
    _helper(o.fill_path);

#if defined(SANITY_CHECK)
    for (const auto &e : new_fp) {
        if (!e->check_type(cc::is_llc) && e->cpu() != this->cpu) {
            std::stringstream sstream;

            sstream << "Invalid fill path merge. CPU " << this->cpu
                    << " as original destination and CPU " << e->cpu()
                    << " found in the fill path.";

            throw std::runtime_error(sstream.str());
        }
    }
#endif

    // Now we sort it.
    std::sort(new_fp.begin(), new_fp.end(),
              [](const auto a, const auto b) -> bool { return *a < *b; });

    new_fp_s = PACKET::fill_path_t(new_fp);

    if (modified != nullptr) {
        *modified = (this->fill_path != new_fp_s);
    }

    // Copying the content of the new fill path into this packet.
    this->fill_path = new_fp_s;
}

bool check_duplicate_block_with_route(const cc::cache *cache, const PACKET &a,
                                      const PACKET &b, bool is_l1d_wq) {
    bool same_address = false, same_route = false, same_cpu = false,
         slow_track_a = false, slow_track_b = false;

    // Checking if we are referring to the same piece of data (this is dependent
    // on whether it's a read or a write).
    if (is_l1d_wq) {
        same_address = (a.full_addr == b.full_addr);
    } else {
        same_address = ((a.full_addr >> cache->log2_block_size()) ==
                        (b.full_addr >> cache->log2_block_size()));
    }

    slow_track_a = (a.route != cc::sdc_dram);
    slow_track_b = (b.route != cc::sdc_dram);

    same_route =
        !((slow_track_a && !slow_track_b) || (!slow_track_a && slow_track_b));

    if (a.route == cc::invalid_route ^ b.route == cc::invalid_route) {
        same_route = false;
    }

    // Checking cpu ids. They have to be equal in order to find a duplicate.
    same_cpu = (a.cpu == b.cpu);

    return (same_address && same_route && same_cpu);
}

int PACKET_QUEUE::check_queue(const PACKET *packet) {
    if ((head == tail) && occupancy == 0) return -1;

    if (head < tail) {
        for (uint32_t i = head; i < tail; i++) {
            if (NAME == "L1D_WQ") {
                if (entry[i].full_addr == packet->full_addr) {
                    DP(if (warmup_complete[packet->cpu]) {
                        cout << "[" << NAME << "] " << __func__
                             << " cpu: " << packet->cpu
                             << " instr_id: " << packet->instr_id
                             << " same address: " << hex << packet->address;
                        cout << " full_addr: " << packet->full_addr << dec
                             << " by instr_id: " << entry[i].instr_id
                             << " index: " << i;
                        cout << " cycle " << packet->event_cycle << endl;
                    });
                    return i;
                }
            } else {
                if (entry[i].address == packet->address) {
                    DP(if (warmup_complete[packet->cpu]) {
                        cout << "[" << NAME << "] " << __func__
                             << " cpu: " << packet->cpu
                             << " instr_id: " << packet->instr_id
                             << " same address: " << hex << packet->address;
                        cout << " full_addr: " << packet->full_addr << dec
                             << " by instr_id: " << entry[i].instr_id
                             << " index: " << i;
                        cout << " cycle " << packet->event_cycle << endl;
                    });
                    return i;
                }
            }
        }
    } else {
        for (uint32_t i = head; i < SIZE; i++) {
            if (NAME == "L1D_WQ") {
                if (entry[i].full_addr == packet->full_addr) {
                    DP(if (warmup_complete[packet->cpu]) {
                        cout << "[" << NAME << "] " << __func__
                             << " cpu: " << packet->cpu
                             << " instr_id: " << packet->instr_id
                             << " same address: " << hex << packet->address;
                        cout << " full_addr: " << packet->full_addr << dec
                             << " by instr_id: " << entry[i].instr_id
                             << " index: " << i;
                        cout << " cycle " << packet->event_cycle << endl;
                    });
                    return i;
                }
            } else {
                if (entry[i].address == packet->address) {
                    DP(if (warmup_complete[packet->cpu]) {
                        cout << "[" << NAME << "] " << __func__
                             << " cpu: " << packet->cpu
                             << " instr_id: " << packet->instr_id
                             << " same address: " << hex << packet->address;
                        cout << " full_addr: " << packet->full_addr << dec
                             << " by instr_id: " << entry[i].instr_id
                             << " index: " << i;
                        cout << " cycle " << packet->event_cycle << endl;
                    });
                    return i;
                }
            }
        }
        for (uint32_t i = 0; i < tail; i++) {
            if (NAME == "L1D_WQ") {
                if (entry[i].full_addr == packet->full_addr) {
                    DP(if (warmup_complete[packet->cpu]) {
                        cout << "[" << NAME << "] " << __func__
                             << " cpu: " << packet->cpu
                             << " instr_id: " << packet->instr_id
                             << " same address: " << hex << packet->address;
                        cout << " full_addr: " << packet->full_addr << dec
                             << " by instr_id: " << entry[i].instr_id
                             << " index: " << i;
                        cout << " cycle " << packet->event_cycle << endl;
                    });
                    return i;
                }
            } else {
                if (entry[i].address == packet->address) {
                    DP(if (warmup_complete[packet->cpu]) {
                        cout << "[" << NAME << "] " << __func__
                             << " cpu: " << packet->cpu
                             << " instr_id: " << packet->instr_id
                             << " same address: " << hex << packet->address;
                        cout << " full_addr: " << packet->full_addr << dec
                             << " by instr_id: " << entry[i].instr_id
                             << " index: " << i;
                        cout << " cycle " << packet->event_cycle << endl;
                    });
                    return i;
                }
            }
        }
    }

    return -1;
}

void PACKET_QUEUE::add_queue(PACKET *packet) {
#ifdef SANITY_CHECK
    if (occupancy && (head == tail)) assert(0);
#endif

    // add entry
    entry[tail] = *packet;

    DP(if (warmup_complete[packet->cpu]) {
        cout << "[" << NAME << "] " << __func__ << " cpu: " << packet->cpu
             << " instr_id: " << packet->instr_id;
        cout << " address: " << hex << entry[tail].address
             << " full_addr: " << entry[tail].full_addr << dec;
        cout << " head: " << head << " tail: " << tail
             << " occupancy: " << occupancy
             << " event_cycle: " << entry[tail].event_cycle << endl;
    });

    occupancy++;
    tail++;
    if (tail >= SIZE) tail = 0;
}

void PACKET_QUEUE::add_queue(const PACKET &packet, const uint32_t &latency) {
    std::size_t ins_idx = this->tail;
    O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);

    // Copying data to the packet queue.
    this->entry[ins_idx] = packet;

    // Updating the queue.
    this->occupancy++;
    this->tail = ((this->tail + 1) % this->SIZE);

    // Making the addition of latency.
    if (this->entry[ins_idx].event_cycle < curr_cpu->current_core_cycle()) {
        this->entry[ins_idx].event_cycle =
            curr_cpu->current_core_cycle() + latency;
    } else {
        this->entry[ins_idx].event_cycle += latency;
    }
}

void PACKET_QUEUE::remove_queue(PACKET *packet) {
#ifdef SANITY_CHECK
    if ((occupancy == 0) && (head == tail)) assert(0);
#endif

    DP(if (warmup_complete[packet->cpu]) {
        cout << "[" << NAME << "] " << __func__ << " cpu: " << packet->cpu
             << " instr_id: " << packet->instr_id;
        cout << " address: " << hex << packet->address
             << " full_addr: " << packet->full_addr << dec
             << " fill_level: " << packet->fill_level;
        cout << " head: " << head << " tail: " << tail
             << " occupancy: " << occupancy
             << " event_cycle: " << packet->event_cycle << endl;
    });

    // reset entry
    PACKET empty_packet;
    *packet = empty_packet;

    occupancy--;
    head++;
    if (head >= SIZE) head = 0;
}

void PACKET_QUEUE::retire_element(PACKET_QUEUE::iterator it) {
    // First, we fill the packet by copying a clean instance.
    *it = PACKET();

    this->occupancy--;
}

PACKET_QUEUE::iterator PACKET_QUEUE::begin() { return this->entry; }

PACKET_QUEUE::iterator PACKET_QUEUE::end() {
    return (this->entry + this->SIZE);
}

bool PACKET_QUEUE::is_full() const { return (this->occupancy == this->SIZE); }

void PACKET_QUEUE::mark_full() { this->FULL++; }

PACKET &PACKET_QUEUE::operator[](const std::size_t &pos) {
    return this->entry[pos];
}

const PACKET &PACKET_QUEUE::operator[](const std::size_t &pos) const {
    return this->entry[pos];
}
