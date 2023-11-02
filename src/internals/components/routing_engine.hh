#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_ROUTINE_ENGINE_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_ROUTINE_ENGINE_HH__

#include <cstdint>
#
#include <list>
#include <map>
#include <vector>
#include <utility>
#
#include <boost/property_tree/ptree.hpp>
#
#include <internals/block.h>

namespace pt = boost::property_tree;

namespace champsim {
	namespace components {
		class routing_engine {
		public:
			struct predictor_metrics {
				uint64_t accurate = 0,
						 inaccurate = 0;

				std::map<cc::sdc_routes, std::pair<uint64_t, uint64_t>> accurate_predictions;
				std::map<cc::sdc_routes, uint64_t> optimal_predictions;
				std::map<cc::sdc_routes, uint64_t> sniffs;
				std::map<cc::sdc_routes, std::map<cc::sdc_routes, uint64_t>> _prediction_changes;

				void clear () {
					std::tie (accurate, inaccurate) = std::make_tuple (0, 0);

					for (auto& [first, second]: sniffs) {
						second = 0;
					}

					for (auto& [first, second]: accurate_predictions) {
						second = std::make_pair (0, 0);
					}

					for (auto& [first, second]: optimal_predictions) {
						second = 0;
					}

					for (auto& [first, second]: _prediction_changes) {
						for (auto& [first_, second_]: second) {
							second_ = 0;
						}
					}
				}
			};

			struct prediction_metrics_entry {
				bool is_in_l2c,		/*!< The block we are looking for is in the L2C. */
					 is_in_llc,		/*!< The block we are looking for is in the LLC. */
					 is_in_dram;	/*!< The block we are looking for is in the DRAM (true most of the time). */
				cc::sdc_routes predicted_route,	/*!< The output of the predictor. */
							   optimal_route;	/*!< The optimal prediction. What should have been predicted. */
			};

		private:
			uint64_t _packet_cnt,
					 _sniffing_periodicity,
					 _histories_length,
					 _flush_perdiods;

			std::map<uint64_t, cc::sdc_routes> _routes_mapping;
			std::map<cc::sdc_routes, std::vector<uint64_t>> _recorded_costs,
													  _recorded_costs_init;
			std::map<cc::sdc_routes, uint64_t> _means;

			predictor_metrics _metrics;
			cc::sdc_routes _latest_prediction;

		public:
			routing_engine ();
			~routing_engine ();

			void init (const pt::ptree &props);


			bool should_sniff () const;
			void mark_sniffer (PACKET& packet) const;
			void collect_sniffer (const PACKET& packet);
			sdc_routes predict ();
			sdc_routes predict_perfect (const uint8_t& cpu, const uint64_t& addr) const;

			void inc_packet_counter ();
			void reset ();

			void check_prediction (const uint64_t& addr, const uint8_t& cpu, const cc::sdc_routes& route);
			void account_accurate (const bool& accurate, const cc::sdc_routes& route);
			void account_prediction (prediction_metrics_entry& entry);

			predictor_metrics& metrics ();
			const predictor_metrics& metrics () const;

		private:
			void _populate_metrics (const prediction_metrics_entry& entry);
			bool _check_prediction_l2c (const uint64_t& addr, const uint8_t& cpu) const;
			bool _check_prediction_llc (const uint64_t& addr) const;
		};
	}
}

#endif // __CHAMPSIM_INTERNALS_COMPONENTS_ROUTINE_ENGINE_HH__
