#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_OFFCHIP_PRED_PERC_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_OFFCHIP_PRED_PERC_HH__

#include <cstdint>
#
#include <deque>
#include <vector>
#
#include <internals/bitmap.h>

class LSQ_ENTRY;

namespace champsim {
namespace components {
enum perceptron_feature_type {
    pc = 0,
    offset = 1,
    page = 2,
    addr = 3,
    first_access = 4,
    pc_offset = 5,
    pc_page = 6,
    pc_addr = 7,
    pc_first_access = 8,
    offset_first_acces = 9,
    cl_offset = 10,
    pc_cl_offset = 11,
    cl_word_offset = 12,
    pc_cl_word_offset = 13,
    cl_double_word_offset = 14,
    pc_cl_double_word_offset = 15,
    last_n_load_pcs = 16,
    last_n_pcs = 17,

    // WIP: Extending features to link the FSP tot the SSP.
    fsp_bit = 18,
    pc_fsp_bit = 19,
    offset_fsp_bit = 20,

    delayed_bit = 21,
    pc_delayed_bit = 22,
    offset_delayed_bit = 23,
};

struct page_buffer_entry {
   public:
    uint64_t page;
    Bitmap bmp_access;
    uint32_t age;

   public:
    page_buffer_entry() = default;
};

struct perceptron_feature {
   public:
    float perceptron_weights_sum;
    uarch_state_info *info;

   public:
    perceptron_feature() : perceptron_weights_sum(0.0f), info(nullptr) {}

    ~perceptron_feature() {
        if (info) delete info;
    }
};

class perceptron_predictor {
   private:
    float _threshold, _max_w, _min_w, _pos_delta, _neg_delta, _pos_threshold,
        _neg_threshold;
    std::vector<uint32_t> _activated_features, _weight_array_sizes;
    std::vector<std::vector<float>> _weights_arrays;

   public:
    static uint32_t _process_pc(uarch_state_info *info,
                                const std::size_t &array_size) {
        uint32_t val = folded_xor(info->pc, 2);
        val = jenkins_hash(val);

        return val % array_size;
    }

    static uint32_t _process_offset(uarch_state_info *info,
                                    const std::size_t &array_size) {
        uint32_t val = folded_xor(info->voffset, 2);
        val = jenkins_hash(val);

        return val % array_size;
    }

    static uint32_t _process_page(uarch_state_info *info,
                                  const std::size_t &array_size) {
        uint32_t val = folded_xor(info->vpage, 2);
        val = jenkins_hash(val);

        return val % array_size;
    }

    static uint32_t _process_pc_offset(uarch_state_info *info,
                                       const std::size_t &array_size) {
        uint32_t val = folded_xor(info->pc, 2);
        val <<= 6;
        val += info->voffset;
        val = jenkins_hash(val);

        return val % array_size;
    }

    static uint32_t _process_pc_first_access(uarch_state_info *info,
                                             const std::size_t &array_size) {
        uint32_t val = folded_xor(info->pc, 2);
        val &= ((1U << 31) - 1);

        if (info->first_access) val |= (1U << 31);

        return jenkins_hash(val) % array_size;
    }

    static uint32_t _process_offset_first_access(
        uarch_state_info *info, const std::size_t &array_size) {
        uint32_t val = info->voffset;
        val &= ((1U << 6) - 1);

        if (info->first_access) val |= (1U << 6);

        return jenkins_hash(val) % array_size;
    }

    static uint32_t _process_pc_cl_offset(uarch_state_info *info,
                                          const std::size_t &array_size) {
        uint32_t val = folded_xor(info->pc, 2);
        val <<= 6;
        val += info->v_cl_offset;

        return jenkins_hash(val) % array_size;
    }

    static uint32_t _process_last_n_loads_pcs(uarch_state_info *info,
                                              const std::size_t &array_size) {
        return jenkins_hash(folded_xor(info->last_n_load_pc_sig, 2)) %
               array_size;
    }

    static uint32_t _process_fsp_bit(uarch_state_info *info,
                                     const std::size_t &array_size) {
        uint32_t val = static_cast<uint32_t>(info->went_offchip_pred);

        return val % array_size;
    }

    static uint32_t _process_pc_fsp_bit(uarch_state_info *info, const std::size_t &array_size) {
        uint32_t val = folded_xor(info->pc, 2);
        val &= ((1U << 31) - 1);

        if (info->went_offchip_pred) val |= (1U << 31);

        return jenkins_hash(val) % array_size;
    }

    static uint32_t _process_offset_fsp_bit(
        uarch_state_info *info, const std::size_t &array_size) {
        uint32_t val = info->voffset;
        val &= ((1U << 6) - 1);

        if (info->went_offchip_pred) val |= (1U << 6);

        return jenkins_hash(val) % array_size;
    }

   private:
    /**
     * @brief
     *
     * @param info
     * @return std::vector<uint32_t>
     */
    std::vector<uint32_t> _generate_indices_from_info(uarch_state_info *info) {
        std::vector<uint32_t> indices;

        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            indices.push_back(this->_generate_index_from_feature(
                static_cast<perceptron_feature_type>(
                    this->_activated_features[i]),
                info, this->_weight_array_sizes[i]));
        }

        return indices;
    }

    /**
     * @brief
     *
     * @param type
     * @param info
     * @param weight_array_size
     * @return uint32_t
     */
    uint32_t _generate_index_from_feature(
        const perceptron_feature_type &type, uarch_state_info *info,
        const std::size_t &weight_array_size) {
        if (info == nullptr) return 0;

        switch (type) {
            case perceptron_feature_type::pc:
                return _process_pc(info, weight_array_size);
            case perceptron_feature_type::offset:
                return _process_offset(info, weight_array_size);
            case perceptron_feature_type::page:
                return _process_page(info, weight_array_size);
            case perceptron_feature_type::pc_offset:
                return _process_pc_offset(info, weight_array_size);
            case perceptron_feature_type::pc_first_access:
                return _process_pc_first_access(info, weight_array_size);
            case perceptron_feature_type::offset_first_acces:
                return _process_offset_first_access(info, weight_array_size);
            case perceptron_feature_type::pc_cl_offset:
                return _process_pc_cl_offset(info, weight_array_size);
            case perceptron_feature_type::last_n_load_pcs:
                return _process_last_n_loads_pcs(info, weight_array_size);

            // WIP: Extending features to link the FSP tot the SSP.
            case perceptron_feature_type::fsp_bit:
                return _process_fsp_bit(info, weight_array_size);
            case perceptron_feature_type::pc_fsp_bit:
                return _process_pc_fsp_bit(info, weight_array_size);
            case perceptron_feature_type::offset_fsp_bit:
                return _process_offset_fsp_bit(info, weight_array_size);
            default:
                return 0;
        }
    }

    /**
     * @brief
     *
     * @param indices
     */
    void _incr_weights(const std::vector<uint32_t> &indices) {
        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            if (this->_weights_arrays[i][indices[i]] + this->_pos_delta <=
                this->_max_w) {
                this->_weights_arrays[i][indices[i]] += this->_pos_delta;
            } else {
                // log
            }
        }
    }

    void _incr_weights(const std::vector<uint32_t> &indices,
                       const cc::cache_type &loc) {
        float alpha = 1.0f;

        switch (loc) {
            case cc::cache_type::is_l1d:
                alpha = 0.25f;
                break;
            case cc::cache_type::is_l2c:
                alpha = 0.5f;
                break;
            case cc::cache_type::is_llc:
            case cc::cache_type::is_dram:
                alpha = 1.0f;
                break;
        }

        // std::cout << alpha << std::endl;

        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            if (this->_weights_arrays[i][indices[i]] +
                    (alpha * this->_pos_delta) <=
                this->_max_w) {
                this->_weights_arrays[i][indices[i]] +=
                    (alpha * this->_pos_delta);
            } else {
                // log
            }
        }
    }

    /**
     * @brief
     *
     * @param indices
     */
    void _decr_weights(const std::vector<uint32_t> &indices) {
        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            if (this->_weights_arrays[i][indices[i]] - this->_neg_delta >=
                this->_min_w) {
                this->_weights_arrays[i][indices[i]] -= this->_neg_delta;
            } else {
                // log
            }
        }
    }

    void _decr_weights(const std::vector<uint32_t> &indices,
                       const cc::cache_type &loc) {
        float alpha = 1.0f;

        switch (loc) {
            case cc::cache_type::is_l1d:
                alpha = 1.0;
                break;
            case cc::cache_type::is_l2c:
                alpha = 0.5;
                break;
            case cc::cache_type::is_llc:
            case cc::cache_type::is_dram:
                alpha = 0.25;
                break;
            default:
                break;
        }

        // std::cout << alpha << std::endl;

        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            if (this->_weights_arrays[i][indices[i]] -
                    (alpha * this->_neg_delta) >=
                this->_min_w) {
                this->_weights_arrays[i][indices[i]] -=
                    (alpha * this->_neg_delta);
            } else {
                // log
            }
        }
    }

   public:
    perceptron_predictor(const std::vector<uint32_t> &activated_features,
                         const float &threshold)
        : _activated_features(activated_features),
          // _activated_features({5, 8, 9, 11, 16}),
          _weight_array_sizes({1024, 1024, 128, 1024, 1024, 1024}),
          _threshold(threshold),
          _max_w(15),
          _min_w(-16),
          _pos_delta(1),
          _neg_delta(1),
          _pos_threshold(40),
          _neg_threshold(-35) {
        for (std::size_t index = 0; index < this->_weight_array_sizes.size();
             index++) {
            this->_weights_arrays.push_back(
                std::vector<float>(this->_weight_array_sizes[index]));
        }
    }

    ~perceptron_predictor() {}

    /**
     * @brief
     *
     * @param info
     * @param prediction
     * @param perceptron_weights_sum
     */
    void predict(uarch_state_info *info, bool &prediction,
                 float &perceptron_weights_sum) {
        float cummulative_weights = 0.0f;
        std::vector<uint32_t> weight_indices =
            this->_generate_indices_from_info(info);

        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            cummulative_weights += this->_weights_arrays[i][weight_indices[i]];
        }
        perceptron_weights_sum = cummulative_weights;

        prediction = (cummulative_weights >= this->_threshold);
    }

    void predict(uarch_state_info *info, const bool &demand_went_offchip_pred,
                 bool &prediction, float &perceptron_weights_sum) {
        float cummulative_weights = 0.0f;
        std::vector<uint32_t> weight_indices =
            this->_generate_indices_from_info(info);

        for (std::size_t i = 0; i < this->_activated_features.size(); i++) {
            cummulative_weights += this->_weights_arrays[i][weight_indices[i]];
        }

        // WIP: If demand_went_offchip_pred is true, we add a small displacement
        // to the cummulative weights that represents the odd of the prefetcher
        // request to be off-chip knowing that the associated demand request is
        // predicted to be offchip as well.
        perceptron_weights_sum = cummulative_weights;
        cummulative_weights += (demand_went_offchip_pred ? 0.0f : 5.0f);

        prediction = (cummulative_weights >= this->_threshold);
    }

    /**
     * @brief
     *
     * @param info
     * @param perceptron_weights_sum
     * @param pred_output
     * @param true_output
     */
    void train(uarch_state_info *info, float perceptron_weights_sum,
               bool pred_output, bool true_output) {
        std::vector<uint32_t> indices = this->_generate_indices_from_info(info);

        if (true_output) {
            // correctly predicted true
            if (pred_output == true_output) {
                // correcly predicted true
                if (perceptron_weights_sum >= this->_neg_threshold &&
                    perceptron_weights_sum <= this->_pos_threshold) {
                    this->_incr_weights(indices);
                } else {
                    // log
                }
            } else {
                this->_incr_weights(indices);
            }
        } else {
            // correctly predicted false
            if (pred_output == true_output) {
                if (perceptron_weights_sum >= this->_neg_threshold &&
                    perceptron_weights_sum <= this->_pos_threshold) {
                    this->_decr_weights(indices);
                } else {
                    // log
                }
            } else {
                this->_decr_weights(indices);
            }
        }
    }

    void train(uarch_state_info *info, float perceptron_weights_sum,
               bool pred_output, bool true_output, const cc::cache_type &loc) {
        std::vector<uint32_t> indices = this->_generate_indices_from_info(info);

        if (true_output) {
            // correctly predicted true
            if (pred_output == true_output) {
                // correcly predicted true
                if (perceptron_weights_sum >= this->_neg_threshold &&
                    perceptron_weights_sum <= this->_pos_threshold) {
                    this->_incr_weights(indices, loc);
                } else {
                    // log
                }
            } else {
                this->_incr_weights(indices, loc);
            }
        } else {
            // correctly predicted false
            if (pred_output == true_output) {
                if (perceptron_weights_sum >= this->_neg_threshold &&
                    perceptron_weights_sum <= this->_pos_threshold) {
                    this->_decr_weights(indices, loc);
                } else {
                    // log
                }
            } else {
                this->_decr_weights(indices, loc);
            }
        }
    }
};

class offchip_predictor_perceptron {
   private:
    uint64_t _cpu;
    uint64_t _true_pos, _false_pos, _true_neg, _false_neg, _true_pos_pf,
        _false_pos_pf, _true_neg_pf, _false_neg_pf, _miss_hit_l1d, _miss_hit_l2c;
    std::size_t _page_buffer_sets, _page_buffer_ways, _pf_page_buffer_sets,
        _pf_page_buffer_ways;
    std::deque<uint64_t> _last_n_load_pc, _last_n_vpn;
    std::vector<std::deque<page_buffer_entry *>> _page_buffer, _pf_page_buffer;
    perceptron_predictor *_pred, *_pf_pred;

    float _tau_1, _tau_2;

    std::vector<std::vector<uint32_t>> _stlb_phist;

   public:
    uarch_state_info *_get_state(ooo_model_instr *arch_instr,
                                 const std::size_t &data_index,
                                 LSQ_ENTRY *lq_entry);
    void _get_state_on_prefetch(PACKET &pf_packet);
    void _lookup_address(const uint64_t &vaddr, const uint64_t &vpage,
                         const uint32_t voffset, bool &first_access);
    void _lookup_address_on_prefetch(const uint64_t &vaddr,
                                     const uint64_t &vpage,
                                     const uint32_t voffset,
                                     bool &first_access);
    uint32_t _get_set(const uint64_t &vpage) const;
    void _get_control_flow_signatures(LSQ_ENTRY *lq_entry,
                                      uint64_t &last_n_load_pc_sig,
                                      uint64_t &last_n_pc_sig,
                                      uint64_t &last_n_vpn_sig);

   public:
    offchip_predictor_perceptron();
    
    void set_cpu(const std::size_t &idx);
    void set_pf_pred(const float &threshold,
                     const std::vector<uint32_t> &features);
    void set_pred(const float &tau_1, const float &tau_2,
                  const std::vector<uint32_t> &features);

    void dump_stats() const;
    void reset_stats();

    void train(ooo_model_instr *arch_instr, const std::size_t &data_index,
               LSQ_ENTRY *lq_entry);
    void train_on_prefetch(PACKET &pf_packet);
    bool predict(ooo_model_instr *arch_instr, const std::size_t &data_index,
                 LSQ_ENTRY *lq_entry);
    bool predict_on_prefetch(PACKET &pf_packet);

    bool predict_on_stlb_pte(const uint64_t &pc, const uint64_t &vpage);

    bool consume_from_core(const std::size_t &lq_index) const,
        consume_from_l1d(const std::size_t &lq_index) const;
};
}  // namespace components
}  // namespace champsim

#endif  // __CHAMPSIM_INTERNALS_COMPONENTS_OFFCHIP_PRED_PERC_HH__
