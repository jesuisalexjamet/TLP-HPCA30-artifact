#include <algorithm>
#
#include <internals/ooo_cpu.h>
#include <internals/uncore.h>
#include <internals/simulator.hh>
#
#include <internals/components/routing_engine.hh>

namespace cc = champsim::components;

cc::routing_engine::routing_engine () :
	_packet_cnt (0ULL), _latest_prediction (cc::invalid_route) {

}

cc::routing_engine::~routing_engine () {

}

void cc::routing_engine::init (const pt::ptree& props) {
	uint64_t i = 0;

	this->_sniffing_periodicity = props.get<uint32_t> ("sniffing_periodicity");
	this->_histories_length = props.get<uint32_t> ("histories_length");
	this->_flush_perdiods = props.get<uint32_t> ("flush_periods");

	// Initializing the route _routes_mapping.
	for (auto& e: {cc::sdc_dram, cc::sdc_l2c_dram, cc::sdc_llc_dram}) {
		this->_routes_mapping.insert (std::make_pair (i, e));
		this->_means.insert (std::make_pair (e, 0ULL));

		this->_recorded_costs.insert (std::make_pair (e, std::vector<uint64_t> (this->_histories_length, 0x0)));

		this->_metrics.sniffs.insert (std::make_pair (e, 0));
		this->_metrics.accurate_predictions.insert (std::pair (e, std::make_pair (0, 0)));
		this->_metrics.optimal_predictions.insert (std::pair (e, 0));

		for (auto& f: {cc::sdc_dram, cc::sdc_l2c_dram, cc::sdc_llc_dram}) {
			this->_metrics._prediction_changes[e][f] = 0x0;
		}

		i++;
	}

	this->reset ();
}

bool cc::routing_engine::should_sniff () const {
	return ((this->_packet_cnt % this->_sniffing_periodicity) == 0);
}

void cc::routing_engine::mark_sniffer (PACKET& packet) const {
	cc::sdc_routes assigned_route;
	O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);

	// Marking as a sniffer.
	packet.sniffer = true;
	packet.birth_cycle = curr_cpu->current_core_cycle();

	// Selecting the route assigned to this packet.
	assigned_route = this->_routes_mapping.at((this->_packet_cnt / this->_sniffing_periodicity) % this->_routes_mapping.size ());

	packet.route = assigned_route;
}

void cc::routing_engine::collect_sniffer (const PACKET& packet) {
	cc::sdc_routes sniffed_route = packet.route;
	uint64_t latency = 0;

	latency = (packet.death_cycle - packet.birth_cycle);

	// For the route sniffed, we shift all but the first values 1 position right in the matching record.
	// std::rotate (this->_recorded_costs[sniffed_route].rbegin (), this->_recorded_costs[sniffed_route].rbegin () + 1, this->_recorded_costs[sniffed_route].rend ());
	// this->_recorded_costs[sniffed_route][0] = latency;

	// Computing mean.
	// this->_means[sniffed_route] = [] (const std::vector<uint64_t>& record) -> uint64_t {
	// 	return std::accumulate (record.cbegin (), record.cend (), 0ULL);
	// } (this->_recorded_costs[sniffed_route]) / this->_histories_length;
	this->_means[sniffed_route] += latency;

	this->_metrics.sniffs[sniffed_route]++;
}

cc::sdc_routes cc::routing_engine::predict () {
	std::map<cc::sdc_routes, uint64_t>::const_iterator route = std::min_element (this->_means.cbegin (), this->_means.cend (),
		[] (const auto& a, const auto& b) -> bool {
			return a.second < b.second;
		});

	// Just a check to be sure that we are actually populating the L2C during the warm-up phase of the simulatoin.
	// if (!all_warmup_complete && route->first != cc::sdc_l2c_dram) {
	// 	throw std::runtime_error ("Over the warmup phase, the predictor should always return cc::sdc_l2c_dram.");
	// }

	if (this->_latest_prediction != cc::invalid_route && this->_latest_prediction != route->first) {
		this->_metrics._prediction_changes[this->_latest_prediction][route->first]++;
	}

	this->_latest_prediction = route->first;

	return route->first;
}

cc::sdc_routes cc::routing_engine::predict_perfect (const uint8_t& cpu, const uint64_t& addr) const {
	// This is an attempt to build a perfect predictor.
	bool in_l2c = this->_check_prediction_l2c (addr, cpu),
		 in_llc = this->_check_prediction_llc (addr),
		 in_dram = true;

	if (in_l2c) {
		return cc::sdc_l2c_dram;
	} else if (!in_l2c && in_llc) {
		return cc::sdc_llc_dram;
	} else {
		return cc::sdc_dram;
	}
}

void cc::routing_engine::inc_packet_counter () {
	this->_packet_cnt++;

	if ((this->_packet_cnt % (this->_flush_perdiods * this->_sniffing_periodicity)) == 0) {
		this->reset ();
	}
}

void cc::routing_engine::reset () {
	cc::sdc_routes min_route;
	uint64_t shift_required = 0;

	// On initialization we don't do anything.
	if (this->_packet_cnt == 0) {
		for (const auto& [first, second]: this->_recorded_costs) {
			switch (first) {
			case cc::sdc_dram:
				// this->_means[first] = UINT32_MAX;
				this->_means[first] = 0;
				// std::fill (this->_recorded_costs[first].begin (), this->_recorded_costs[first].end (), UINT32_MAX / this->_histories_length);
				std::fill (this->_recorded_costs[first].begin (), this->_recorded_costs[first].end (), 0);
				break;
			case cc::sdc_llc_dram:
				// this->_means[first] = UINT32_MAX / 2;
				this->_means[first] = 0;
				// std::fill (this->_recorded_costs[first].begin (), this->_recorded_costs[first].end (), (UINT32_MAX / this->_histories_length) / 2);
				std::fill (this->_recorded_costs[first].begin (), this->_recorded_costs[first].end (), 0);
				break;
			case cc::sdc_l2c_dram:
				this->_means[first] = 0;
				std::fill (this->_recorded_costs[first].begin (), this->_recorded_costs[first].end (), 0);
				break;
			}
		}

		return;
	}

	// for (const auto& [first, second]: this->_means) {
	// 	std::cout << first << " " << second << std::endl;
	// }

	// We start by getting the predicted route (supposed to be the fastest).
	min_route = this->predict ();

	// We shift all values until a 1 appears in that shift register.
	// while (std::find_if (this->_recorded_costs[min_route].cbegin (), this->_recorded_costs[min_route].cend (),
	// 	   [] (const uint64_t& e) -> bool { return (e <= 1); }) == this->_recorded_costs[min_route].cend ()) {
	// 	std::for_each (this->_recorded_costs[min_route].begin (), this->_recorded_costs[min_route].end (),
	// 		[] (uint64_t& e) -> void {
	// 			e >>= 1;
	// 		});
	//
	// 	shift_required++;
	// }

	while (this->_means[min_route] > 1) {
		this->_means[min_route] >>= 1;

		shift_required++;
	}

	// Shifting values as required.
	for (auto& [first, second]: this->_means) {
		if (first == min_route) {
			continue;
		}

		second >>= shift_required;

		// for (uint64_t& e: second) {
		// 	e >>= shift_required;
		// }
	}

	// for (auto& [first, second]: this->_recorded_costs) {
	// 	this->_means[first] = [] (const std::vector<uint64_t>& record) -> uint64_t {
	// 		return std::accumulate (record.cbegin (), record.cend (), 0ULL);
	// 	} (second) / this->_histories_length;
	// }

	// for (const auto& [first, second]: this->_means) {
	// 	std::cout << first << " " << second << std::endl;
	// }
	//
	// std::cout << std::endl;
}

/**
 * @brief This methods intends to check if a prediction of this predictor for a given cache block was accurate or not.
 * In order to know if it was so, for a prediction to access a given storage, we check if any faster storage could have
 * served that access.
 * @param addr The full address of the byte that we want to access.
 * @param cpu
 * @param route The prediction associated with that access.
 */
void cc::routing_engine::check_prediction (const uint64_t& addr, const uint8_t& cpu, const cc::sdc_routes& route) {
	bool in_l2c = this->_check_prediction_l2c (addr, cpu),
		 in_llc = this->_check_prediction_llc (addr),
		 in_dram = true;
	prediction_metrics_entry entry;

	entry = {
		.is_in_l2c = in_l2c,
		.is_in_llc = in_llc,
		.is_in_dram = in_dram,
		.predicted_route = route,
	};

	// Accounting the prediction. There the computation of which path was optmial will be made.
	this->account_prediction (entry);
}

void cc::routing_engine::_populate_metrics (const prediction_metrics_entry& entry) {
	if (entry.predicted_route == entry.optimal_route) {
		this->_metrics.accurate++;
		this->_metrics.accurate_predictions[entry.predicted_route].first++;
	} else {
		this->_metrics.inaccurate++;
		this->_metrics.accurate_predictions[entry.predicted_route].second++;
	}

	this->_metrics.optimal_predictions[entry.optimal_route]++;
}

bool cc::routing_engine::_check_prediction_l2c (const uint64_t& addr, const uint8_t& cpu) const {
	uint32_t set = champsim::simulator::instance ()->modeled_cpu (cpu)->l2c->get_set (addr);
	uint16_t way = champsim::simulator::instance ()->modeled_cpu (cpu)->l2c->get_way (addr, set);

	return (way != champsim::simulator::instance ()->modeled_cpu (cpu)->l2c->associativity ());
}

bool cc::routing_engine::_check_prediction_llc (const uint64_t& addr) const {
	uint32_t set = uncore.llc->get_set (addr);
	uint16_t way = uncore.llc->get_way (addr, set);

	return (way != uncore.llc->associativity ());
}

void cc::routing_engine::account_accurate (const bool& accurate, const cc::sdc_routes& route) {
	if (accurate) {
		this->_metrics.accurate++;
		this->_metrics.accurate_predictions[route].first++;
	} else {
		this->_metrics.inaccurate++;
		this->_metrics.accurate_predictions[route].second++;
	}
}

/**
 *
 */
void cc::routing_engine::account_prediction (cc::routing_engine::prediction_metrics_entry& entry) {
	// We start by computing the ideal storage target for this access.
	if (entry.is_in_l2c) {
		entry.optimal_route = cc::sdc_l2c_dram;
	} else if (entry.is_in_llc && !entry.is_in_l2c) {
		entry.optimal_route = cc::sdc_llc_dram;
	} else {
		entry.optimal_route = cc::sdc_dram;
	}

	// Now that we have the optimal route, we can enqueue this entry for later.
	this->_populate_metrics (entry);
}

cc::routing_engine::predictor_metrics& cc::routing_engine::metrics () {
	return this->_metrics;
}

const cc::routing_engine::predictor_metrics& cc::routing_engine::metrics () const {
	return this->_metrics;
}
