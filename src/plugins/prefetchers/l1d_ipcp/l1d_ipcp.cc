#include <internals/simulator.hh>
#
#include <plugins/prefetchers/l1d_ipcp/l1d_ipcp.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l1d_ipcp::l1d_ipcp() {}

cp::l1d_ipcp::l1d_ipcp(const cp::l1d_ipcp& o) : iprefetcher(o) {}

/**
 * Destructor of the class.
 */
cp::l1d_ipcp::~l1d_ipcp() {}

/**
 * @brief This method performs the actual operation of the prefetcher. This
 * method is meant to be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch
 * request.
 */
void cp::l1d_ipcp::operate(const cp::prefetch_request_descriptor& desc) {
    uint64_t curr_page = desc.addr >> LOG2_PAGE_SIZE,
             cl_addr = desc.addr >> LOG2_BLOCK_SIZE,
             cl_offset = (desc.addr >> LOG2_BLOCK_SIZE) & 0x3f;
    uint16_t signature = 0, last_signature = 0;
    int prefetch_degree = 0, spec_nl_threshold = 0, num_prefs = 0;
    uint32_t metadata = 0UL;  // TODO: Is that any useful?
    uint16_t ip_tag =
        (desc.ip >> this->_ip_index_bits) & ((1 << this->_ip_tag_bits) - 1);
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(desc.cpu);

    // These two knobs are specific to single-core simulation and might need a
    // tweak if used in the multi-core context.
    // TODO: We should consider moving them to the configuration file.
    prefetch_degree = 3;
    spec_nl_threshold = 15;

    // Update miss counter.
    if (!desc.hit) this->_num_misses++;

    // Update spec nl bit when num misses crosses certain threshold.
    if (this->_num_misses == 256) {
        this->_mpkc =
            ((float)this->_num_misses /
             (curr_cpu->current_core_cycle() - this->_prev_cpu_cycle)) *
            1000;
        this->_prev_cpu_cycle = curr_cpu->current_core_cycle();

        this->_spec_nl = !(this->_mpkc > spec_nl_threshold);
        this->_num_misses = 0;
    }

    int index = desc.ip & ((1 << this->_ip_index_bits) - 1);

    if (this->_trackers[index].ip_tag != ip_tag) {  // new/conflict IP.
        if (!this->_trackers[index].ip_valid) {
            this->_trackers[index].ip_tag = ip_tag;
            this->_trackers[index].last_page = curr_page;
            this->_trackers[index].last_cl_offset = cl_offset;
            this->_trackers[index].last_stride = 0;
            this->_trackers[index].signature = 0;
            this->_trackers[index].conf = 0;
            this->_trackers[index].str_valid = false;
            this->_trackers[index].str_strength = false;
            this->_trackers[index].str_dir = false;
            this->_trackers[index].ip_valid = true;
        } else {
            this->_trackers[index].ip_valid = false;
        }

        // Issue a next line prefetcher upon encountering new IP.
        uint64_t pf_address = ((desc.addr >> LOG2_BLOCK_SIZE) + 1)
                              << LOG2_BLOCK_SIZE;
        // metadata = ...?

        if ((pf_address >> LOG2_PAGE_SIZE) == (desc.addr >> LOG2_PAGE_SIZE))
            this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip,
                                             desc.addr, pf_address,
                                             cc::cache::fill_l1, desc.went_offchip_pred);

        // Stats...
        this->_stats.misses++;

        return;
    } else {  // If same IP encountered, set valid bit.
        this->_trackers[index].ip_valid = true;
    }

    // Calculate the stride between the current address and the last address.
    int64_t stride = 0;
    if (cl_offset > this->_trackers[index].last_cl_offset) {
        stride = cl_offset - this->_trackers[index].last_cl_offset;
    } else {
        stride = this->_trackers[index].last_cl_offset - cl_offset;
        stride *= -1;
    }

    // Don't do anything if same address is seen twice in a row.
    if (stride == 0) return;

    // Page boundary learning.
    if (curr_page != this->_trackers[index].last_page) {
        if (stride < 0)
            stride += 64;
        else
            stride -= 64;
    }

    // Update Constant Stride (CS) confidence.
    this->_update_confidence(this->_trackers[index].conf, stride,
                             this->_trackers[index].last_stride);

    // Update CS only if confidence is zero.
    if (this->_trackers[index].conf == 0)
        this->_trackers[index].last_stride = stride;

    last_signature = this->_trackers[index].signature;

    // Update complex stride (CPLX) confidence.
    this->_update_confidence(this->_dpt[last_signature].conf, stride,
                             this->_dpt[last_signature].delta);

    // Update CPLX only if confidence is zero.
    if (this->_dpt[last_signature].conf == 0)
        this->_dpt[last_signature].delta = stride;

    // Calculate and update new signature in IP table.
    signature = this->_compute_signature(last_signature, stride);
    this->_trackers[index].signature = signature;

    // Check GHB for stream IP.
    this->_check_for_stream(index, cl_addr);

    if (this->_trackers[index].str_valid) {  // IP stream.
        // For stream, prefetch with twice the usual degree.
        prefetch_degree *= 2;

        for (int i = 0; i < prefetch_degree; i++) {
            uint64_t pf_address = 0ULL;

            if (this->_trackers[index].str_dir) {  // +ve stream
                pf_address = (cl_addr + i + 1) << LOG2_BLOCK_SIZE;
            } else {  // -ve stream
                pf_address = (cl_addr - i - 1) << LOG2_BLOCK_SIZE;
            }

            // Check if prefetch address is in same 4KB page.
            if ((pf_address >> LOG2_PAGE_SIZE) == (desc.addr >> LOG2_PAGE_SIZE))
                this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip,
                                                 desc.addr, pf_address,
                                                 cc::cache::fill_l1, desc.went_offchip_pred);

            num_prefs++;
        }

        // Stats...
        this->_stats.str++;
    } else if (this->_trackers[index].conf > 1 &&
               this->_trackers[index].last_stride != 0) {  // CS IP.
        for (int i = 0; i < prefetch_degree; i++) {
            uint64_t pf_address =
                (cl_addr + (this->_trackers[index].last_stride * (i + 1)))
                << LOG2_BLOCK_SIZE;

            // Check if prefetch address is in same 4KB page.
            if ((pf_address >> LOG2_PAGE_SIZE) == (desc.addr >> LOG2_PAGE_SIZE))
                this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip,
                                                 desc.addr, pf_address,
                                                 cc::cache::fill_l1, desc.went_offchip_pred);

            num_prefs++;
        }

        // Stats...
        this->_stats.cs++;
    } else if (this->_dpt[signature].conf >= 0 &&
               this->_dpt[signature].delta !=
                   0) {  // If conf >= 0, continue looking for delta CPLX IP.
        int pref_offset = 0, i = 0;
        int bpbp = 0;

        for (i = 0; i < prefetch_degree; i++) {
            bpbp = 0;
            pref_offset += this->_dpt[signature].delta;
            uint64_t pf_address = ((cl_addr + pref_offset) << LOG2_BLOCK_SIZE);

            // Check if prefetch address is in same 4KB page.
            if (((pf_address >> LOG2_PAGE_SIZE) ==
                 (desc.addr >> LOG2_PAGE_SIZE)) ||
                (this->_dpt[signature].conf == -1) ||
                (this->_dpt[signature].delta == 0)) {
                bpbp = 1;
            }

            if (this->_dpt[signature].conf > 0 && bpbp == 0) {
                this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip,
                                                 desc.addr, pf_address,
                                                 cc::cache::fill_l1, desc.went_offchip_pred);
                num_prefs++;
            }

            signature = this->_compute_signature(signature,
                                                 this->_dpt[signature].delta);
        }

        // Stats...
        this->_stats.cplx++;
    }

    // If no prefetches are issues till now, speculatively issue a next_line
    // prefetch.
    if (num_prefs == 0 && this->_spec_nl == 1) {  // NL IP
        uint64_t pf_address = ((desc.addr >> LOG2_BLOCK_SIZE) + 1)
                              << LOG2_BLOCK_SIZE;

        this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip,
                                         desc.addr, pf_address,
                                         cc::cache::fill_l1, desc.went_offchip_pred);

        // Stats...
        this->_stats.nl++;
    }

    // Update the IP table entries.
    this->_trackers[index].last_cl_offset = cl_offset;
    this->_trackers[index].last_page = curr_page;

    // Update GHB, search for matching cl_addr.
    int ghb_index = 0;
    for (ghb_index = 0; ghb_index < this->_ghb_size; ghb_index++) {
        if (cl_addr == this->_ghb[ghb_index]) break;
    }

    // Only update the GHB upon finding new cl classes.
    if (ghb_index == this->_ghb_size) {
        for (ghb_index = this->_ghb_size - 1; ghb_index > 0; ghb_index--) {
            this->_ghb[ghb_index] = this->_ghb[ghb_index - 1];
        }

        this->_ghb[0] = cl_addr;
    }
}

void cp::l1d_ipcp::clear_stats() {
    this->_stats = ipcp_stats();
}

void cp::l1d_ipcp::dump_stats() {
    std::cout << "[IPCP Prefetcher]" << std::endl
              << "misses: " << this->_stats.misses << std::endl
              << "constant_stride: " << this->_stats.cs << std::endl
              << "complex_stride: " << this->_stats.cplx << std::endl
              << "stream: " << this->_stats.str << std::endl
              << "next_line: " << this->_stats.nl << std::endl
              << std::endl;
}

cp::l1d_ipcp* cp::l1d_ipcp::clone() { return new l1d_ipcp(*this); }

/**
 * This method is used to create an instance of the prefetcher and provide it to
 * the performance model.
 */
cp::iprefetcher* cp::l1d_ipcp::create_prefetcher() {
    return new cp::l1d_ipcp();
}

void cp::l1d_ipcp::_init(const pt::ptree& props, cc::cache* cache_inst) {
    // Calling the version of the parent class first.
    cp::iprefetcher::_init(props, cache_inst);

    // Getting knobs from from the configuration file.
    this->_ip_table_size = props.get<uint64_t>("ip_table_size");
    this->_ghb_size = props.get<uint64_t>("ghb_size");
    this->_ip_index_bits = props.get<uint64_t>("ip_index_bits");
    this->_ip_tag_bits = props.get<uint64_t>("ip_tag_bits");

    // Now that we ahve retrieved the knobs, we initialize data structures.
    this->_trackers = ip_tracker(this->_ip_table_size, ip_tracker_entry());
    this->_dpt = delta_prediction_table(4096, delta_prediction_entry());
    this->_ghb = std::vector<uint64_t>(this->_ghb_size, 0ULL);
}

uint16_t cp::l1d_ipcp::_compute_signature(const uint16_t& old_sig, int delta) {
    uint16_t new_sig = 0;
    int sig_delta = 0;

    // 7-bit sign magnitude form, since we need to tracj deltas from +63 to -63.
    sig_delta = (delta < 0) ? (((-1) * delta) + (1 << 6)) : delta;
    new_sig = ((old_sig << 1) ^ sig_delta) & 0xfff;

    return new_sig;
}

void cp::l1d_ipcp::_check_for_stream(const uint64_t& index,
                                     const uint64_t& cl_addr) {
    int pos_count = 0, neg_count = 0, count = 0;
    uint64_t check_addr = cl_addr;

    // Check for +ve stream.
    for (uint64_t i = 0; i < this->_ghb_size; i++) {
        check_addr--;
        for (uint64_t j = 0; j < this->_ghb_size; j++) {
            if (check_addr == this->_ghb[j]) {
                pos_count++;
                break;
            }
        }
    }

    check_addr = cl_addr;

    // check for -ve stream.
    for (uint64_t i = 0; i < this->_ghb_size; i++) {
        check_addr++;
        for (uint64_t j = 0; j < this->_ghb_size; j++) {
            if (check_addr == this->_ghb[j]) {
                neg_count++;
                break;
            }
        }
    }

    if (pos_count > neg_count) {  // Stream direction is +ve.
        this->_trackers[index].str_dir = true;
        count = pos_count;
    } else {  // Stream direction is -ve.
        this->_trackers[index].str_dir = false;
        count = neg_count;
    }

    if (count > this->_ghb_size / 2) {  // Stream is detected.
        this->_trackers[index].str_valid = true;

        if (count > ((this->_ghb_size * 3) / 4))
            this->_trackers[index].str_strength = true;
    } else {
        if (!this->_trackers[index].str_strength)
            this->_trackers[index].str_valid = false;
    }
}

void cp::l1d_ipcp::_update_confidence(int& confidence, const int& stride,
                                      const int& pred_stride) {
    if (stride ==
        pred_stride) {  // Use 2-bit saturating counter for confidence.
        if (confidence < 3) confidence++;
    } else {
        if (confidence > 0) confidence--;
    }
}
