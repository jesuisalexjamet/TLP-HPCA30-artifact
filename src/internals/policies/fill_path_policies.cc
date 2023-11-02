#include <uncore.h>

#include <internals/simulator.hh>
#
#include <internals/policies/fill_path_policies.hh>

namespace cp = champsim::prefetchers;
namespace cpol = champsim::policies;

/* -------------------------------------------------------------------------- */
/*                         Abstract Fill Path Policy.                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Construct a new cpol::abstract fill path policy::abstract fill path
 * policy object.
 *
 * @param cpu_idx
 */
cpol::abstract_fill_path_policy::abstract_fill_path_policy(
    const std::size_t &cpu_idx)
    : _cpu_idx(cpu_idx),
      _l1i(champsim::simulator::instance()->modeled_cpu(cpu_idx)->l1i),
      _l1d(champsim::simulator::instance()->modeled_cpu(cpu_idx)->l1d),
      _l2c(champsim::simulator::instance()->modeled_cpu(cpu_idx)->l2c),
      _llc(uncore.llc),
      _dram(&uncore.DRAM) {}

/**
 * @brief Destroy the cpol::abstract fill path policy::abstract fill path policy
 * object
 *
 */
cpol::abstract_fill_path_policy::~abstract_fill_path_policy() {}

/**
 * @brief
 * @pre The cache from which this prefetch originated must be higher in the
 * cache hierarchy than c.
 *
 * @param c
 * @param packet
 * @param p_desc
 */
void cpol::abstract_fill_path_policy::_prefetch_on_higher_prefetch(
    cc::cache *c, const PACKET &packet,
    const cp::prefetch_request_descriptor &p_desc) {
    // Checking the precondition.
    if (packet.pf_origin_level >= c->fill_level()) {
        throw std::runtime_error(
            "Issuing a subsequent prefetch on a prefetch originating from a "
            "lower level cache is not allowed.");
    }

    // Operating the prefetching using the given prefetch descriptor.
    c->prefetcher()->operate(p_desc);
}

/**
 * @brief The propagation policy for a miss in the L1I is provided as a default
 * policy. The packet is propagated to the L2C. If it misses further in the
 * memory hierarchy it will follow a very classic path (L1I, L2C, LLC & DRAM).
 *
 * @param packet A packet that missed in the L1I.
 * @return true The propagation was successful.
 * @return false The propation was not successful.
 */
bool cpol::abstract_fill_path_policy::_propagate_l1i_miss(PACKET &packet) {
    bool l1i_mshr_full = this->_l1i->mshr_full(), l2c_queue_full = false;
    PACKET_QUEUE *queue = nullptr;

    // Based on the type of request, we need different queues.
    switch (packet.type) {
        case cc::cache::load:
            queue = this->_l2c->read_queue();
            break;

        case cc::cache::prefetch:
            queue = this->_l2c->prefetch_queue();
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            break;
    }

    // Is the queue full?
    l2c_queue_full = queue->is_full();

    // If one of the MSHR or the queue is full, we cannot proceed and return
    // false.
    if (l1i_mshr_full || l2c_queue_full) {
        return false;
    }

    // We now know that there is space in both the L1I MSHR and the L2C queue.
    // Let's add the packet.
    packet.push_fill_path(this->_l1i);

    this->_l1i->allocate_mshr(packet);
    // queue->add_queue(packet);

    switch (packet.type) {
        case cc::cache::load:
            // queue = this->_l2c->read_queue();
            this->_l2c->add_read_queue(packet);
            break;

        case cc::cache::prefetch:
            // queue = this->_l2c->prefetch_queue();
            this->_l2c->add_prefetch_queue(packet);
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            break;
    }

    return true;
}

/**
 * @brief
 *
 * @param c
 * @param packet
 * @return true
 * @return false
 */
bool cpol::abstract_fill_path_policy::propagate_miss(cc::cache *c,
                                                     PACKET &packet) {
    bool ret;

    /* Different actions are taken depending on which type of cache c is
     * representing. For instance it is an L1I, we will propagate to the lower
     * levels of the cache hierarchy using the propagate_l1i_miss method.
     */
    switch (c->type()) {
        case cc::is_l1i:
            ret = this->_propagate_l1i_miss(packet);
            break;

        case cc::is_l1d:
            ret = this->_propagate_l1d_miss(packet);
            break;

        case cc::is_l2c:
            ret = this->_propagate_l2c_miss(packet);
            break;

        case cc::is_llc:
            ret = this->_propagate_llc_miss(packet);
            break;

        // TODO: This is undefined. Hence, we must throw an exception here.
        default:
            assert(0);
            break;
    }

    // Sanity check.
    // assert((ret ^ packet.fill_path.empty()) ||
    //        packet.type == cc::cache::prefetch ||
    //        packet.route == cc::dram_ddrp_request);

    // Checking consistency of the fill path.
#ifndef NDEBUG
    if (packet.fill_path.size() > 1) {
        uint64_t cpu_idx = 0ULL;
        PACKET::fill_path_t fp_cpy = packet.fill_path;

        if (fp_cpy.top()->check_type(cc::is_llc)) {
            fp_cpy.pop();
        }

        cpu_idx = fp_cpy.top()->cpu();
        fp_cpy.pop();

        while (!fp_cpy.empty()) {
            if (fp_cpy.top()->cpu() != cpu_idx) {
                throw std::runtime_error("Illegal fill path.");
            }

            fp_cpy.pop();
        }
    }
#endif  // NDEBUG

    return ret;
}

/**
 * @brief
 *
 * @param c
 * @param packet
 */
void cpol::abstract_fill_path_policy::prefetch_on_higher_prefetch_on_hit(
    cc::cache *c, const PACKET &packet) {
    // Building the prefetch descriptor based on the packet.
    cp::prefetch_request_descriptor p_desc;
    p_desc.hit = true;
    p_desc.access_type = static_cast<cc::cache::access_types>(packet.type);
    p_desc.cpu = packet.cpu;
    p_desc.size = BLOCK_SIZE;
    p_desc.addr = (packet.full_addr &
                   ~(BLOCK_SIZE - 1ULL));  // Getting rid of the lower six bits.
    p_desc.ip = packet.ip;

    this->_prefetch_on_higher_prefetch(c, packet, p_desc);
}

/**
 * @brief
 *
 * @param c
 * @param packet
 */
void cpol::abstract_fill_path_policy::prefetch_on_higher_prefetch_on_miss(
    cc::cache *c, const PACKET &packet) {
    // Building the prefetch descriptor based on the packet.
    cp::prefetch_request_descriptor p_desc;
    p_desc.hit = false;
    p_desc.access_type = static_cast<cc::cache::access_types>(packet.type);
    p_desc.cpu = packet.cpu;
    p_desc.size = BLOCK_SIZE;
    p_desc.addr = (packet.full_addr &
                   ~(BLOCK_SIZE - 1ULL));  // Getting rid of the lower six bits.
    p_desc.ip = packet.ip;

    this->_prefetch_on_higher_prefetch(c, packet, p_desc);
}

cc::sdc_routes cpol::abstract_fill_path_policy::route(
    const cc::block_location &loc) {
    switch (loc) {
        case cc::is_in_l2c:
        // case cc::is_in_llc:
        case cc::is_in_both:
            return cc::sdc_l2c_dram;
            break;

        case cc::is_in_llc:
            return cc::l1d_llc;
            break;

        case cc::is_in_dram:
            return cc::dram_ddrp_request;
            // return cc::l1d_dram;
            break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */
/*                       Conservative Fill Path Policy.                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Construct a new cpol::conservative fill path policy::conservative fill
 * path policy object.
 *
 * @param cpu_idx
 */
cpol::conservative_fill_path_policy::conservative_fill_path_policy(
    const std::size_t &cpu_idx)
    : abstract_fill_path_policy(cpu_idx) {}

/**
 * @brief Destroy the cpol::conservative fill path policy::conservative fill
 * path policy object
 *
 */
cpol::conservative_fill_path_policy::~conservative_fill_path_policy() {}

/**
 * @brief
 *
 * @param packet
 * @return true
 * @return false
 */
bool cpol::conservative_fill_path_policy::_propagate_l1d_l2c_route(
    PACKET &packet) {
    bool l1d_mshr_full = this->_l1d->mshr_full(), l2c_queue_full = false;
    PACKET_QUEUE *queue = nullptr;

    // Based on the type of request, we need different queues.
    switch (packet.type) {
        case cc::cache::load:
        // On a L1D RFO miss, the miss is propagated to the read queue of the
        // target cache.
        case cc::cache::rfo:
            queue = this->_l2c->read_queue();
            break;

        case cc::cache::prefetch:
            queue = this->_l2c->prefetch_queue();
            break;

        case cc::cache::writeback:
            queue = this->_l2c->write_queue();
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            break;
    }

    // Is the queue full?
    l2c_queue_full = queue->is_full();

    // If one of the MSHR or the queue is full, we cannot proceed and return
    // false.
    if (l1d_mshr_full || l2c_queue_full) {
        return false;
    }

    // We now know that there is space in both the L1D MSHR and the L2C queue.
    // Let's add the packet.
    packet.push_fill_path(this->_l1d);

    this->_l1d->allocate_mshr(packet);
    // queue->add_queue(packet);

    switch (packet.type) {
        case cc::cache::load:
        case cc::cache::rfo:
            this->_l2c->add_read_queue(packet);
            break;

        case cc::cache::prefetch:
            this->_l2c->add_prefetch_queue(packet);
            break;

        case cc::cache::writeback:
            this->_l2c->add_write_queue(packet);
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            assert(0);
            break;
    }

    return true;
}

/**
 * @brief
 *
 * @param packet
 * @return true
 * @return false
 */
bool cpol::conservative_fill_path_policy::_propagate_l1d_llc_route(
    PACKET &packet) {
    bool l1d_mshr_full = this->_l1d->mshr_full(),
         l2c_mshr_full = this->_l2c->mshr_full(), llc_queue_full = false;
    PACKET_QUEUE *queue = nullptr;

    // Based on the type of request, we need different queues.
    switch (packet.type) {
        case cc::cache::load:
            queue = this->_llc->read_queue();
            break;

        case cc::cache::prefetch:
            queue = this->_llc->prefetch_queue();
            break;

        case cc::cache::rfo:
        case cc::cache::writeback:
            queue = this->_llc->write_queue();
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            assert(0);
            break;
    }

    // Is the queue full?
    llc_queue_full = queue->is_full();

    // If one of the MSHR or the queue is full, we cannot proceed and return
    // false.
    if (l1d_mshr_full || l2c_mshr_full || llc_queue_full) {
        return false;
    }

    // We now know that there is space in both the L1D MSHR, L2C MSHR and the
    // LLC queue. Let's add the packet.
    for (cc::cache *c : {this->_l1d, this->_l2c}) {
        // If that cache is not to be filled, we carry on to the next iteration.
        if (packet.fill_level > c->fill_level()) {
            continue;
        }

        packet.push_fill_path(c);
        c->allocate_mshr(packet);
    }
    // packet.push_fill_path(this->_l1d);
    // this->_l1d->allocate_mshr(packet);

    // packet.push_fill_path(this->_l2c);
    // this->_l2c->allocate_mshr(packet);
    // queue->add_queue(packet);

    switch (packet.type) {
        case cc::cache::load:
            // queue = this->_llc->read_queue();
            this->_llc->add_read_queue(packet);
            break;

        case cc::cache::prefetch:
            // queue = this->_llc->prefetch_queue();
            this->_l2c->add_prefetch_queue(packet);
            break;

        case cc::cache::rfo:
        case cc::cache::writeback:
            // queue = this->_llc->write_queue();
            this->_llc->add_write_queue(packet);
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            break;
    }

    return true;
}

/**
 * @brief
 *
 * @param packet
 * @return true
 * @return false
 */
bool cpol::conservative_fill_path_policy::_propagate_l1d_dram_route(
    PACKET &packet) {
    bool l1d_mshr_full = this->_l1d->mshr_full(),
         l2c_mshr_full = this->_l2c->mshr_full(),
         llc_mshr_full = this->_llc->mshr_full(), dram_queue_full = false;
    uint8_t queue_type;

    // Based on the type of request, we need different queues.
    switch (packet.type) {
        case cc::cache::load:
        case cc::cache::prefetch:
        // On a RFO miss in the L1D we throw the request to the RQ of the DRAM.
        case cc::cache::rfo:
            queue_type = 1;
            break;

        case cc::cache::writeback:
            queue_type = 2;
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            assert(0);
            break;
    }

    // Is the queue full?
    dram_queue_full = (this->_dram->get_occupancy(queue_type, packet.address) ==
                       this->_dram->get_size(queue_type, packet.address));

    // If one of the MSHR or the queue is full, we cannot proceed and return
    // false.
    if (l1d_mshr_full || l2c_mshr_full || llc_mshr_full || dram_queue_full) {
        return false;
    }

    // We now know that there is space in both the L1D MSHR, L2C MSHR and the
    // LLC queue. Let's add the packet.
    /*
     * TODO: We should adapt the fill paths to the caches individually. For
     * instance the packet placed in the L1D MSHR should not contain anything as
     * it is the end of the road. The packet placed in the L2C MSHR should
     * contain only a pointer to the L1D. And the packet placed in the LLC must
     * contain a pointer to both L1D and L2C. Finally the added to the DRAM RQ
     * must contain a pointer to the 3 caches.
     */
    for (cc::cache *c : {this->_l1d, this->_l2c, this->_llc}) {
        // If that cache is not to be filled, we carry on to the next iteration.
        if (packet.fill_level > c->fill_level()) {
            continue;
        }

        packet.push_fill_path(c);
        c->allocate_mshr(packet);
    }

    // packet.push_fill_path(this->_l1d);
    // this->_l1d->allocate_mshr(packet);

    // packet.push_fill_path(this->_l2c);
    // this->_l2c->allocate_mshr(packet);

    // packet.push_fill_path(this->_llc);
    // this->_llc->allocate_mshr(packet);

    if (queue_type == 1) {
        this->_dram->add_rq(&packet);
    } else {
        this->_dram->add_wq(&packet);
    }

    return true;
}

/**
 * @brief Propagates a packet to the lower levels of the cache hierarchy
 * assuming that the route has already been defined.
 *
 * @param packet The packet to propagate.
 * @return true The packet was successfully propagated.
 * @return false The packet could not be propagated.
 */
bool cpol::conservative_fill_path_policy::_propagate_l1d_miss(PACKET &packet) {
    bool ret = false;
    O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);

    switch (packet.route) {
        case cc::dram_ddrp_request:
            // case cc::l1d_dram:
            ret = this->_propagate_l1d_l2c_route(packet);
            if (packet.type != cc::cache::prefetch)
                curr_cpu->issue_ddrp_request(packet.lq_index, true);
            else
                curr_cpu->issue_ddrp_request_on_prefetch(packet);
            // ret = true;
            break;

        case cc::l1d_llc:
            ret = this->_propagate_l1d_llc_route(packet);
            break;

        case cc::sdc_l2c_dram:
            ret = this->_propagate_l1d_l2c_route(packet);
            break;

        // TODO: Anything else is invalid and must throw an exception.
        default:
            assert(0);
            break;
    }

    return ret;
}

/**
 * @brief
 *
 * @param packet
 * @return true
 * @return false
 */
bool cpol::conservative_fill_path_policy::_propagate_l2c_miss(PACKET &packet) {
    bool l2c_mshr_full = this->_l2c->mshr_full(), llc_queue_full = false;
    PACKET_QUEUE *queue = nullptr;
    PACKET packet_ins = packet; // We create a copy for eventual modifications.

    // Based on the type of request, we need different queues.
    switch (packet.type) {
        case cc::cache::load:
        case cc::cache::rfo:
        // case cc::cache::writeback:
            queue = this->_llc->read_queue();
            break;

        case cc::cache::prefetch:
            queue = this->_llc->prefetch_queue();
            break;

        case cc::cache::writeback:
            queue = this->_llc->write_queue();
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            assert(0);
            break;
    }

    // Is the queue full?
    llc_queue_full = queue->is_full();

    // If one of the MSHR or the queue is full, we cannot proceed and return
    // false.
    if (l2c_mshr_full || llc_queue_full) {
        return false;
    }

    // We now know that there is space in both the L1D MSHR and the L2C queue.
    // Let's add the packet.
    if (packet.fill_level <= cc::cache::fill_l2) {
        packet.push_fill_path(this->_l2c);
    }

    // Allocate MSHR only if the requested fill level is compatible.
    this->_l2c->allocate_mshr(packet);
    // queue->add_queue(packet);

    switch (packet.type) {
        case cc::cache::load:
        case cc::cache::rfo:
            // queue = this->_llc->read_queue();
            this->_llc->add_read_queue(packet);
            break;

        case cc::cache::prefetch:
            // queue = this->_llc->prefetch_queue();
            this->_llc->add_prefetch_queue(packet);
            break;

        // case cc::cache::rfo:
        case cc::cache::writeback:
            // queue = this->_llc->write_queue();
            this->_llc->add_write_queue(packet);
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            assert(0);
            break;
    }

    return true;
}

/**
 * @brief
 *
 * @param packet
 * @return true
 * @return false
 */
bool cpol::conservative_fill_path_policy::_propagate_llc_miss(PACKET &packet) {
    bool llc_mshr_full = this->_llc->mshr_full(), dram_queue_full = false;
    uint8_t queue_type;

    // Based on the type of request, we need different queues.
    switch (packet.type) {
        case cc::cache::load:
        case cc::cache::prefetch:
        case cc::cache::rfo:
            queue_type = 1;
            break;

        case cc::cache::writeback:
            queue_type = 2;
            break;

        // TODO: Other cases are undefined behaviour. Hence, we must throw an
        // exception.
        default:
            assert(0);
            break;
    }

    // Is the queue full?
    dram_queue_full = (this->_dram->get_occupancy(queue_type, packet.address) ==
                       this->_dram->get_size(queue_type, packet.address));

    // If one of the MSHR or the queue is full, we cannot proceed and return
    // false.
    if (llc_mshr_full || dram_queue_full) {
        return false;
    }

    // We now know that there is space in both the L1D MSHR and the L2C queue.
    // Let's add the packet.
    packet.push_fill_path(this->_llc);

    this->_llc->allocate_mshr(packet);

    if (queue_type == 1) {
        this->_dram->add_rq(&packet);
    } else {
        this->_dram->add_wq(&packet);
    }

    return true;
}
