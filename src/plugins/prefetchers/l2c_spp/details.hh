#ifndef __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_DETAILS__HH__
#define __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_DETAILS__HH__

#include <cstdint>
#
#include <vector>
#
#include <internals/champsim.h>

namespace champsim {
	namespace prefetchers {
		namespace details {
			struct helpers {
			public:
				static uint64_t hash (const uint64_t& key) {
					uint64_t r = key;

					r += (r << 12);
					r ^= (r >> 22);
					r += (r << 4);
					r ^= (r >> 9);
					r += (r << 10);
					r ^= (r >> 2);
					r += (r << 7);
					r ^= (r >> 12);

					r = (r >> 3) * 0x9E3779B1;

					return r;
				}
			};

			struct spp_descriptor {
			public:
				std::size_t		st_sets,
								st_ways,
								st_tag_bits,
								st_tag_mask,
								sig_shift,
								sig_bits,
								sig_mask,
								sig_delta_bits;

				std::size_t		pt_sets,
								pt_ways,
								c_sig_bits,
								c_delta_bits,
								c_sig_max,
								c_delta_max;

				std::size_t		quotient_bits,
								remainder_bits,
								hash_bits,
								filter_sets,
								fill_threshold,
								pf_threshold;

				std::size_t		global_counter_bits,
								global_counter_max,
								ghr_size;
			};

			struct global_register {
			public:
				spp_descriptor desc;
				uint64_t 	pf_useful,
							pf_issued,
							global_accuracy;

				std::vector<bool> 	valid;

				std::vector<uint32_t>	sig,
										confidence,
										offset,
										delta;

			public:
				global_register () = default;

				void init (const spp_descriptor& desc) {
					this->pf_useful = 0;
					this->pf_issued = 0;
					this->global_accuracy = 0;

					// Initializing tables.
					this->valid = std::vector<bool> (desc.ghr_size, false);
					this->sig = std::vector<uint32_t> (desc.ghr_size, 0);
					this->confidence = std::vector<uint32_t> (desc.ghr_size, 0);
					this->offset = std::vector<uint32_t> (desc.ghr_size, 0);
					this->delta = std::vector<uint32_t> (desc.ghr_size, 0);

					this->desc = desc;
				}

				void update_entry (const uint32_t& pf_sig, const uint32_t& pf_confidence, const uint32_t& pf_offset, const int32_t& pf_delta);
				std::size_t check_entry (const uint32_t& page_offset);
			};

			struct signature_table {
			public:
				spp_descriptor desc;
				std::vector<std::vector<bool>> valid;
				std::vector<std::vector<uint32_t>>  tag,
													last_offset,
													sig,
													lru;

			public:
				signature_table () = default;

				void init (const spp_descriptor& desc) {
					// Initializing tables.
					this->valid = std::vector<std::vector<bool>> (desc.st_sets, std::vector<bool> (desc.st_ways, false));
					this->tag = std::vector<std::vector<uint32_t>> (desc.st_sets, std::vector<uint32_t> (desc.st_ways, 0ULL));
					this->last_offset = std::vector<std::vector<uint32_t>> (desc.st_sets, std::vector<uint32_t> (desc.st_ways, 0ULL));
					this->sig = std::vector<std::vector<uint32_t>> (desc.st_sets, std::vector<uint32_t> (desc.st_ways, 0ULL));
					this->lru = std::vector<std::vector<uint32_t>> (desc.st_sets, std::vector<uint32_t> (desc.st_ways, desc.st_ways - 1));

					this->desc = desc;
				}

				void read_and_update_sig (const uint64_t& page, const uint32_t& page_offset, uint32_t& last_sig,
										  uint32_t& curr_sig, int32_t& delta, global_register& ghr);
			};

			struct pattern_table {
			public:
				spp_descriptor desc;
				std::vector<std::vector<int32_t>> delta;
				std::vector<std::vector<uint32_t>> counter_delta;
				std::vector<uint32_t> counter_sig;

			public:
				pattern_table () = default;

				void init (const spp_descriptor& desc) {
					// Initializing tables.
					this->delta = std::vector<std::vector<int32_t>> (desc.pt_sets, std::vector<int32_t> (desc.pt_ways, 0));
					this->counter_delta = std::vector<std::vector<uint32_t>> (desc.pt_sets, std::vector<uint32_t> (desc.pt_ways, 0));
					this->counter_sig = std::vector<uint32_t> (desc.pt_sets, 0);

					this->desc = desc;
				}

				void update_pattern (const uint32_t& last_sig, const int32_t& curr_delta);
				void read_pattern (const uint32_t& curr_sig, uint32_t& lookahead_way, uint32_t& lookahead_conf,
					uint32_t& pf_q_tail, uint32_t& depth, std::vector<int32_t>& prefetch_delta, std::vector<uint32_t>& confidence_q,
					global_register& ghr);
			};

			struct prefetch_filter {
			public:
				enum filter_request {
					spp_l2c_prefetch,
					spp_llc_prefetch,
					l2c_demand,
					l2c_evict,
				};

			public:
				spp_descriptor desc;
				std::vector<uint64_t> remainder_tag;
				std::vector<bool> 	valid,
									useful;

			public:
				prefetch_filter () = default;

				void init (const spp_descriptor& desc) {
					// Initializing tables.
					this->remainder_tag = std::vector<uint64_t> (desc.filter_sets, 0ULL);
					this->valid = std::vector<bool> (desc.filter_sets, false);
					this->useful = std::vector<bool> (desc.filter_sets, false);

					this->desc = desc;
				}

				bool check (const uint64_t& check_addr, const filter_request& request, global_register& ghr);
			};
		}
	}
}

#endif // __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_DETAILS__HH__
