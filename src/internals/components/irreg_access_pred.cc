#include <iostream>
#include <iomanip>
#
#include <algorithm>
#include <utility>
#
#include <components/irreg_access_pred.hh>
#
#include <internals/champsim.h>

namespace cc = champsim::components;

cc::irreg_access_pred::irreg_access_pred (const uint32_t& entries) :
 	irreg_access_pred (1, entries) {
}

cc::irreg_access_pred::irreg_access_pred (const uint32_t& sets, const uint32_t& ways) :
    _predictor_latency (0), _threshold (0), _state (unused), _pred () {
    // Computing the # of bits required to index the cache structure.
    this->_sets_bits = static_cast<uint32_t> (std::log2 (sets));

    // Adding entries to the predictor.
    for (uint32_t i = 0; i < sets; i++) {
        // Creating a new set.
        this->_pred.push_back (prediction_table_set ());

        for (uint32_t j = 0; j < ways; j++) {
            this->_pred[i].push_back (predictor_entry ());

            this->_pred[i][j].repl_state = ways - 1;
        }
    }
}

cc::irreg_access_pred& cc::irreg_access_pred::operator= (const cc::irreg_access_pred& o) {
    this->_predictor_latency = o._predictor_latency;
    this->_threshold = o._threshold;
    this->_pred = o._pred;
    this->_metrics = o._metrics;
    this->_sets_bits = o._sets_bits;
    this->_state = o._state;
    this->_psel_caches = o._psel_caches;
    this->_psel_max_val = o._psel_max_val;

    return *this;
}

cc::irreg_access_pred& cc::irreg_access_pred::operator= (cc::irreg_access_pred&& o) {
    this->_predictor_latency = std::move (o._predictor_latency);
    this->_threshold = std::move (o._threshold);
    this->_pred = std::move (o._pred);
    this->_metrics = std::move (o._metrics);
    this->_sets_bits = std::move (o._sets_bits);
    this->_state = std::move (o._state);
    this->_psel_caches = std::move (o._psel_caches);
    this->_psel_max_val = std::move (o._psel_max_val);

    return *this;
}

void cc::irreg_access_pred::set_threshold (const uint64_t& new_threshold) {
    this->_threshold = new_threshold;
}

void cc::irreg_access_pred::set_stride_bits (const uint8_t& stride_bits) {
    this->_stride_max_val = ((1ULL << stride_bits) - 1ULL);

    // this->_threshold = (this->_stride_max_val * 3) >> 2;
    // this->_threshold = 8;
    this->_threshold = this->_stride_max_val;
}

void cc::irreg_access_pred::set_psel_bits (const uint8_t& psel_bits) {
    this->_psel_max_val = ((1ULL << psel_bits) - 1ULL);

    this->_psel_caches = (this->_psel_max_val >> 1);
}

cc::irreg_access_pred::prediction_table_set::iterator cc::irreg_access_pred::find_victim (const uint32_t& set) {
    prediction_table_set::iterator it = std::find_if (this->_pred[set].begin (), this->_pred[set].end (),
        [=, this] (const auto& e) -> bool  {
            return (!e.valid) || (e.repl_state == this->_pred[set].size () - 1);
        });

    // Sanity check.
    if (it == this->_pred[set].end ()) {
        throw std::runtime_error ("No victim could be found.");
    }

    return it;
}

void cc::irreg_access_pred::update_replacement_state (const bool& hit, const uint32_t& set, prediction_table_set::iterator it) {
    uint32_t old_repl_state = it->repl_state;

    for (prediction_table_set::iterator pred_it = this->_pred[set].begin ();
         pred_it != this->_pred[set].end ();
         ++pred_it) {
        if (pred_it->repl_state < it->repl_state) {
            pred_it->repl_state++;
        }
    }

    // Promoting the MRU position.
    it->repl_state = 0x0;

    // Let's update some additional meta-data.
    if (!hit) {
        it->valid = true;
    }
}

void cc::irreg_access_pred::update (const uint64_t& pc, const uint64_t& vaddr) {
    // Computing the set index based on the PC.
    uint32_t set_idx = this->_get_set (pc);
    uint64_t curr_stride = 0x0;

	// First, we look for an entry matching the given PC.
	prediction_table_set::iterator it = std::find_if (this->_pred[set_idx].begin (), this->_pred[set_idx].end (),
        [=] (const auto& e) -> bool {
            return (e.pc == pc);
        });

	// The netry has not been found.
	if (it == this->_pred[set_idx].end ()) {
        prediction_table_set::iterator victim_it = this->find_victim (set_idx);

        this->update_replacement_state (false, set_idx, victim_it);

        victim_it->stride = 0x0;
        victim_it->old_addr = vaddr;
        victim_it->pc = pc;

        this->_metrics.misses++;
	} else { // The entry has been found.
        // In this situation we have to trigger a couple updates to the predictor entry.
        curr_stride = std::min ((vaddr > it->old_addr) ? (vaddr - it->old_addr) : (it->old_addr - vaddr), this->_stride_max_val);

        it->stride = std::min (curr_stride + it->stride, this->_stride_max_val);
        it->stride >>= 1;
        it->old_addr = vaddr;

        this->update_replacement_state (true, set_idx, it);

        this->_metrics.hits++;
	}

    this->_metrics.accesses++;
}

bool cc::irreg_access_pred::predict (const uint64_t& pc) {
    bool p = false;

    // Computing the set index based on the PC.
    uint32_t set_idx = this->_get_set (pc);

    // First, we look for an entry identified by the given PC.
    prediction_table_set::const_iterator it = std::find_if (this->_pred[set_idx].begin (), this->_pred[set_idx].end (),
        [=] (const auto& e) -> bool {
            return (e.pc == pc);
        });

    // In this case, we assume the  PC to expose regular behaviour.
    if (it == this->_pred[set_idx].cend ()) {
        return false;
    }

    // Otherwise, we look into the stride stored in the predictor entry.
    p = (it->stride >= (this->_threshold));
    // p = (it->stride >= (this->_threshold));

    // std::clog << it->stride << " " << this->_threshold << " " << p << std::endl;

    // Computing stats on prediction changes.
    if (p != this->_prev_prediction) {
        this->_metrics.prediction_changes++;
    }

    this->_prev_prediction = p;

    // We switch state if required.
    this->_state = (this->_state == unused) ? used : unused;

    return p;
}

void cc::irreg_access_pred::feedback_l1d_path (const l1d_path_feedback_info& info) {
    bool reached_limit = false;

    // We want to decrease the PSEL when the micro-architecture triggers a signal that there is low locality.
    switch (info.packet->served_from) {
		// That was a hit in the L1D.
    	case cc::is_l1d:
            break;

        case cc::is_l2c:
            // this->_psel_caches = std::clamp (this->_psel_caches + 1, 0, CLAMP_LIMIT);
            // break;

		// That was a miss in the L1D as the access was served from another component of the memory hierarchy.
        case cc::is_llc:
            this->_psel_caches = std::clamp (this->_psel_caches + 1, 0, (int) this->_psel_max_val);
            break;

		default:
            this->_psel_caches = std::clamp (this->_psel_caches - 1, 0, (int) this->_psel_max_val);
			break;
    }

    // std::clog << this->_psel_scaches << " " << this->_threshold << std::endl;

    if (this->_psel_caches == this->_psel_max_val) {
        // this->_threshold = (this->_stride_max_val * 3) >> 2;
        // this->_threshold = (this->_stride_max_val * 1) >> 2;
        this->_threshold = std::clamp (this->_threshold * 2, (uint64_t) 8ULL, this->_stride_max_val >> 2);

        reached_limit = true;
    } else if (this->_psel_caches == 0) {
        // this->_threshold = 8;
        this->_threshold = std::clamp (this->_threshold / 2, (uint64_t) 8ULL, this->_stride_max_val >> 2);

        reached_limit = true;
    }

    if (reached_limit) {
        this->_psel_caches = (this->_psel_max_val >> 1);
    }

    // std::clog << this->_psel_caches << " " << this->_threshold << std::endl;
}

void cc::irreg_access_pred::feedback_sdc_path (const sdc_path_feedback_info& info) {
    bool reached_limit = false;

    return;

    switch (info.packet->served_from) {
        case cc::is_sdc:
            this->_psel_caches = std::clamp (this->_psel_caches + 1, 0, (int) this->_psel_max_val);
            break;

        case cc::is_dram:
            this->_psel_caches = std::clamp (this->_psel_caches - 1, 0, (int) this->_psel_max_val);
            break;
    }

    if (this->_psel_caches == this->_psel_max_val) {
        // this->_threshold = (this->_stride_max_val * 3) >> 2;
        // this->_threshold = (this->_stride_max_val * 1) >> 2;
        this->_threshold = std::clamp (this->_threshold * 2, (uint64_t) 8ULL, this->_stride_max_val >> 2);

        reached_limit = true;
    } else if (this->_psel_caches == 0) {
        // this->_threshold = 8;
        this->_threshold = std::clamp (this->_threshold / 2, (uint64_t) 8ULL, this->_stride_max_val >> 2);

        reached_limit = true;
    }

    if (reached_limit) {
        this->_psel_caches = (this->_psel_max_val >> 1);
    }

    // std::clog << this->_psel_caches << " " << this->_threshold << std::endl;
}

uint8_t& cc::irreg_access_pred::latency () {
    return this->_predictor_latency;
}

const uint8_t& cc::irreg_access_pred::latency () const {
    return this->_predictor_latency;
}

cc::irreg_access_pred::predictor_metrics& cc::irreg_access_pred::metrics () {
    return this->_metrics;
}

const cc::irreg_access_pred::predictor_metrics& cc::irreg_access_pred::metrics () const {
    return this->_metrics;
}

uint32_t cc::irreg_access_pred::_get_set (const uint64_t& pc) const {
    if (this->_sets_bits == 0) {
        return 0;
    }

    return (pc % this->_pred.size ());
}

std::ostream& operator<< (std::ostream& os, const cc::irreg_access_pred::predictor_metrics& pm) {
    // Printing stats.
    os << "IRREG_PRED ACCESS: " << pm.accesses
       << " HITS: " << pm.hits
       << " MISSES: " << pm.misses
       << " PREDICTION CHANGES: " << pm.prediction_changes
       << std::endl;

    return os;
}
