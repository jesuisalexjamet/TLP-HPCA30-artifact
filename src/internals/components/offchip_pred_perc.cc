#include <algorithm>
#
#include <internals/block.h>
#
#include <internals/components/offchip_pred_perc.hh>
#include <internals/simulator.hh>

namespace cc = champsim::components;

cc::offchip_predictor_perceptron::offchip_predictor_perceptron()
    : _cpu(0),
      _page_buffer_sets(64),
      _page_buffer_ways(16),
      _pf_page_buffer_sets(64),
      _pf_page_buffer_ways(16) {
    // stats
    this->_true_pos = 0;
    this->_false_pos = 0;
    this->_true_neg = 0;
    this->_false_neg = 0;

    // perceptron predictor
    this->_pred = new cc::perceptron_predictor({5, 8, 9, 11, 16}, -17);
    this->_pf_pred = new cc::perceptron_predictor({5, 8, 9, 11, 16}, -17);

    this->_page_buffer = std::vector<std::deque<cc::page_buffer_entry *>>(
        this->_page_buffer_sets, std::deque<cc::page_buffer_entry *>(0));

    this->_pf_page_buffer = std::vector<std::deque<cc::page_buffer_entry *>>(
        this->_pf_page_buffer_sets, std::deque<cc::page_buffer_entry *>(0));

    this->_stlb_phist = std::vector<std::vector<uint32_t>>(
        0x40, std::vector<uint32_t>(0x40, 0));
}

void cc::offchip_predictor_perceptron::set_cpu(const std::size_t &idx) {
    this->_cpu = idx;
}

void cc::offchip_predictor_perceptron::set_pf_pred(
    const float &threshold, const std::vector<uint32_t> &features) {
    // First, we get rid of the perceptron that is already there.
    delete this->_pf_pred;

    // Second, we replace it by a new perceptron that uses the provided
    // features.
    this->_pf_pred = new cc::perceptron_predictor(features, threshold);
}

void cc::offchip_predictor_perceptron::set_pred(
    const float &tau_1, const float &tau_2,
    const std::vector<uint32_t> &features) {
    delete this->_pred;

    this->_pred = new cc::perceptron_predictor(features, tau_2);

    this->_tau_1 = tau_1;
    this->_tau_2 = tau_2;
}

cc::uarch_state_info *cc::offchip_predictor_perceptron::_get_state(
    ooo_model_instr *archi_instr, const std::size_t &data_index,
    LSQ_ENTRY *lq_entry) {
    cc::uarch_state_info *info = new cc::uarch_state_info;

    // filling the state info structure
    info->pc = lq_entry->ip;
    info->data_index = data_index;
    info->vaddr = lq_entry->virtual_address;
    info->vpage = info->vaddr >> LOG2_PAGE_SIZE;
    info->voffset = (info->vaddr >> LOG2_BLOCK_SIZE) &
                    ((1ULL << (LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE)) - 1ULL);
    info->v_cl_offset = info->vaddr & ((1ULL << LOG2_BLOCK_SIZE) - 1ULL);
    info->v_cl_word_offset = info->v_cl_offset >> 2;
    info->v_cl_dword_offset = info->v_cl_offset >> 4;

    this->_lookup_address(info->vaddr, info->vpage, info->voffset,
                          info->first_access);
    this->_get_control_flow_signatures(lq_entry, info->last_n_load_pc_sig,
                                       info->last_n_pc_sig,
                                       info->last_n_vpn_sig);

    return info;
}

void cc::offchip_predictor_perceptron::_get_state_on_prefetch(
    PACKET &pf_packet) {
    // WIP: Here, instead of allocating dynamic memory, we write the info
    // directly into the appropriate field of the pf_packet.
    cc::uarch_state_info &info = pf_packet.info;

    info.pc = pf_packet.ip;
    info.vaddr = pf_packet.full_addr;
    info.vpage = info.vaddr >> LOG2_BLOCK_SIZE;
    info.voffset = (info.vaddr >> LOG2_BLOCK_SIZE) &
                   ((1ULL << (LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE)) - 1ULL);
    info.v_cl_offset = info.vaddr & ((1ULL << LOG2_BLOCK_SIZE) - 1ULL);
    info.v_cl_word_offset = info.v_cl_word_offset >> 2;
    info.v_cl_dword_offset = info.v_cl_dword_offset >> 4;

    info.went_offchip_pred = pf_packet.went_offchip_pred;

    // TODO: Getting control flow signatures on prefetches seems off as of now.
    this->_lookup_address_on_prefetch(info.vaddr, info.vpage, info.voffset,
                                      info.first_access);
}

/**
 * @brief
 *
 * @param vaddr
 * @param vpage
 * @param voffset
 * @param first_access
 */
void cc::offchip_predictor_perceptron::_lookup_address(const uint64_t &vaddr,
                                                       const uint64_t &vpage,
                                                       const uint32_t voffset,
                                                       bool &first_access) {
    cc::page_buffer_entry *entry = nullptr;
    uint32_t set = this->_get_set(vpage);

    auto it = std::find_if(
        this->_page_buffer[set].begin(), this->_page_buffer[set].end(),
        [vpage](cc::page_buffer_entry *e) -> bool { return e->page == vpage; });

    if (it != this->_page_buffer[set].end()) {
        entry = *it;
        first_access = !entry->bmp_access.test(voffset);
        entry->bmp_access.set(voffset);
        entry->age = 0;
        this->_page_buffer[set].erase(it);
        this->_page_buffer[set].push_back(entry);
    } else {
        if (this->_page_buffer[set].size() >= this->_page_buffer_ways) {
            entry = this->_page_buffer[set].front();
            this->_page_buffer[set].pop_front();
            delete entry;
        }

        entry = new cc::page_buffer_entry;
        entry->page = vpage;
        entry->bmp_access.set(voffset);
        entry->age = 0;
        this->_page_buffer[set].push_back(entry);
        first_access = true;
    }
}

void cc::offchip_predictor_perceptron::_lookup_address_on_prefetch(
    const uint64_t &vaddr, const uint64_t &vpage, const uint32_t voffset,
    bool &first_access) {
    cc::page_buffer_entry *entry = nullptr;
    uint32_t set = this->_get_set(vpage);

    auto it = std::find_if(
        this->_pf_page_buffer[set].begin(), this->_pf_page_buffer[set].end(),
        [vpage](cc::page_buffer_entry *e) -> bool { return e->page == vpage; });

    if (it != this->_pf_page_buffer[set].end()) {
        entry = *it;
        first_access = !entry->bmp_access.test(voffset);
        entry->bmp_access.set(voffset);
        entry->age = 0;
        this->_pf_page_buffer[set].erase(it);
        this->_pf_page_buffer[set].push_back(entry);
    } else {
        if (this->_pf_page_buffer[set].size() >= this->_pf_page_buffer_ways) {
            entry = this->_pf_page_buffer[set].front();
            this->_pf_page_buffer[set].pop_front();
            delete entry;
        }

        entry = new cc::page_buffer_entry;
        entry->page = vpage;
        entry->bmp_access.set(voffset);
        entry->age = 0;
        this->_pf_page_buffer[set].push_back(entry);
        first_access = true;
    }
}

uint32_t cc::offchip_predictor_perceptron::_get_set(
    const uint64_t &vpage) const {
    uint32_t hash = fnv1a64(vpage);
    return hash % this->_page_buffer_sets;
}

/**
 * @brief
 *
 * @param lq_entry
 * @param last_n_load_pc_sig
 * @param last_n_pc_sig
 */
void cc::offchip_predictor_perceptron::_get_control_flow_signatures(
    LSQ_ENTRY *lq_entry, uint64_t &last_n_load_pc_sig, uint64_t &last_n_pc_sig,
    uint64_t &last_n_vpn) {
    uint64_t curr_pc = lq_entry->ip;

    // TODO: Here we hardcode the number of load PCs to use but we should
    // parametrize this.
    if (this->_last_n_load_pc.size() >= 4) {
        this->_last_n_load_pc.pop_front();
    }
    this->_last_n_load_pc.push_back(curr_pc);

    last_n_load_pc_sig = 0ULL;

    for (const uint64_t &pc : this->_last_n_load_pc) {
        last_n_load_pc_sig <<= 1;
        last_n_load_pc_sig ^= pc;
    }

    // signature from all N instruction PCs.
    int32_t prior = lq_entry->rob_index;
    std::deque<uint64_t> last_n_pcs;

    // TODO: Here we hardcode the number of PCs to use but we should parametrize
    // this.
    for (std::size_t i = 0; i < 4; i++) {
        last_n_pcs.push_front(champsim::simulator::instance()
                                  ->modeled_cpu(this->_cpu)
                                  ->ROB.entry[prior]
                                  .ip);

        prior--;

        if (prior < 0) prior = ROB_SIZE - 1;
    }

    last_n_pc_sig = 0ULL;
    for (const uint64_t &pc : last_n_pcs) {
        last_n_pc_sig <<= 1;
        last_n_pc_sig ^= pc;
    }

    // TODO: Here we hardcode the number of VPNs to use but we should
    // parametrize this.
    if (this->_last_n_vpn.size() >= 4) {
        this->_last_n_vpn.pop_front();
    }
    this->_last_n_vpn.push_back(lq_entry->physical_address >> LOG2_PAGE_SIZE);

    last_n_vpn = 0ULL;

    for (const uint64_t &vpn : this->_last_n_vpn) {
        last_n_vpn <<= 1;
        last_n_vpn ^= vpn;
    }
}

void cc::offchip_predictor_perceptron::dump_stats() const {
    std::cout << "Popet stats" << std::endl
              << "perc_true_pos " << this->_true_pos << std::endl
              << "perc_false_pos " << this->_false_pos << std::endl
              << "perc_false_neg " << this->_false_neg << std::endl
              << "perc_true_neg " << this->_true_neg << std::endl
              << "perc_true_pos_pf " << this->_true_pos_pf << std::endl
              << "perc_false_pos_pf " << this->_false_pos_pf << std::endl
              << "perc_false_neg_pf " << this->_false_neg_pf << std::endl
              << "perc_true_neg_pf " << this->_true_neg_pf << std::endl
              << "miss_hit_l1d " << this->_miss_hit_l1d << std::endl
              << "miss_hit_l2c " << this->_miss_hit_l2c << std::endl;
}

void cc::offchip_predictor_perceptron::reset_stats() {
    this->_true_pos = 0;
    this->_true_neg = 0;
    this->_false_neg = 0;
    this->_false_pos = 0;

    this->_true_pos_pf = 0;
    this->_false_pos_pf = 0;
    this->_false_neg_pf = 0;
    this->_true_neg_pf = 0;

    this->_miss_hit_l1d = 0;
    this->_miss_hit_l2c = 0;
}

bool cc::offchip_predictor_perceptron::predict(ooo_model_instr *arch_instr,
                                               const std::size_t &data_index,
                                               LSQ_ENTRY *lq_entry) {
    bool prediction = false;

    // save all necessary data that would later be required for training on the
    // LQ entry.
    lq_entry->perc_feature = new cc::perceptron_feature;
    lq_entry->perc_feature->info =
        this->_get_state(arch_instr, data_index, lq_entry);

    // predict
    this->_pred->predict(lq_entry->perc_feature->info, prediction,
                         lq_entry->perc_feature->perceptron_weights_sum);

    return prediction;
}

bool cc::offchip_predictor_perceptron::predict_on_prefetch(PACKET &pf_packet) {
    bool prediction = false;

    // save all necessary data that would late rbe required for training in the
    // prefetch packet.
    this->_get_state_on_prefetch(pf_packet);

    this->_pf_pred->predict(&pf_packet.info, prediction,
                            pf_packet.perceptron_weights_sum);

    return prediction;
}

bool cc::offchip_predictor_perceptron::predict_on_stlb_pte(
    const uint64_t &pc, const uint64_t &vpage) {
    const uint64_t idx_1 = jenkins_hash(folded_xor(pc, 2)) % 0x40,
                   idx_2 = jenkins_hash(folded_xor(vpage, 2)) % 0x40;

    return (this->_stlb_phist[0][idx_2] > 15);
}

void cc::offchip_predictor_perceptron::train(ooo_model_instr *arch_instr,
                                             const std::size_t &data_index,
                                             LSQ_ENTRY *lq_entry) {
    bool actual_prediction = lq_entry->went_offchip_pred;

    // stats
    if (actual_prediction && lq_entry->went_offchip)
        this->_true_pos++;
    else if (actual_prediction && !lq_entry->went_offchip)
        this->_false_pos++;
    else if (!actual_prediction && lq_entry->went_offchip)
        this->_false_neg++;
    else if (!actual_prediction && !lq_entry->went_offchip)
        this->_true_neg++;

#if defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))
    if (lq_entry->offchip_pred_hit_l1d) this->_miss_hit_l1d++;
    if (lq_entry->offchip_pred_hit_l2c) this->_miss_hit_l2c++;
#endif  // defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))

    // activation threshold update
    this->_pred->train(lq_entry->perc_feature->info,
                       lq_entry->perc_feature->perceptron_weights_sum,
                       lq_entry->went_offchip_pred, lq_entry->went_offchip);
}

void cc::offchip_predictor_perceptron::train_on_prefetch(PACKET &pf_packet) {
    bool actual_prediction = pf_packet.pf_went_offchip_pred;

    // stats
    if (actual_prediction && pf_packet.pf_went_offchip)
        this->_true_pos_pf++;
    else if (actual_prediction && !pf_packet.pf_went_offchip)
        this->_false_pos_pf++;
    else if (!actual_prediction && pf_packet.pf_went_offchip)
        this->_false_neg_pf++;
    else if (!actual_prediction && !pf_packet.pf_went_offchip)
        this->_true_neg_pf++;

    // weithgs update.
    this->_pf_pred->train(&pf_packet.info, pf_packet.perceptron_weights_sum,
                          pf_packet.pf_went_offchip_pred,
                          pf_packet.pf_went_offchip);
}

bool cc::offchip_predictor_perceptron::consume_from_core(
    const std::size_t &lq_index) const {
#if defined(ENABLE_FSP) && !defined(ENABLE_DELAYED_FSP)
    LSQ_ENTRY &entry = champsim::simulator::instance()
                           ->modeled_cpu(this->_cpu)
                           ->LQ.entry[lq_index];

    return (entry.perc_feature->perceptron_weights_sum >= this->_tau_1);
#else
    return false;
#endif
}

bool cc::offchip_predictor_perceptron::consume_from_l1d(
    const std::size_t &lq_index) const {
#if defined(ENABLE_FSP) && \
    (defined(ENABLE_BIMODAL_FSP) || defined(ENABLE_DELAYED_FSP))
    LSQ_ENTRY &entry = champsim::simulator::instance()
                           ->modeled_cpu(this->_cpu)
                           ->LQ.entry[lq_index];

    return (entry.perc_feature->perceptron_weights_sum >= this->_tau_2);
#else
    return false;
#endif
}
