#ifndef BLOCK_H
#define BLOCK_H

#include <functional>
#include <stack>
#include <type_traits>
#
#include "champsim.h"
#include "set.h"
#
#include <boost/dynamic_bitset.hpp>
#
#include <internals/components/memory_enums.hh>

namespace cc = champsim::components;

namespace champsim::components {
class cache;
class perceptron_feature;
}  // namespace champsim::components

// CACHE BLOCK
class BLOCK {
   public:
    bool pte_used = false, filled_from_write_allocate = false,
         went_offchip_pred = false;

    uint8_t valid, prefetch, dirty, used;

    int delta, depth, signature, confidence;

    uint64_t address, full_addr, tag, data, ip, cpu, instr_id, pte_pc_insertion;

    cc::cache_type served_from;

    // replacement state
    uint32_t lru;

    BLOCK() {
        valid = 0;
        prefetch = 0;
        dirty = 0;
        used = 0;

        delta = 0;
        depth = 0;
        signature = 0;
        confidence = 0;

        address = 0;
        full_addr = 0;
        tag = 0;
        data = 0;
        cpu = 0;
        instr_id = 0;

        lru = 0;
    };
};

// DRAM CACHE BLOCK
class DRAM_ARRAY {
   public:
    BLOCK **block;

    DRAM_ARRAY() { block = NULL; };
};

// message packet
class PACKET {
   public:
    using fill_path_t = std::stack<cc::cache *>;
    static_assert(std::is_move_assignable<fill_path_t::value_type>::value &&
                  std::is_move_constructible<fill_path_t::value_type>::value);

   public:
    bool is_sliced, is_first_slice,
        sniffer = false, bypassed_l2c_llc = false, metadata_insertion = false,
        metadata_eviction = false, went_offchip_pred = false,
        l1d_offchip_pred_used = false, pf_went_offchip_pred = false,
        pf_went_offchip = false;

    uint8_t instruction, is_data, is_metadata, fill_l1i, fill_l1d, tlb_access,
        scheduled, translated, fetched, prefetched, drc_tag_read;

    int fill_level, pf_origin_level, rob_signal, rob_index, producer, delta,
        depth, signature, confidence;

    uint32_t pf_metadata;
    uint8_t locmap_data[BLOCK_SIZE];

    uint8_t is_producer,
        // rob_index_depend_on_me[ROB_SIZE],
        // lq_index_depend_on_me[ROB_SIZE],
        // sq_index_depend_on_me[ROB_SIZE],
        instr_merged, load_merged, store_merged, returned, asid[2], type;

    fastset rob_index_depend_on_me, lq_index_depend_on_me,
        sq_index_depend_on_me;

    uint32_t cpu, data_index, lq_index, sq_index;

    uint64_t address, full_addr, v_address, full_v_addr, instruction_pa,
        data_pa, data, instr_id, ip, event_cycle, cycle_enqueued, birth_cycle,
        death_cycle;

    uint32_t memory_size;

    cc::block_location block_location_pred;

    cc::sdc_routes route;

    // Elements used to describe hits in a Distill Cache.
    cc::hit_types hit_type;
    cc::cache_type served_from, missed_in;
    cc::cache_type metadata_type;

    std::stack<cc::cache *> fill_path;

    float perceptron_weights_sum;
    cc::uarch_state_info info;

    PACKET() {
        instruction = 0;
        is_data = 1;
        is_metadata = false;
        fill_l1i = 0;
        fill_l1d = 0;
        tlb_access = 0;
        scheduled = 0;
        translated = 0;
        fetched = 0;
        prefetched = 0;
        drc_tag_read = 0;

        returned = 0;
        asid[0] = UINT8_MAX;
        asid[1] = UINT8_MAX;
        type = 0;

        fill_level = -1;
        rob_signal = -1;
        rob_index = -1;
        producer = -1;
        delta = 0;
        depth = 0;
        signature = 0;
        confidence = 0;

#if 0
		for (uint32_t i=0; i<ROB_SIZE; i++) {
			rob_index_depend_on_me[i] = 0;
			lq_index_depend_on_me[i] = 0;
			sq_index_depend_on_me[i] = 0;
		}
#endif
        is_producer = 0;
        instr_merged = 0;
        load_merged = 0;
        store_merged = 0;

        cpu = 0;
        data_index = 0;
        lq_index = 0;
        sq_index = 0;

        address = 0;
        full_addr = 0;
        instruction_pa = 0;
        data = 0;
        instr_id = 0;
        ip = 0;
        event_cycle = UINT64_MAX;
        cycle_enqueued = 0;

        memory_size = 0;

        route = cc::invalid_route;

        hit_type = static_cast<cc::hit_types>(INT32_MAX);
        served_from = static_cast<cc::cache_type>(INT32_MAX);
        missed_in = static_cast<cc::cache_type>(0x0);
    }

    ~PACKET() {}

    void push_fill_path(cc::cache *c);
    cc::cache *pop_fill_path();
    void pop_fill_path_until(
        const std::function<bool(const fill_path_t &)> &cmp);
    void merge_fill_path(const PACKET &o, bool *modified = nullptr);
};

bool check_duplicate_block_with_route(const cc::cache *cache, const PACKET &a,
                                      const PACKET &b, bool is_l1d_wq);

// packet queue
class PACKET_QUEUE {
   public:
    using value_type = PACKET;
    using pointer = std::add_pointer<value_type>::type;
    using iterator = std::add_pointer<value_type>::type;

   public:
    string NAME;
    uint32_t SIZE;

    uint8_t is_RQ, is_WQ, write_mode;

    uint32_t cpu, head, tail, occupancy, num_returned, next_fill_index,
        next_schedule_index, next_process_index;

    uint64_t next_fill_cycle, next_schedule_cycle, next_process_cycle, ACCESS,
        FORWARD, MERGED, TO_CACHE, ROW_BUFFER_HIT, ROW_BUFFER_MISS, FULL;

    pointer entry, processed_packet[2 * MAX_READ_PER_CYCLE];

    // constructor
    PACKET_QUEUE(string v1, uint32_t v2) : NAME(v1), SIZE(v2) {
        is_RQ = 0;
        is_WQ = 0;
        write_mode = 0;

        cpu = 0;
        head = 0;
        tail = 0;
        occupancy = 0;
        num_returned = 0;
        next_fill_index = 0;
        next_schedule_index = 0;
        next_process_index = 0;

        next_fill_cycle = UINT64_MAX;
        next_schedule_cycle = UINT64_MAX;
        next_process_cycle = UINT64_MAX;

        ACCESS = 0;
        FORWARD = 0;
        MERGED = 0;
        TO_CACHE = 0;
        ROW_BUFFER_HIT = 0;
        ROW_BUFFER_MISS = 0;
        FULL = 0;

        entry = new PACKET[SIZE];
    };

    PACKET_QUEUE() {
        is_RQ = 0;
        is_WQ = 0;

        cpu = 0;
        head = 0;
        tail = 0;
        occupancy = 0;
        num_returned = 0;
        next_fill_index = 0;
        next_schedule_index = 0;
        next_process_index = 0;

        next_fill_cycle = UINT64_MAX;
        next_schedule_cycle = UINT64_MAX;
        next_process_cycle = UINT64_MAX;

        ACCESS = 0;
        FORWARD = 0;
        MERGED = 0;
        TO_CACHE = 0;
        ROW_BUFFER_HIT = 0;
        ROW_BUFFER_MISS = 0;
        FULL = 0;

        // entry = new PACKET[SIZE];
    };

    // destructor
    ~PACKET_QUEUE() { delete[] entry; };

    // functions
    int check_queue(const PACKET *packet);

    template <typename TernaryOperation>
    int check_queue(const cc::cache *cache, const PACKET &packet,
                    TernaryOperation op) {
        if ((head == tail) && occupancy == 0) {
            return -1;
        }

        if (head < tail) {
            for (uint32_t i = head; i < tail; i++) {
                if (op(cache, entry[i], packet, (this->NAME == "L1D_WQ"))) {
                    return i;
                }
            }
        } else {
            for (uint32_t i = head; i < SIZE; i++) {
                if (op(cache, entry[i], packet, (this->NAME == "L1D_WQ"))) {
                    return i;
                }
            }
            for (uint32_t i = 0; i < tail; i++) {
                if (op(cache, entry[i], packet, (this->NAME == "L1D_WQ"))) {
                    return i;
                }
            }
        }

        return -1;
    }

    void add_queue(PACKET *packet), remove_queue(PACKET *packet);

    void retire_element(iterator it);

    void add_queue(const PACKET &packet, const uint32_t &latency = 0);

    iterator begin();
    iterator end();

    bool is_full() const;
    void mark_full();

    // Operators.
    PACKET &operator[](const std::size_t &pos);
    const PACKET &operator[](const std::size_t &pos) const;
};

// reorder buffer
class CORE_BUFFER {
   public:
    const string NAME;
    const uint32_t SIZE;
    uint32_t cpu, head, tail, occupancy, last_read, last_fetch, last_scheduled,
        inorder_fetch[2], next_fetch[2], next_schedule;
    uint64_t event_cycle, fetch_event_cycle, schedule_event_cycle,
        execute_event_cycle, lsq_event_cycle, retire_event_cycle;

    ooo_model_instr *entry;

    // constructor
    CORE_BUFFER(string v1, uint32_t v2) : NAME(v1), SIZE(v2) {
        head = 0;
        tail = 0;
        occupancy = 0;

        last_read = SIZE - 1;
        last_fetch = SIZE - 1;
        last_scheduled = 0;

        inorder_fetch[0] = 0;
        inorder_fetch[1] = 0;
        next_fetch[0] = 0;
        next_fetch[1] = 0;
        next_schedule = 0;

        event_cycle = 0;
        fetch_event_cycle = UINT64_MAX;
        schedule_event_cycle = UINT64_MAX;
        execute_event_cycle = UINT64_MAX;
        lsq_event_cycle = UINT64_MAX;
        retire_event_cycle = UINT64_MAX;

        entry = new ooo_model_instr[SIZE];
    };

    // destructor
    ~CORE_BUFFER() { delete[] entry; };
};

// load/store queue
class LSQ_ENTRY {
   public:
    bool went_offchip, went_offchip_pred, l1d_miss_offchip_pred,
        l1d_offchip_pred_used;
    uint64_t instr_id, producer_id, virtual_address, physical_address, ip,
        event_cycle;

    uint32_t rob_index, data_index, sq_index;
    uint32_t memory_size;

    uint8_t translated, fetched, asid[2];
    // forwarding_depend_on_me[ROB_SIZE];
    fastset forwarding_depend_on_me;

    cc::perceptron_feature *perc_feature;
    cc::block_location block_location_pred;

    int32_t l1d_offchip_pred_weights_sum;

    // WIP: Where was the request served from in the memory sub-system?
    cc::cache_type served_from;

#if defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))
    bool offchip_pred_hit_l1d,
         offchip_pred_hit_l2c;
#endif  // defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))

    // constructor
    LSQ_ENTRY() {
        went_offchip = false;
        went_offchip_pred = false;
        l1d_miss_offchip_pred = false;
        l1d_offchip_pred_used = false;

        block_location_pred = cc::is_in_dram;

        l1d_offchip_pred_weights_sum = 0;

        instr_id = 0;
        producer_id = UINT64_MAX;
        virtual_address = 0;
        physical_address = 0;
        ip = 0;
        event_cycle = 0;

        rob_index = 0;
        data_index = 0;
        sq_index = UINT32_MAX;

        memory_size = 0;

        translated = 0;
        fetched = 0;
        asid[0] = UINT8_MAX;
        asid[1] = UINT8_MAX;

#if defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))
        offchip_pred_hit_l1d = false;
        offchip_pred_hit_l2c = false;
#endif  // defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))

#if 0
		for (uint32_t i=0; i<ROB_SIZE; i++)
			forwarding_depend_on_me[i] = 0;
#endif
    };
};

class LOAD_STORE_QUEUE {
   public:
    const string NAME;
    const uint32_t SIZE;
    uint32_t occupancy, head, tail;

    LSQ_ENTRY *entry;

    // constructor
    LOAD_STORE_QUEUE(string v1, uint32_t v2) : NAME(v1), SIZE(v2) {
        occupancy = 0;
        head = 0;
        tail = 0;

        entry = new LSQ_ENTRY[SIZE];
    };

    // destructor
    ~LOAD_STORE_QUEUE() { delete[] entry; };
};
#endif
