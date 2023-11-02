#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_MISS_MAP_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_MISS_MAP_HH__

#include <cmath>
#include <cstdint>
#include <ostream>
#
#include <utility>
#
#include <forward_list>
#include <vector>
#
#include <internals/block.h>

#define BLOCK_COVERAGE (8ULL * (BLOCK_SIZE / 2ULL))
#define BLOCK_COVERAGE_SIZE (BLOCK_SIZE * BLOCK_COVERAGE)
#define BLOCK_COUNT ((DRAM_SIZE << 20ULL) / BLOCK_COVERAGE_SIZE)

#define TAG_SAMPLER_SET 1024

// Forward declarations.
class O3_CPU;

namespace champsim {
class simulator;
}

namespace champsim {
namespace components {
/**
 * @brief Defines events such as insertions and evictions from the L2C and LLC.
 */
enum cache_event {
    evict_l2c = 0x0,  /*!< A block has been evicted from the L2C. */
    insert_l2c = 0x1, /*!< A block has been inserted in the L2C. */
    evict_llc = 0x2,  /*!< A block has been evicted from the LLC. */
    insert_llc = 0x3, /*!< A block has been evicted from the LLC. */
};

/**
 * @brief Defines an update of the MetaData cache.
 */
struct metadata_access_descriptor {
    bool hit;
    cache_type type;
    cache::access_types access_type;
    uint8_t cpu;
    uint64_t paddr, victim_addr;
};

/**
 * @brief Describes a prediction issued by the LocMap and the MetaData Cache.
 */
struct locmap_prediction_descriptor {
    bool metadata_cache_hit; /*!< Was the access to the MetaData Cache a hit? */
    block_location
        location; /*!< What is the predicted location for the block? */
    std::list<cc::block_location>
        destinations; /*!< A list of the different destinations to add a packet
                         to. */
};

class metadata_cache {
   public:
    struct entry {
       public:
        bool valid, dirty, heuristic_based;
        uint16_t lru;
        uint64_t tag;
        std::vector<uint64_t> visited_tags;
        // std::vector<bool> bit_vector;
        uint8_t* data = nullptr;

       public:
        entry()
            : valid(false),
              dirty(false),
              heuristic_based(false),
              tag(0ULL),
              data(nullptr) {}

        entry(const std::size_t& length) : valid(false), tag(0x0) {
            // Sanity check. At this point in the execution, the data pointer
            // must be null.
            assert(this->data == nullptr);

            // We allocate a 64B buffer for each MetaData Cache entry.
            this->data = new uint8_t[BLOCK_SIZE];

            // TODO: We must initialize the data to all 'is_in_dram'.
            for (std::size_t i = 0; i < BLOCK_SIZE; i++) {
                for (std::size_t j = 0; j < 4; j++) {
                    this->data[i] |= static_cast<uint8_t>(cc::is_in_dram)
                                     << (2 * j);
                }
            }
        }

        entry(const entry& o) {
            this->valid = o.valid;
            this->dirty = o.dirty;
            this->heuristic_based = o.heuristic_based;
            this->tag = o.tag;

            // We also have to copy the data.
            if (this->data == nullptr) {
                this->data = new uint8_t[BLOCK_SIZE];
            }

            std::memcpy(this->data, o.data, BLOCK_SIZE);
        }

        entry(entry&& o) {
            this->valid = std::move(o.valid);
            this->dirty = std::move(o.dirty);
            this->heuristic_based = std::move(o.heuristic_based);
            this->tag = std::move(o.tag);

            // We also have to copy the data.
            if (this->data == nullptr) {
                this->data = new uint8_t[BLOCK_SIZE];
            }

            std::memmove(this->data, o.data, BLOCK_SIZE);
        }

        ~entry() {
            // If a data buffer has been allocated, we free the memory.
            if (data) {
                delete[] this->data;
            }
        }

        entry& operator=(const entry& o) {
            this->valid = o.valid;
            this->dirty = o.dirty;
            this->heuristic_based = o.heuristic_based;
            this->tag = o.tag;

            // We also have to copy the data.
            if (this->data == nullptr) {
                this->data = new uint8_t[BLOCK_SIZE];
            }

            std::memcpy(this->data, o.data, BLOCK_SIZE);

            return *this;
        }

        entry& operator=(entry&& o) {
            this->valid = std::move(o.valid);
            this->dirty = std::move(o.dirty);
            this->heuristic_based = std::move(o.heuristic_based);
            this->tag = std::move(o.tag);

            // We also have to copy the data.
            if (this->data == nullptr) {
                this->data = new uint8_t[BLOCK_SIZE];
            }

            std::memmove(this->data, o.data, BLOCK_SIZE);

            return *this;
        }

        /**
         * @brief This method retrives the location of a block in the memory
         * hirarchy based on a LocMap entry.
         *
         * @param paddr The full physical address of the memory block for which
         * we are lookineg up the location.
         * @return The location of a memory block in the memory hierarchy.
         */
        block_location location(const uint64_t& paddr) const {
            uint64_t paddr_mask = 0x3fff, data_mask = 0x3;

            // Computing indices... i is needed to index data and j to query
            // only the adequate bits.
            uint8_t i = ((paddr & paddr_mask) >> LOG2_BLOCK_SIZE) / 4,
                    j = ((paddr & paddr_mask) >> LOG2_BLOCK_SIZE) % 4;

            // Sanity checks.
            assert(i < BLOCK_SIZE && j < 4);

            // Now, we can retrive the two bits location tags.
            block_location loc = static_cast<block_location>(
                (this->data[i] >> (2 * j)) & data_mask);

            return loc;
        }

        /**
         * @brief Set the location in the memory hierarchy of one block.
         *
         * @param paddr The physical address of the block considered.
         * @param loc The location affected to this block.
         */
        void set_location(const uint64_t& paddr, block_location loc) const {
            uint64_t paddr_mask = 0x3fff, data_mask = 0x3;

            // Computing indices... i is needed to index data and j to query
            // only the adequate bits.
            uint8_t i = ((paddr & paddr_mask) >> LOG2_BLOCK_SIZE) / 4,
                    j = ((paddr & paddr_mask) >> LOG2_BLOCK_SIZE) % 4;

            // Sanity checks.
            assert(i < BLOCK_SIZE && j < 4);

            // First, we must clear the bits...
            this->data[i] &= ~(0x3 << (2 * j));

            // New we can set the bits.
            this->data[i] |= (loc << (2 * j));
        }

        void distribution() {
            const std::size_t cuts = 64;

            // Building an histogram of the predictions.
            for (std::size_t k = 1; k <= cuts; k++) {
                double mean = 0.0f, variance = 0.0f;

                std::vector<uint32_t> freq(4, 0);
                std::discrete_distribution<> dist;

                for (std::size_t i = 0; i < BLOCK_SIZE / k; i++) {
                    for (std::size_t j = 0; j < 4; j++) {
                        std::size_t loc =
                            (this->data[i + cuts * k] >> (2 * j)) & 0x3;

                        freq[loc]++;
                    }
                }

                dist = std::discrete_distribution<>(freq.begin(), freq.end());

                // Computing the mean.
                for (std::size_t i = 0; i < dist.probabilities().size(); i++) {
                    mean += static_cast<double>(i) * dist.probabilities()[i];
                }

                for (std::size_t i = 0; i < dist.probabilities().size(); i++) {
                    variance += std::pow(dist.probabilities()[i] - mean, 2);
                }

                variance =
                    std::sqrt(variance /
                              static_cast<double>(dist.probabilities().size()));

                std::cout << dist << " " << variance / mean << " " << variance
                          << " " << mean << std::endl;
            }

            std::cout << std::endl;
        }

        bool operator==(const uint64_t& tag) {
            return (this->tag == tag) && valid;
        }
    };

    struct popular_level_detector {
       public:
        std::map<cc::cache_type, uint32_t> pld_counters;
        std::vector<uint32_t> thresholds;

       public:
        popular_level_detector() : thresholds({64, 128}) {
            for (const cc::cache_type& e :
                 {cc::is_l2c, cc::is_llc, cc::is_dram}) {
                this->pld_counters.insert(std::make_pair(e, 0U));
            }
        }

        void report_hit(const cc::cache_type& type) {
            std::map<cc::cache_type, uint32_t>::iterator it =
                this->pld_counters.find(type);

            // TODO: Throw an exception here that is not a valid update.
            if (it == this->pld_counters.end()) {
            }

            for (auto& [first, second] : this->pld_counters) {
                if (first == type) {
                    if (second < UINT32_MAX) second++;
                } else {
                    if (second > 0U) second--;
                }
            }
        }

        cc::block_location predict() {
            static std::map<cc::cache_type, cc::block_location> conv_map = {
                {cc::is_l2c, cc::is_in_l2c},
                {cc::is_llc, cc::is_in_llc},
                {cc::is_dram, cc::is_in_dram},
            };

            std::map<cc::cache_type, uint32_t>::iterator it = std::max_element(
                this->pld_counters.begin(), this->pld_counters.end(),
                [](const auto& a, const auto& b) -> bool {
                    return a.second < b.second;
                });

            return conv_map[it->first];
        }

        /**
         * @brief Predicts to which memory system a packet must be forwarded to
         * using perfect knowledge of the actual location of data blocks.
         *
         * @return cc::block_location
         */
        cc::block_location predict_perfect(const uint64_t& paddr) {
            bool is_in_l2c = metadata_cache::_check_prediction_l2c(paddr, 0),
                 is_in_llc = metadata_cache::_check_prediction_llc(paddr);

            if (!is_in_l2c && !is_in_llc) {
                return cc::is_in_dram;
            } else if (is_in_l2c && is_in_llc) {
                return cc::is_in_both;
            } else if (is_in_l2c && !is_in_llc) {
                return cc::is_in_l2c;
            } else {
                return cc::is_in_llc;
            }
        }

        /**
         * @brief Predicts to which memory system(s) a packet must be
         * forwarded to.
         *
         * @return std::list<cc::block_location> A list of all the destination
         * to forward a packet to.
         */
        std::list<cc::block_location> predict_levels() {
            bool predicted = false;
            uint64_t i = 0, acc = 0;
            std::list<cc::block_location> dests;
            std::map<cc::cache_type, uint32_t> cnt_cpy = this->pld_counters;
            static std::map<cc::cache_type, cc::block_location> conv_map = {
                {cc::is_l2c, cc::is_in_l2c},
                {cc::is_llc, cc::is_in_llc},
                {cc::is_dram, cc::is_in_dram},
            };

            while (!predicted && !cnt_cpy.empty()) {
                // We get the greatest element of the counters list.
                std::map<cc::cache_type, uint32_t>::iterator it =
                    std::max_element(cnt_cpy.begin(), cnt_cpy.end(),
                                     [](const auto& a, const auto& b) -> bool {
                                         return a.second < b.second;
                                     });

                acc += it->second;

                // We compare that (these) counter(s) to the i-th threshold.
                if (acc >= this->thresholds[i]) {
                    predicted = true;
                }

                dests.push_back(conv_map[it->first]);
                i++;
                cnt_cpy.erase(
                    it);  // We remove that element from the counters list.

                // If we finally reached a prediction, we return the result.
                if (predicted) {
                    break;
                }
            }

            // At this point we went over all the possible targets. We can
            // return the predicted destinations.
            return dests;
        }
    };

    /**
     * @brief
     *
     */
    struct pc_based_predictor {
       public:
        uint8_t _counters_bits;
        int32_t _counters_min, _counters_max, _threshold, _high_conf_threshold,
            _psel;
        uint64_t true_pos, false_pos, false_neg, true_neg, train_count,
            l1d_hits_on_offchip_pred, l2c_hits_on_offchip_pred,
            llc_hits_on_offchip_pred;
        std::size_t _size;
        std::vector<std::map<cc::cache_type, uint32_t>> _pc_counters;
        std::vector<int32_t> _f1, _f2, _f3, _f4, _f5, _f6, _f7, _f8;
        std::deque<uint64_t> _pc_history;

       public:
        pc_based_predictor() = default;
        pc_based_predictor(const uint8_t& counters_bits,
                           const std::size_t& size)
            : _counters_bits(counters_bits), _size(size), _pc_counters(size) {
            this->_counters_max = ((1ULL << (this->_counters_bits - 1)) - 1ULL);
            this->_counters_min = -this->_counters_max - 1;
            this->_psel = 0;

            this->true_pos = 0;
            this->false_pos = 0;
            this->true_neg = 0;
            this->false_neg = 0;
            this->train_count = 0;

            this->l1d_hits_on_offchip_pred = 0;
            this->l2c_hits_on_offchip_pred = 0;
            this->llc_hits_on_offchip_pred = 0;

            for (auto& e : this->_pc_counters) {
                for (const cc::cache_type& f :
                     {cc::is_l2c, cc::is_llc, cc::is_dram}) {
                    e.insert(std::make_pair(f, 0ULL));
                }
            }

            this->_f1 = std::vector<int32_t>(1024, 1);
            this->_f2 = std::vector<int32_t>(1024, 1);
            this->_f3 = std::vector<int32_t>(1024, 1);
            this->_f4 = std::vector<int32_t>(1024, 1);
            this->_f5 = std::vector<int32_t>(1024, 1);
            this->_f6 = std::vector<int32_t>(1024, 1);
            this->_f7 = std::vector<int32_t>(1024, 1);
            this->_f8 = std::vector<int32_t>(1024, 1);
        }

        void dump_info() const {
            std::cout << "[L1D OFFCHIP PERCEPTRON PREDICTOR KNOBS]" << std::endl
                      << "counters_bits: " << (uint32_t)this->_counters_bits
                      << std::endl
                      << "counters_min: " << this->_counters_min << std::endl
                      << "counters_max: " << this->_counters_max << std::endl
                      << "threshold: " << this->_threshold << std::endl
                      << "high_conf_threshold: " << this->_high_conf_threshold
                      << std::endl
                      << std::endl;
        }

        cc::block_location predict_desc(const uint64_t& pc,
                                        const LSQ_ENTRY& lq_entry);
        void report_hit(const uint64_t& pc, const cc::cache_type& type,
                        const LSQ_ENTRY& lq_entry, bool reverse = false);
        void check_and_update_act_thresh(const LSQ_ENTRY& lq_entry);
        void train(const LSQ_ENTRY& lq_entry);
        bool predict(LSQ_ENTRY& lq_entry);
        bool predict_on_prefetch(const PACKET& packet);

        void clear_stats();
        void dump_stats();

        uint64_t compute_pc_1_53_10_false(const LSQ_ENTRY&lq_entry),
            compute_pc_3_11_16_true(const LSQ_ENTRY&lq_entry),
            compute_pc_8_16_5_false(const LSQ_ENTRY&lq_entry),
            compute_pc_6_20_0_true(const LSQ_ENTRY&lq_entry),
            compute_pc_6_20_14_true(const LSQ_ENTRY&lq_entry),
            compute_pc_14_43_11_false(const LSQ_ENTRY&lq_entry),
            compute_offset_0_6_true(const LSQ_ENTRY&lq_entry),
            compute_offset_1_6_true(const LSQ_ENTRY&lq_entry);
    };

    struct prediction_descriptor {
        bool correct;
        uint64_t cycle, pc, claddr, pfn;
        uint32_t l2c_counter, llc_counter, dram_counter, paddr_feature_cnt;
    };

    struct miss_map_metrics {
        uint64_t hits, misses, correct, incorrect, pld_correct, pld_incorrect,
            llc_locmap_miss, l2c_pref_reached_dram;
        uint64_t aggreement_checks, agreements;
        uint64_t latency;
        std::map<cc::block_location, std::map<cc::block_location, uint64_t>>
            predicted_routes;
        std::map<cc::block_location, uint64_t>
            avgs_loc; /*!< Aberage location on MetaData Cache insertion. */
        std::map<uint64_t, std::pair<uint64_t, uint64_t>> per_pc_corrects;

        miss_map_metrics() = default;

        miss_map_metrics(const miss_map_metrics& o) {
            this->hits = o.hits;
            this->misses = o.misses;
            this->correct = o.correct;
            this->incorrect = o.incorrect;

            this->pld_correct = o.pld_correct;
            this->pld_incorrect = o.pld_incorrect;

            this->llc_locmap_miss = o.llc_locmap_miss;
            this->l2c_pref_reached_dram = o.l2c_pref_reached_dram;

            this->latency = o.latency;

            this->predicted_routes = o.predicted_routes;

            this->per_pc_corrects = o.per_pc_corrects;

            this->aggreement_checks = o.aggreement_checks;
            this->agreements = o.agreements;
        }

        miss_map_metrics(miss_map_metrics&& o) {
            this->hits = std::move(o.hits);
            this->misses = std::move(o.misses);
            this->correct = std::move(o.correct);
            this->incorrect = std::move(o.incorrect);

            this->pld_correct = std::move(o.pld_correct);
            this->pld_incorrect = std::move(o.pld_incorrect);

            this->llc_locmap_miss = std::move(o.llc_locmap_miss);
            this->l2c_pref_reached_dram = std::move(o.l2c_pref_reached_dram);

            this->latency = std::move(o.latency);

            this->predicted_routes = std::move(o.predicted_routes);

            this->per_pc_corrects = std::move(o.per_pc_corrects);

            this->aggreement_checks = std::move(o.aggreement_checks);
            this->agreements = std::move(o.agreements);
        }

        miss_map_metrics& operator=(const miss_map_metrics& o) {
            this->hits = o.hits;
            this->misses = o.misses;
            this->correct = o.correct;
            this->incorrect = o.incorrect;

            this->pld_correct = o.pld_correct;
            this->pld_incorrect = o.pld_incorrect;

            this->llc_locmap_miss = o.llc_locmap_miss;
            this->l2c_pref_reached_dram = o.l2c_pref_reached_dram;

            this->latency = o.latency;

            this->predicted_routes = o.predicted_routes;

            this->per_pc_corrects = o.per_pc_corrects;

            this->aggreement_checks = o.aggreement_checks;
            this->agreements = o.agreements;

            return *this;
        }

        miss_map_metrics& operator=(miss_map_metrics&& o) {
            this->hits = std::move(o.hits);
            this->misses = std::move(o.misses);
            this->correct = std::move(o.correct);
            this->incorrect = std::move(o.incorrect);

            this->llc_locmap_miss = std::move(o.llc_locmap_miss);
            this->l2c_pref_reached_dram = std::move(o.l2c_pref_reached_dram);

            this->latency = std::move(o.latency);

            this->predicted_routes = std::move(o.predicted_routes);

            this->per_pc_corrects = std::move(o.per_pc_corrects);

            this->aggreement_checks = std::move(o.aggreement_checks);
            this->agreements = std::move(o.agreements);

            return *this;
        }

        void clear() {
            this->hits = 0;
            this->misses = 0;
            this->correct = 0;
            this->incorrect = 0;

            this->pld_correct = 0;
            this->pld_incorrect = 0;

            this->llc_locmap_miss = 0;
            this->l2c_pref_reached_dram = 0;

            this->latency = 0;

            this->agreements = 0;
            this->aggreement_checks = 0;

            this->predicted_routes.clear();

            this->avgs_loc.clear();

            this->per_pc_corrects.clear();
        }
    };

    using miss_map_set = std::vector<entry>;
    using miss_map_entries = std::vector<miss_map_set>;

    struct tag_sampler_entry {
       public:
        uint32_t lru;
        uint64_t tag;
    };

    using tag_sampler_set = std::vector<tag_sampler_entry>;
    using tag_sampler = std::vector<tag_sampler_set>;

   private:
    champsim::simulator* _sim;
    O3_CPU* _cpu;
    uint32_t _sets, _ways, _length;
    miss_map_entries _entries;

    popular_level_detector _pld;
    pc_based_predictor _pbp;

    tag_sampler _l2c_tag_sampler;

    miss_map_metrics _metrics;

    std::ofstream _metrics_file;

    /* ---------- Members below refer to the LLC transaction mechanism.
     * --------- */
    bool
        _llc_transactions_enabled; /*!< Are tansactions with the LLC enabled? */
    float _miss_rate_threshold;
    uint64_t _access_cnt, _miss_cnt;

   public:
    metadata_cache() = default;
    metadata_cache(const std::size_t& sets, const std::size_t& ways,
                   const std::size_t& length);
    metadata_cache(const metadata_cache& o);
    metadata_cache(metadata_cache&& o);

    metadata_cache& operator=(const metadata_cache& o);
    metadata_cache& operator=(metadata_cache&& o);

    miss_map_metrics& metrics();
    const miss_map_metrics& metrics() const;

    void set_cpu(O3_CPU* cpu_ptr);

    void set_miss_rate_thrshold(const float& o);

    uint64_t get_offset(uint64_t paddr) const;
    uint64_t get_set(uint64_t paddr) const;
    uint64_t get_tag(uint64_t paddr) const;

    popular_level_detector& pld();
    const popular_level_detector& pld() const;

    pc_based_predictor& pbp();
    const pc_based_predictor& pbp() const;

    void mark_inserted(const uint64_t& paddr);
    void mark_evicted(const uint64_t& paddr);

    void update_metadata(const metadata_access_descriptor& desc);
    void update_llc_transaction_mechanism();

    bool is_miss(const uint64_t& paddr);
    cc::block_location predict(const uint64_t& paddr);
    locmap_prediction_descriptor predict_desc(PACKET& packet,
                                              const uint64_t& pc,
                                              const uint64_t& paddr,
                                              const uint64_t& lq_index);
    locmap_prediction_descriptor predict_desc_on_prefetch(PACKET& packet,
                                              const uint64_t& pc,
                                              const uint64_t& paddr,
                                              const uint64_t& lq_index);

    void update_replacement_state(const uint64_t& set,
                                  miss_map_set::iterator it);
    miss_map_set::iterator find_victim(const uint64_t& set);
    void allocate_entry(const uint64_t& tag, miss_map_set::iterator it);
    miss_map_set::iterator find_entry(const uint64_t& set, const uint64_t& tag);

    bool is_l2c_set_sampled(const uint64_t& set_idx) const;
    bool lookup_l2c_tag_sampler(const uint64_t& paddr) const;
    void update_l2c_tag_sampler(const uint64_t& paddr);

    void check_correctness(const PACKET& packet);

    void return_data(PACKET& packet);

   public:
    void _handle_insertion_miss(const metadata_access_descriptor& desc);
    void _handle_eviction_miss(const metadata_access_descriptor& desc);

    static bool _check_prediction_l2c(const uint64_t& addr, const uint8_t& cpu);
    static bool _check_prediction_llc(const uint64_t& addr);

   private:
    static cc::block_location _update_location(const block_location& loc,
                                               const cache_event& e);
};

/**
 * @brief Defines the structure and operation of the Location Map used for Cache
 * level prediction.
 */
class location_map {
   private:
    const uint64_t _block_coverage, _block_coverage_size, _block_count;
    uint64_t _base_addr, _map_counter;
    uint8_t* _locmap_data;
    std::vector<metadata_cache::entry> _entries;
    std::map<uint64_t, uint64_t> _matching_map;

   public:
    location_map();
    ~location_map();

    const uint64_t& base_addr() const;

    // uint8_t* operator[](uint64_t locmap_addr);
    metadata_cache::entry& operator[](const uint64_t& locmap_addr);

    uint64_t make_locmap_addr(const uint64_t& paddr) const;
    uint64_t make_locmap_addr(const uint64_t& tag, const uint64_t& set) const;
};
}  // namespace components
}  // namespace champsim

std::ostream& operator<<(std::ostream& os,
                         const champsim::components::metadata_cache& mm);
std::ostream& operator<<(
    std::ostream& os,
    const champsim::components::metadata_cache::miss_map_metrics& mmm);
std::ofstream& operator<<(
    std::ofstream& ofs,
    const champsim::components::metadata_cache::prediction_descriptor& desc);

#endif  // __CHAMPSIM_INTERNALS_COMPONENTS_MISS_MAP_HH__
