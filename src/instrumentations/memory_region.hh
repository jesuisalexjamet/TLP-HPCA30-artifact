#ifndef __CHAMPSIM_INSTRUMENTATIONS_MEMORY_REGION_HH__
#define __CHAMPSIM_INSTRUMENTATIONS_MEMORY_REGION_HH__

#include <cstdint>
#
#include <list>
#
#include <instrumentations/bits_entropy.hh>

namespace champsim {
	namespace instrumentations {
		class memory_region {
		private:
			uint64_t _begin;
			uint64_t _end;

			std::list<uint64_t> _addr_dir;
			bits_entropy<uint64_t> _entropy_comp;

		public:
			memory_region ();

			void record_access (const uint64_t& paddr);

			uint64_t& begin ();
			const uint64_t& begin () const;

			uint64_t& end ();
			const uint64_t& end () const;

			uint64_t mask () const;
			const std::vector<float> entropy () const;

			uint64_t size () const;
		};
	}
}

#endif // __CHAMPSIM_INSTRUMENTATIONS_MEMORY_REGION_HH__
