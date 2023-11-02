#ifndef __INSTRUMENTATIONS_CACHE_UTILIZATION_H__
#define __INSTRUMENTATIONS_CACHE_UTILIZATION_H__

#include <bitset>
#include <vector>
#include <map>
#include <tuple>
#
#include "champsim.h"

namespace champsim {
	namespace instrumentations {
		class usage_tracker {
		public:
			enum {
				offset_mask 			= ((1ULL << LOG2_BLOCK_SIZE) - 1ULL),

				word_granularity_bits 	= 0x0,
				word_granularity 		= (1ULL << word_granularity_bits),

				classes 				= (BLOCK_SIZE >> word_granularity_bits),
			};

			using usage_bitmap = std::bitset<classes>;
			using set_usage_bitmap = std::vector<usage_bitmap>;
			using cache_usage_bitmap = std::vector<set_usage_bitmap>;

		private:
			cache_usage_bitmap _usage_bitmap;
			std::map<uint8_t, uint64_t> _counters;

		public:
			usage_tracker () = default;
			usage_tracker (std::size_t sets, std::size_t ways);
			usage_tracker (const usage_tracker& o);
			usage_tracker (usage_tracker&& o);

			~usage_tracker ();

			usage_tracker& operator= (const usage_tracker& o);
			usage_tracker& operator= (usage_tracker&& o);

			usage_bitmap& bitmap (const std::tuple<std::size_t, std::size_t>& coords);
			void set (const std::tuple<std::size_t, std::size_t, std::size_t>& coords, const std::size_t &size = 1);

			const std::map<uint8_t, uint64_t>& counters () const;
			std::map<uint8_t, uint64_t>& counters ();
		};
	}
}

#endif // __INSTRUMENTATIONS_CACHE_UTILIZATION_H__
