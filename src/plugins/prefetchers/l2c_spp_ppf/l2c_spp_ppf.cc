#include <plugins/prefetchers/l2c_spp_ppf/l2c_spp_ppf.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l2c_spp_ppf_prefetcher::l2c_spp_ppf_prefetcher() {}

cp::l2c_spp_ppf_prefetcher::l2c_spp_ppf_prefetcher(
    const cp::l2c_spp_ppf_prefetcher& o)
    : iprefetcher(o) {}

/**
 * Destructor of the class.
 */
cp::l2c_spp_ppf_prefetcher::~l2c_spp_ppf_prefetcher() {}

/**
 * @brief This method performs the actual operation of the prefetcher. This
 * method is meant to be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch
 * request.
 */
void cp::l2c_spp_ppf_prefetcher::operate(
    const cp::prefetch_request_descriptor& desc) {
    uint64_t page = desc.addr >> LOG2_PAGE_SIZE;
    uint32_t page_offset =
                 (desc.addr >> LOG2_BLOCK_SIZE) & (PAGE_SIZE / BLOCK_SIZE - 1),
             last_sig = 0, curr_sig = 0,
             confidence_q[100 * this->_cache_inst->mshr_size()], depth = 0;

    int32_t delta = 0, delta_q[100 * this->_cache_inst->mshr_size()],
            perc_sum_q[100 * this->_cache_inst->mshr_size()];

    for (uint32_t i = 0; i < 100 * this->_cache_inst->mshr_size(); i++) {
        confidence_q[i] = 0;
        delta_q[i] = 0;
        perc_sum_q[i] = 0;
    }

    confidence_q[0] = 100;
    GHR.global_accuracy =
        GHR.pf_issued
            ? ((100 * GHR.pf_useful) / GHR.pf_issued)
            : 0;

    for (int i = PAGES_TRACKED - 1; i > 0; i--) {  // N down to 1
        GHR.page_tracker[i] = GHR.page_tracker[i - 1];
    }

    GHR.page_tracker[0] = page;

    int distinct_pages = 0;
    uint8_t num_pf = 0;
    for (int i = 0; i < PAGES_TRACKED; i++) {
        int j;
        for (j = 0; j < i; j++) {
            if (GHR.page_tracker[i] == GHR.page_tracker[j]) break;
        }
        if (i == j) distinct_pages++;
    }

    // Stage 1: Read and update a sig stored in ST
    // last_sig and delta are used to update (sig, delta) correlation in PT
    // curr_sig is used to read prefetch candidates in PT
    ST.read_and_update_sig(page, page_offset, last_sig, curr_sig, delta);

    FILTER.train_neg = 1;

    // Also check the prefetch filter in parallel to update global accuracy
    // counters
    FILTER.check(desc.addr, 0, 0, L2C_DEMAND, 0, 0, 0, 0, 0, 0);

    // Stage 2: Update delta patterns stored in PT
    if (last_sig) PT.update_pattern(last_sig, delta);

    // Stage 3: Start prefetching
    uint64_t base_addr = desc.addr;
    uint64_t curr_ip = desc.ip;
    uint32_t lookahead_conf = 100, pf_q_head = 0, pf_q_tail = 0;
    uint8_t do_lookahead = 0;
    int32_t prev_delta = 0;

    uint64_t train_addr = desc.addr;
    int32_t train_delta = 0;

    GHR.ip_3 = GHR.ip_2;
    GHR.ip_2 = GHR.ip_1;
    GHR.ip_1 = GHR.ip_0;
    GHR.ip_0 = desc.ip;

#ifdef LOOKAHEAD_ON
    do {
#endif  // LOOKAHEAD_ON
        uint32_t lookahead_way = PT_WAY;

        train_addr = desc.addr;
        train_delta = prev_delta;
        // Remembering the original addr here and accumulating the deltas in
        // lookahead stages

        // Read the PT. Also passing info required for perceptron inferencing as
        // PT calls perc_predict()
        PT.read_pattern(curr_sig, delta_q, confidence_q, perc_sum_q,
                               lookahead_way, lookahead_conf, pf_q_tail, depth,
                               desc.addr, base_addr, train_addr, curr_ip,
                               train_delta, last_sig,
                               this->_cache_inst->prefetch_queue_occupancy(),
                               this->_cache_inst->prefetch_queue_size(),
                               this->_cache_inst->mshr_occupancy(),
                               this->_cache_inst->mshr_size());

        do_lookahead = 0;

        for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {
            uint64_t pf_addr = (base_addr & ~(BLOCK_SIZE - 1)) +
                               (delta_q[i] << LOG2_BLOCK_SIZE);
            int32_t perc_sum = perc_sum_q[i];

            FILTER_REQUEST fill_level = (perc_sum >= PERC_THRESHOLD_HI)
                                            ? SPP_L2C_PREFETCH
                                            : SPP_LLC_PREFETCH;

            if ((desc.addr & ~(PAGE_SIZE - 1)) ==
                (pf_addr & ~(PAGE_SIZE -
                             1))) {  // Prefetch request is in the same physical
                                     // page Filter checks for redundancy and
                                     // returns FALSE if redundant
                // Else it returns TRUE and logs the features for future
                // retrieval
                if (num_pf < ceil(((this->_cache_inst->prefetch_queue_size()) /
                                   distinct_pages))) {
                    if (FILTER.check(
                            pf_addr, train_addr, curr_ip, fill_level,
                            train_delta + delta_q[i], last_sig, curr_sig,
                            confidence_q[i], perc_sum, (depth - 1))) {
                        // Histogramming Idea
                        int32_t perc_sum_shifted =
                            perc_sum + (PERC_COUNTER_MAX + 1) * PERC_FEATURES;
                        int32_t hist_index = perc_sum_shifted / 10;
                        FILTER.hist_tots[hist_index]++;

                        //[DO NOT TOUCH]:
                        if (this->_cache_inst->prefetch_line(
                                desc.cpu, BLOCK_SIZE, desc.ip, desc.addr,
                                pf_addr,
                                ((fill_level == SPP_L2C_PREFETCH)
                                     ? cc::cache::fill_l2
                                     : cc::cache::fill_llc),
                                0)) {
                            num_pf++;
                            FILTER.add_to_filter(
                                pf_addr, train_addr, curr_ip, fill_level,
                                train_delta + delta_q[i], last_sig, curr_sig,
                                confidence_q[i], perc_sum, (depth - 1));
                        } else {
                            this->_prefetch_q_full++;
                        }

                        // Only for stats
                        GHR.perc_pass++;
                        GHR.depth_val = 1;
                        GHR.pf_total++;
                        if (fill_level == SPP_L2C_PREFETCH) GHR.pf_l2c++;
                        if (fill_level == SPP_LLC_PREFETCH) GHR.pf_llc++;
                        // Stats end

                        // FILTER.valid_reject[quotient] = 0;
                        if (fill_level == SPP_L2C_PREFETCH) {
                            GHR.pf_issued++;
                            if (GHR.pf_issued > GLOBAL_COUNTER_MAX) {
                                GHR.pf_issued >>= 1;
                                GHR.pf_useful >>= 1;
                            }
                        }
                    }
                }
            } else {  // Prefetch request is crossing the physical page boundary
#ifdef GHR_ON
                // Store this prefetch request in GHR to bootstrap SPP learning
                // when we see a ST miss (i.e., accessing a new page)
                GHR.update_entry(curr_sig, confidence_q[i],
                                        (pf_addr >> LOG2_BLOCK_SIZE) & 0x3F,
                                        delta_q[i]);
#endif
            }
            do_lookahead = 1;
            pf_q_head++;
        }

        // Update base_addr and curr_sig
        if (lookahead_way < PT_WAY) {
            uint32_t set = get_hash(curr_sig) % PT_SET;
            base_addr +=
                (PT.delta[set][lookahead_way] << LOG2_BLOCK_SIZE);
            prev_delta += PT.delta[set][lookahead_way];

            // PT.delta uses a 7-bit sign magnitude representation to generate
            // sig_delta
            // int sig_delta = (PT.delta[set][lookahead_way] < 0) ? ((((-1) *
            // PT.delta[set][lookahead_way]) & 0x3F) + 0x40) :
            // PT.delta[set][lookahead_way];
            int sig_delta =
                (PT.delta[set][lookahead_way] < 0)
                    ? (((-1) * PT.delta[set][lookahead_way]) +
                       (1 << (SIG_DELTA_BIT - 1)))
                    : PT.delta[set][lookahead_way];
            curr_sig = ((curr_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
        }
#ifdef LOOKAHEAD_ON
    } while (do_lookahead);
#endif  // LOOKAHEAD_ON

    // Stats
    if (GHR.depth_val) {
        GHR.depth_num++;
        GHR.depth_sum += depth;
    }

    this->_depth_track[depth]++;
}

void cp::l2c_spp_ppf_prefetcher::fill(
    const champsim::helpers::cache_access_descriptor& desc) {
#ifdef FILTER_ON
    FILTER.check(desc.victim_addr, 0ULL, 0ULL, L2C_EVICT, 0, 0, 0, 0, 0,
                        0);
#endif  // FILTER_ON
}

cp::l2c_spp_ppf_prefetcher* cp::l2c_spp_ppf_prefetcher::clone() {
    return new l2c_spp_ppf_prefetcher(*this);
}

void cp::l2c_spp_ppf_prefetcher::clone(l2c_spp_ppf_prefetcher* o) {
    cp::iprefetcher::operator=(*o);
}

/**
 * This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cp::iprefetcher* cp::l2c_spp_ppf_prefetcher::create_prefetcher() {
    return new cp::l2c_spp_ppf_prefetcher();
}

void cp::l2c_spp_ppf_prefetcher::_init(const pt::ptree& props,
                                       cc::cache* cache_inst) {
    // Calling the version of the parent class first.
    cp::iprefetcher::_init(props, cache_inst);

    for (std::size_t i = 0; i < 30; i++) this->_depth_track[i] = 0;

    this->_prefetch_q_full = 0;
}
