#ifndef __DISTILL_CACHE_LOC_DEAD_BLOCK_PREDICTOR_HH__
#define __DISTILL_CACHE_LOC_DEAD_BLOCK_PREDICTOR_HH__

#include <vector>
#include <map>
#
#include <boost/property_tree/ptree.hpp>
#
#include <internals/components/cache.hh>

namespace pt = boost::property_tree;

namespace champsim {
	namespace components {
		class distill_cache_loc_dead_block_predictor {
		public:
			struct sampler_entry {
				bool			valid,
								used;

				cache::access_types	type;

				uint8_t			lru;

				uint64_t		tag, cl_addr, pc;

				sampler_entry () :
					valid (false), used (false),
					type (cache::load), lru (0),
					tag (0ULL), cl_addr (0ULL), pc (0ULL) {

				}
			};

			// Creating type aliases to ease the use of compounds types.
			using sampler_set = std::vector<sampler_entry>;
			using sampler = std::vector<sampler_set>;

		private:
			std::size_t _sampler_sets, _sampler_ways, _cache_sets, _block_size;
			sampler _samp;
			std::map<std::size_t, std::size_t> _sampling_map;
			std::vector<uint8_t> _pred_table;

		public:
			distill_cache_loc_dead_block_predictor ();
			~distill_cache_loc_dead_block_predictor ();

			void init (const pt::ptree& props);

			bool is_cache_set_sampled (const std::size_t& set_idx) const;

			void update_sampler (const helpers::cache_access_descriptor& desc);

			const uint8_t& operator[] (const uint64_t& pc) const;

		private:
			uint64_t _get_tag (const helpers::cache_access_descriptor& desc) const;

			std::size_t _is_sampler_hit (const helpers::cache_access_descriptor& desc);
			sampler_entry& _sampler_victim (const helpers::cache_access_descriptor& desc);
		};
	}
}

#endif // __DISTILL_CACHE_LOC_DEAD_BLOCK_PREDICTOR_HH__
