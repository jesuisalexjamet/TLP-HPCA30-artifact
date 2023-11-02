#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_IRREG_ACCESS_PRED_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_IRREG_ACCESS_PRED_HH__

#include <ostream>
#
#include <list>
#include <tuple>
#
#include <internals/block.h>

namespace champsim {
	namespace components {
		class irreg_access_pred {
		private:
			struct predictor_entry {
				bool valid;
				uint32_t repl_state;
				uint64_t stride,
						 old_addr,
						 pc;

				predictor_entry () :
					valid (false), stride (0x0), old_addr (0x0), repl_state (0x0), pc (0x0) {

				}

				predictor_entry (const uint64_t& s, const uint64_t& a) :
					stride (s), old_addr (a) {

				};
			};

			using prediction_table_set = std::vector<predictor_entry>;
			using prediction_table = std::vector<prediction_table_set>;

		public:
			struct predictor_metrics {
				uint64_t accesses,
						 hits,
						 misses,
						 prediction_changes;

				predictor_metrics () :
					accesses (0x0), hits (0x0), misses (0x0),
					prediction_changes (0x0) {

				}

				void clear () {
					this->accesses = 0x0;
					this->hits = 0x0;
					this->misses = 0x0;
					this->prediction_changes = 0x0;
				}
			};

			struct l1d_path_feedback_info {
				PACKET* packet;
			};

			struct sdc_path_feedback_info {
				PACKET* packet;
			};

			enum internal_states {
				unused = 0x0,
				used = 0x1,
			};

		private:
			internal_states _state;
			bool _prev_prediction;

			uint8_t _predictor_latency;

			uint16_t _psel_caches;
			uint64_t _threshold;

			uint32_t _sets_bits;

			uint64_t _stride_max_val, _psel_max_val;

			prediction_table _pred;
			predictor_metrics _metrics;

		public:
			irreg_access_pred () = default;
			irreg_access_pred (const uint32_t& entries);
			irreg_access_pred (const uint32_t& sets, const uint32_t& ways);

			irreg_access_pred& operator= (const irreg_access_pred& o);
			irreg_access_pred& operator= (irreg_access_pred&& o);

			void set_threshold (const uint64_t& new_threshold);
			void set_stride_bits (const uint8_t& stride_bits);
			void set_psel_bits (const uint8_t& psel_bits);

			prediction_table_set::iterator find_victim (const uint32_t& set);
			void update_replacement_state (const bool& hit, const uint32_t& set, prediction_table_set::iterator it);

			void update (const uint64_t& pc, const uint64_t& vaddr);
			bool predict (const uint64_t& pc);

			void feedback_l1d_path (const l1d_path_feedback_info& info);
			void feedback_sdc_path (const sdc_path_feedback_info& info);

			uint8_t& latency();
			const uint8_t& latency() const;

			predictor_metrics& metrics ();
			const predictor_metrics& metrics () const;

		private:
			uint32_t _get_set (const uint64_t& pc) const;
		};
	}
}

std::ostream& operator<< (std::ostream& os, const champsim::components::irreg_access_pred::predictor_metrics& pm);

#endif // __CHAMPSIM_INTERNALS_COMPONENTS_IRREG_ACCESS_PRED_HH__
