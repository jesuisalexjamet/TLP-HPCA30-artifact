#ifndef __CHAMPSIM_INTERNALS_PREFETCHERS_IPREFETCHER_HH__
#define __CHAMPSIM_INTERNALS_PREFETCHERS_IPREFETCHER_HH__

#include <map>
#include <string>
#
#include <exception>
#
#include <internals/components/cache.hh>
#
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

static std::map<std::string, cc::cache_type> type_map = {
    {"itlb", cc::is_itlb}, {"dtlb", cc::is_dtlb}, {"stlb", cc::is_stlb},
    {"l1i", cc::is_l1i},   {"l1d", cc::is_l1d},   {"l2c", cc::is_l2c},
    {"llc", cc::is_llc},   {"sdc", cc::is_sdc},
};

namespace cc = champsim::components;
namespace pt = boost::property_tree;

namespace champsim {
namespace prefetchers {
/**
 * @brief This structure gathers information required by the prefetchers
 * to be able to throw a prefetch request to theirs associated caches.
 */
struct prefetch_request_descriptor {
    bool hit, /*!< Was this access a hit or a miss? */
         went_offchip_pred;
    cc::cache::access_types
        access_type; /*!< What kind of access led to a prefetch request? */
    uint32_t cpu,    /*!< Which CPU threw the prefetch request? */
        size;        /*!< What is the size of the element requested? */
    uint64_t addr, /*!< What is the physical address of the block from which the
                      request was thrown? */
        ip;        /*!< What is the instruction pointer that threw this prefetch
                      request? */
};

/**
 * @brief This class is an interface for prefetchers and is supposed to be
 * inherited in a module.
 */
class iprefetcher {
   public:
    iprefetcher() = default;

    /**
     * @biref Destructor of the class.
     */
    virtual ~iprefetcher() {}

    /**
     * @brief Initialization method of the class. This method fetches the
     * configuration file and performs a couple of sanity checks to ensure that
     * the pair of prefetcher and cache are compatible.
     * @param config_file Path to the configuration file of the prefetcher.
     * @param cache_inst The instance of the cache class to which the prefetcher
     * is bound.
     */
    void init(const std::string& config_file, cc::cache* cache_inst) {
        pt::ptree props;

        pt::read_json(config_file, props);

        // Calling the actual initialization.
        this->_init(props, cache_inst);
    }

    const std::string& name() const { return this->_name; }

    virtual iprefetcher* clone() { return nullptr; }

    virtual void clone(iprefetcher* o) { *this = *o; }

    /**
     * @brief This method performs the actual operation of the prefetcher. This
     * method is meant to be called on every prefetch request opportunity.
     * @param desc A descriptor filled with information regarding the prefetch
     * request.
     */
    virtual void operate(const prefetch_request_descriptor& desc) {}

    /**
     * @brief This method performs updates on the prefetcher on the event of a
     * fill in the cache.
     * @param A descriptor with information regarding the cahce block filled.
     */
    virtual void fill(const champsim::helpers::cache_access_descriptor& desc) {}

	/**
	 * @brief Reset the statistics of the prefetchers. 
	 */
	virtual void clear_stats() {}

    /**
     * @brief This method is used to provide statistics about the prefetcher.
     */
    virtual void dump_stats() {}

   protected:
    iprefetcher(const iprefetcher& o)
        : _cache_inst(o._cache_inst),
          _cache_type(o._cache_type),
          _name(o._name) {}

    iprefetcher& operator=(const iprefetcher& o) {
        this->_cache_inst = o._cache_inst;
        this->_cache_type = o._cache_type;
        this->_name = o._name;

        return *this;
    }

    /**
     * @brief The actual implementation of the initialization sequence of the
     * prefetcher. This method is meant to be overloaded by derived classes.
     * @param props The property tree contaning the configuration knobs needed
     * by the prefetcher.
     * @param cache_inst The instance of the cache class to which the prefetcher
     * is bound.
     */
    virtual void _init(const pt::ptree& props, cc::cache* cache_inst) {
        std::string cache_type;
        std::map<std::string, cc::cache_type>::iterator cache_type_it;

        // Getting meta-data about the prefetcher.
        this->_name = props.get<std::string>("name");
        cache_type = props.get<std::string>("cache_type");

        if ((cache_type_it = type_map.find(cache_type)) == type_map.end()) {
            // TODO: Throw exception.
        }

        this->_cache_type = cache_type_it->second;

        // Throwing some sanity checks.
        if (!cache_inst->check_type(this->_cache_type)) {
            throw std::runtime_error(
                "Mismatch between cache and its associated prefetcher.");
        }

        this->_cache_inst = cache_inst;
    }

   private:
    iprefetcher(iprefetcher&&) = delete;

    iprefetcher& operator=(iprefetcher&&) = delete;

   protected:
    cc::cache* _cache_inst;

   private:
    cc::cache_type _cache_type;

    std::string _name;
};
}  // namespace prefetchers
}  // namespace champsim

#endif  // __CHAMPSIM_INTERNALS_PREFETCHERS_IPREFETCHER_HH__
