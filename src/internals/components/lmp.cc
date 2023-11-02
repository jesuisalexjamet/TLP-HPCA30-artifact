#include <utility>
#
#include <iostream>
#
#include <internals/components/lmp.hh>
#include <internals/components/cache.hh>

namespace cc = champsim::components;

cc::load_miss_predictor::load_miss_predictor (const std::size_t &num_pc, const std::size_t &num_history) :
 	_num_pc (num_pc), _num_history (num_history) {
    // Initializing the two-levels predictor.
    for (std::size_t i = 0; i < num_pc; i++) {
        this->_l1pt.insert (std::make_pair (i, 0x0));
    }

    for (std::size_t i = 0; i < num_history; i++) {
        this->_l2pt.insert (std::make_pair (i, false));
    }
}

cc::load_miss_predictor::load_miss_predictor (const cc::load_miss_predictor &o) {
    this->_num_pc = o._num_pc;
    this->_num_history = o._num_history;
}

cc::load_miss_predictor::load_miss_predictor (cc::load_miss_predictor &&o) {
    this->_num_pc = std::move (o._num_pc);
    this->_num_history = std::move (o._num_history);
}

cc::load_miss_predictor& cc::load_miss_predictor::operator= (const cc::load_miss_predictor &o) {
    this->_num_pc = o._num_pc;
    this->_num_history = o._num_history;
    this->_l1pt = o._l1pt;
    this->_l2pt = o._l2pt;

    return *this;
}

cc::load_miss_predictor& cc::load_miss_predictor::operator= (cc::load_miss_predictor &&o) {
    this->_num_pc = std::move (o._num_pc);
    this->_num_history = std::move (o._num_history);
    this->_l1pt = std::move (o._l1pt);
    this->_l2pt = std::move (o._l2pt);

    return *this;
}

bool cc::load_miss_predictor::predict (const uint64_t &ip) const {
    return this->_l2pt.at (this->_l1pt.at (ip % this->_num_pc));
}

void cc::load_miss_predictor::update (const uint64_t &ip, const bool &cache_miss) {
    /**
     * First, we have to update the hit/miss history associated to this instruction pointer in the first level
     * prediction table.
     */
    bool old_pred = false;
    uint32_t& hm_hist = this->_l1pt[ip % this->_num_pc];

    hm_hist = (hm_hist << 1) % this->_num_history;
    hm_hist |= static_cast<uint32_t> (cache_miss);

    // std::cout << this->_l1pt[ip % this->_num_pc] << " " << hm_hist << std::endl;

    // std::cout << hm_hist << " " << cache_miss << std::endl;

    // Second we have to update the second level of the predictor according to the cache_miss bit provided.
    old_pred = this->_l2pt[hm_hist];

    this->_l2pt[hm_hist] = cache_miss;

    // Stats...
    if (old_pred == this->_l2pt[hm_hist]) {
        this->_metrics_tracker.accurate++;
    } else {
        this->_metrics_tracker.inaccurate++;
    }
}

void cc::load_miss_predictor::update (const uint64_t &ip, const PACKET &packet) {
    /**
     * First, we have to update the hit/miss history associated to this instruction pointer in the first level
     * prediction table.
     */
    bool old_pred = false,
         cache_miss = packet.served_from == cc::is_dram;
    uint32_t& hm_hist = this->_l1pt[ip % this->_num_pc];

    hm_hist = (hm_hist << 1) % this->_num_history;
    hm_hist |= static_cast<uint32_t> (cache_miss);

    // std::cout << this->_l1pt[ip % this->_num_pc] << " " << hm_hist << std::endl;

    // std::cout << hm_hist << " " << cache_miss << std::endl;

    // Second we have to update the second level of the predictor according to the cache_miss bit provided.
    old_pred = this->_l2pt[hm_hist];

    this->_l2pt[hm_hist] = cache_miss;

    // Stats...
    // Stats...
    if (packet.type == cc::cache::load) {
        if (packet.route == cc::l1d_dram && packet.served_from == cc::is_dram) {
            this->_metrics_tracker.accurate++;
        } else if (packet.route == cc::invalid_route) {
            if (packet.bypassed_l2c_llc) {
                if (packet.served_from == cc::is_l2c || packet.served_from == cc::is_llc) {
                    this->_metrics_tracker.inaccurate++;
                } else {
                    this->_metrics_tracker.accurate++;
                }
            } else {
                if (packet.served_from == cc::is_l2c || packet.served_from == cc::is_llc) {
                    this->_metrics_tracker.accurate++;
                } else {
                    this->_metrics_tracker.inaccurate++;
                }
            }
        } else {
            this->_metrics_tracker.inaccurate++;
        }
    }
}

cc::load_miss_predictor::lmp_stats& cc::load_miss_predictor::metrics () {
    return this->_metrics_tracker;
}

const cc::load_miss_predictor::lmp_stats& cc::load_miss_predictor::metrics () const {
    return this->_metrics_tracker;
}

std::ostream& operator<< (std::ostream& os, const cc::load_miss_predictor& lmp) {
    const cc::load_miss_predictor::lmp_stats &stats = lmp.metrics ();

    os << "LMP stats : "
       << stats.accurate << " " << stats.inaccurate
       << std::endl;

    return os;
}
