#include <plugins/prefetchers/l2c_spp/details.hh>

namespace details = champsim::prefetchers::details;

std::size_t details::global_register::check_entry (const uint32_t& page_offset) {
	std::size_t max_conf_way = this->desc.ghr_size;
	uint32_t max_conf = 0;

	for (std::size_t i = 0; i < this->desc.ghr_size; i++) {
		if ((this->offset[i] == page_offset) && (max_conf < this->confidence[i])) {
			max_conf = this->confidence[i];
			max_conf_way = i;
		}
	}

	return max_conf_way;
}

void details::global_register::update_entry (const uint32_t& pf_sig, const uint32_t& pf_confidence, const uint32_t& pf_offset,
	const int32_t& pf_delta) {
	uint32_t 	min_conf = 100,
				victim_way = this->desc.ghr_size;

	for (std::size_t i = 0; i < this->desc.ghr_size; i++) {
		// If GHR already holds the same pf_offset, update the GHR entry with the latest info.
		if (this->valid[i] && (this->offset[i] == pf_offset)) {
			this->sig[i] = pf_sig;
			this->confidence[i] = pf_confidence;
			this->delta[i] = pf_delta;

			return;
		}

		/*
		 * GHR replacement policy is based on the stored confidence value
		 * The ntry with the lowest confidnece is selected for eviction.
		 */
		if (this->confidence[i] < min_conf) {
			min_conf = this->confidence[i];
			victim_way = i;
		}
	}

	// Replacing the victim entry.
	this->valid[victim_way] = 1;
	this->sig[victim_way] = pf_sig;
	this->confidence[victim_way] = pf_confidence;
	this->offset[victim_way] = pf_offset;
}

void details::signature_table::read_and_update_sig (const uint64_t& page, const uint32_t& page_offset, uint32_t& last_sig,
						  uint32_t& curr_sig, int32_t& delta, details::global_register& ghr) {
	bool st_hit = false;
	int32_t sig_delta = 0;
	std::size_t set = helpers::hash (page) % this->desc.st_sets,
				match = this->desc.st_ways,
				partial_page = page & this->desc.st_tag_mask;

	// Case 1: Hit.
	for (match = 0; match < this->desc.st_ways; match++) {
		if (this->valid[set][match] && tag[set][match] == partial_page) {
			last_sig = this->sig[set][match];
			delta = page_offset - this->last_offset[set][match];

			if (delta) {
				// Build a new signature based on 7-bit sign magnitude representation of delta.
				sig_delta = (delta < 0) ? (((-1) * delta) + (1 << (this->desc.sig_delta_bits - 1))) : delta;
				this->sig[set][match] = ((last_sig << this->desc.sig_shift) ^ sig_delta) & this->desc.sig_mask;
				this->last_offset[set][match] = page_offset;
				curr_sig = this->sig[set][match];
			} else {
				last_sig = 0; // Hitting the same cache line, delta is zero.
			}

			st_hit = true;
			break;
		}
	}

	// Case 2: Invalid
	if (match == this->desc.st_ways) {
		for (match = 0; match < this->desc.st_ways; match++) {
			if (!this->valid[set][match]) {
				this->valid[set][match] = true;
				this->valid[set][match] = partial_page;
				this->sig[set][match] = 0;
				this->last_offset[set][match] = page_offset;
				curr_sig = this->sig[set][match];

				break;
			}
		}
	}

	// Case 3: Miss.
	if (match == this->desc.st_ways) {
		for (match = 0; match < this->desc.st_ways; match++) {
			if (this->lru[set][match] == this->desc.st_ways - 1) {
				this->tag[set][match] = partial_page;
				this->sig[set][match] = 0;
				this->last_offset[set][match] = page_offset;
				curr_sig = this->sig[set][match];

				break;
			}
		}
	}

	if (!st_hit) {
		std::size_t ghr_found = ghr.check_entry (page_offset);

		if (ghr_found < this->desc.ghr_size) {
			sig_delta = (ghr.delta[ghr_found] < 0) ? (((-1) * ghr.delta[ghr_found]) + (1 << (this->desc.sig_delta_bits - 1))) : ghr.delta[ghr_found];
			this->sig[set][match] = ((ghr.sig[ghr_found] << this->desc.sig_shift) ^ sig_delta) & this->desc.sig_mask;
			curr_sig = this->sig[set][match];
		}
	}

	// Update the LRU replacement states.
	for (std::size_t way = 0; way < this->desc.st_ways; way++){
		if (this->lru[set][way] < this->lru[set][match]) {
			this->lru[set][way]++;
		}
	}

	this->lru[set][match] = 0; // Promote to the MRU position.
}

void details::pattern_table::update_pattern (const uint32_t& last_sig, const int32_t& curr_delta) {
	std::size_t set = details::helpers::hash (last_sig) % this->desc.pt_sets,
				match = 0;

	// Case 1: Hit.
	for (; match < this->desc.pt_ways; match++) {
		if (this->delta[set][match] == curr_delta) {
			this->counter_delta[set][match]++;
			this->counter_sig[set]++;

			if (this->counter_sig[set] > this->desc.c_sig_max) {
				for (auto& e: this->counter_delta[set]) {
					e >>= 1;
				}

				this->counter_sig[set] >>= 1;
			}

			break;
		}
	}

	// Case 2: Miss.
	if (match == this->desc.pt_ways) {
		uint32_t victim_way = this->desc.pt_ways,
					min_counter = this->desc.c_sig_max;

		for (match = 0; match < this->desc.pt_ways; match++) {
			if (this->counter_delta[set][match] < min_counter) {
				victim_way = match;
				min_counter = this->counter_delta[set][match];
			}
		}

		this->delta[set][victim_way] = curr_delta;
		this->counter_delta[set][victim_way] = 0;
		this->counter_sig[set]++;

		if (this->counter_sig[set] > this->desc.c_sig_max) {
			for (auto& e: this->counter_delta[set]) {
				e >>= 1;
			}

			this->counter_sig[set] >>= 1;
		}
	}
}

void details::pattern_table::read_pattern (const uint32_t& curr_sig, uint32_t& lookahead_way, uint32_t& lookahead_conf,
	uint32_t& pf_q_tail, uint32_t& depth, std::vector<int32_t>& prefetch_delta, std::vector<uint32_t>& confidence_q,
	global_register& ghr) {
	std::size_t set = details::helpers::hash (curr_sig) % this->desc.pt_sets,
				local_conf = 0,
				pf_conf = 0,
				max_conf = 0;

	if (this->counter_sig[set]) {
		for (std::size_t way = 0; way < this->desc.pt_ways; way++) {
			local_conf = (100 * this->counter_delta[set][way]) / this->counter_sig[set];
			pf_conf = depth ? (ghr.global_accuracy * this->counter_delta[set][way] / this->counter_sig[set] * lookahead_conf / 100) : local_conf;

			if (pf_conf >= this->desc.pf_threshold) {
				confidence_q[pf_q_tail] = pf_conf;
				prefetch_delta[pf_q_tail] = delta[set][way];

				// Lookahead path follows the most confident entry.
				if (pf_conf > max_conf) {
					lookahead_way = way;
					max_conf = pf_conf;
				}
				pf_q_tail++;
			}
		}

		lookahead_conf = max_conf;

		if (lookahead_conf >= this->desc.pf_threshold) {
			depth++;
		}
	} else {
		confidence_q[pf_q_tail] = 0;
	}
}

bool details::prefetch_filter::check (const uint64_t& check_addr, const details::prefetch_filter::filter_request& request,
	global_register& ghr) {
	uint64_t cache_line = check_addr >> LOG2_BLOCK_SIZE,
				hash = details::helpers::hash (cache_line),
				quotient = (hash >> this->desc.remainder_bits) & ((1 << this->desc.quotient_bits) - 1),
				remainder = hash % (1 << this->desc.remainder_bits);

	switch (request) {
		case spp_l2c_prefetch:
			if ((this->valid[quotient] || this->useful[quotient]) && this->remainder_tag[quotient] == remainder) {
				return false; // False return indicate "Do not prefetch".
			} else {
				this->valid[quotient] = 1;
				this->useful[quotient] = 0;
				this->remainder_tag[quotient] = remainder;
			}
			break;

		case spp_llc_prefetch:
			if ((this->valid[quotient] || this->useful[quotient]) && this->remainder_tag[quotient] == remainder) {
				return false;
			} else {
				// NOTE: SPP_LLC_PREFETCH has relatively low confidence (FILL_THRESHOLD <= SPP_LLC_PREFETCH < PF_THRESHOLD)
				// Therefore, it is safe to prefetch this cache line in the large LLC and save precious L2C capacity
				// If this prefetch request becomes more confident and SPP eventually issues SPP_L2C_PREFETCH,
				// we can get this cache line immediately from the LLC (not from DRAM)
				// To allow this fast prefetch from LLC, SPP does not set the valid bit for SPP_LLC_PREFETCH

				//valid[quotient] = 1;
				//useful[quotient] = 0;
			}
			break;

		case l2c_demand:
			if ((this->remainder_tag[quotient] == remainder) && (this->useful[quotient] == 0)) {
				this->useful[quotient] = 1;

				if (this->valid[quotient]) {
					ghr.pf_useful++;
				}
			}
			break;

		case l2c_evict:
			if (this->valid[quotient] && !this->useful[quotient] && ghr.pf_useful) {
				ghr.pf_useful--;
			}

			// Reset filter entry.
			this->valid[quotient] = 0;
			this->useful[quotient] = 0;
			this->remainder_tag[quotient] = 0;
			break;
	}

	return true;
}
