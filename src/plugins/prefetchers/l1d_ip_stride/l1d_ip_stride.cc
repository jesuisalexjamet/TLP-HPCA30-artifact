#include <plugins/prefetchers/l1d_ip_stride/l1d_ip_stride.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l1d_ip_stride_prefetcher::l1d_ip_stride_prefetcher() {}

cp::l1d_ip_stride_prefetcher::l1d_ip_stride_prefetcher(
    const cp::l1d_ip_stride_prefetcher& o)
    : iprefetcher(o) {}

/**
 * Destructor of the class.
 */
cp::l1d_ip_stride_prefetcher::~l1d_ip_stride_prefetcher() {}

/**
 * @brief This method performs the actual operation of the prefetcher. This
 * method is meant to be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch
 * request.
 */
void cp::l1d_ip_stride_prefetcher::operate(
    const cp::prefetch_request_descriptor& desc) {
    uint64_t cl_addr = desc.addr >> LOG2_BLOCK_SIZE;
    int64_t stride = 0LL;

    // Check for a tracker hit.
    auto it_match = this->_lookup_trackers(desc.ip);

    // If the IP doesn't have a tracker yet, we allocate one.
    if (it_match == this->_trackers.end()) {
        auto it_victim = this->_find_victim();

        it_victim->ip = desc.ip;
        it_victim->last_cl_addr = cl_addr;
        it_victim->last_stride = 0;

        // Updating the replacement states.
        this->_update_replacement_state(it_victim);

        return;
    }

    // At this point, we know an IP tracker exists for the incoming IP.
    if (cl_addr > it_match->last_cl_addr) {
        stride = cl_addr - it_match->last_cl_addr;
    } else {
        stride = it_match->last_cl_addr - cl_addr;
        stride *= -1;
    }

    // If the stride is equal to zero, we don't do anything.
    if (stride == 0) return;

    // We only issue prefetches if there is a pattern of seeing the same dtride
    // more than once.
    if (stride == it_match->last_stride) {
        for (uint32_t i = 0; i < this->_prefetch_degree; i++) {
            uint64_t pf_address = (cl_addr + (stride * (i + 1)))
                                  << LOG2_BLOCK_SIZE;

            // Only issue prefetch if the prefetch address is in the same page
            // as the current demand access address.
			if ((pf_address >> LOG2_PAGE_SIZE) != (desc.addr >> LOG2_PAGE_SIZE))
				break;

			this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip, desc.addr, pf_address, cc::cache::fill_l1, 0);
        }
    }

    // Updates.
    it_match->last_cl_addr = cl_addr;
    it_match->last_stride = stride;

    this->_update_replacement_state(it_match);
}

cp::l1d_ip_stride_prefetcher* cp::l1d_ip_stride_prefetcher::clone() {
    return new l1d_ip_stride_prefetcher(*this);
}

/**
 * This method is used to create an instance of the prefetcher and provide it to
 * the performance model.
 */
cp::iprefetcher* cp::l1d_ip_stride_prefetcher::create_prefetcher() {
    return new cp::l1d_ip_stride_prefetcher();
}

void cp::l1d_ip_stride_prefetcher::_init(const pt::ptree& props,
                                         cc::cache* cache_inst) {
    // Calling the version of the parent class first.
    cp::iprefetcher::_init(props, cache_inst);

    this->_prefetch_degree = props.get<uint64_t>("prefetch_degree");
    this->_ip_tracker_size = props.get<uint64_t>("ip_tracker_size");

    // Now that have got the knobs from the configuration file, let's create the
    // data structures.
    this->_trackers =
        std::vector<ip_tracker>(this->_ip_tracker_size, ip_tracker());

    for (std::size_t i = 0; i < this->_ip_tracker_size; i++) {
        this->_trackers[i].lru = i;
    }
}

cp::l1d_ip_stride_prefetcher::tracker_array::iterator
cp::l1d_ip_stride_prefetcher::_find_victim() {
    tracker_array::iterator it_victim =
        std::find_if(this->_trackers.begin(), this->_trackers.end(),
                     [size = this->_ip_tracker_size](const auto& e) -> bool {
                         return (e.lru == (size - 1));
                     });

    // Sanity check.
    assert(it_victim != this->_trackers.end());

    return it_victim;
}

cp::l1d_ip_stride_prefetcher::tracker_array::iterator
cp::l1d_ip_stride_prefetcher::_lookup_trackers(const uint64_t& ip) {
    return std::find(this->_trackers.begin(), this->_trackers.end(), ip);
}

void cp::l1d_ip_stride_prefetcher::_update_replacement_state(
    tracker_array::iterator it) {
    std::for_each(this->_trackers.begin(), this->_trackers.end(),
                  [it](auto& e) -> void {
                      if (e.lru >= it->lru) return;

                      e.lru++;
                  });

    it->lru = 0;
}
