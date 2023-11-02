#ifndef __CHAMPSIM_REPLACEMENTS_LLC_TOPT_HH__
#define __CHAMPSIM_REPLACEMENTS_LLC_TOPT_HH__

#include <internals/replacements/ireplacementpolicy.hh>
#
#include <cstdint>
#include <cstdio>
#include <vector>
#include <random>
#
#include <boost/property_tree/ptree.hpp>
#
#include <boost/dll.hpp>
#include <boost/dll/alias.hpp>

namespace dll = boost::dll;
namespace pt = boost::property_tree;

namespace champsim {
	namespace replacements {
	/**
     * @brief An implementation of a L1D prefetcher that does nothing.
     */
    	class llc_topt_replacement_policy : public ireplacementpolicy {
		public:
			using reuse_distance_pair = std::pair<uint64_t, uint64_t>;
			using reuse_distance_vector = std::vector<reuse_distance_pair>;

		public:
			llc_topt_replacement_policy ();
			virtual ~llc_topt_replacement_policy ();

			virtual void update_replacement_state (const ch::cache_access_descriptor& desc) final;

			virtual std::size_t find_victim (const ch::cache_access_descriptor& desc) final;

			static ireplacementpolicy* create_replacementpolicy ();

		private:
			uint64_t _get_vertex_id (const ch::cache_access_descriptor& desc, const uint64_t& vaddr);
			void _get_reuse_distance (const ch::cache_access_descriptor& desc, reuse_distance_pair& rdv);
			uint64_t _get_vertex_reuse_distance (const uint64_t& vertex_id, const uint8_t& cpu);
			uint64_t _inverse_address_translation (const ch::cache_access_descriptor& desc, const uint64_t& paddr);
			void _browse_trace (const uint32_t& curr_vertex, reuse_distance_vector& rdv, const ch::cache_access_descriptor& desc);

		protected:
			void _init (const pt::ptree& props, cc::cache* cache_inst) final;

		private:
			std::string _graph_filename;
			std::vector<std::vector<uint32_t>> _vertices_trace;
			std::vector<std::vector<uint32_t>::iterator> _trace_it;

			// Random number generator for the back replacement policy.
			std::mt19937 _gen;
			std::uniform_int_distribution<> _dist, _dist_back_up;
		};

		// Exporting the symbol used for module loading.
		BOOST_DLL_ALIAS(
			champsim::replacements::llc_topt_replacement_policy::create_replacementpolicy,
			create_replacementpolicy
		)
	}
}

#endif // __CHAMPSIM_REPLACEMENTS_LLC_TOPT_HH__
