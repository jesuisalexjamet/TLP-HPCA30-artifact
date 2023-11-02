#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_SECTORED_CACHE_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_SECTORED_CACHE_HH__

#include <vector>
#
#include <internals/components/cache.hh>
#include <internals/components/lmp.hh>
#include <internals/components/miss_map.hh>
#
#include <instrumentations/reuse_tracker.hh>

namespace champsim {
	namespace components {
		class sectored_cache : public cache {
		private:
			using tag_type = uint64_t;
			using tag_set_array = std::vector<tag_type>;
			using tag_array = std::vector<tag_set_array>;

			using data_set_array = std::vector<BLOCK>;
			using data_array = std::vector<data_set_array>;

			using valid_bits_type = boost::dynamic_bitset<>;
			using valid_set_array = std::vector<valid_bits_type>;
			using valid_array = std::vector<valid_set_array>;

			using dirty_bits_type = boost::dynamic_bitset<>;
			using dirty_set_array = std::vector<dirty_bits_type>;
			using dirty_array = std::vector<dirty_set_array>;

			using replacement_state_set_array = std::vector<uint16_t>;
			using replacement_state_array = std::vector<replacement_state_set_array>;

			std::size_t _set_degree, _associativity_degree, _sectoring_degree, _block_size;

			std::size_t _sector_size;
			uint16_t _offset_mask;

			tag_array _tags;
			data_array _blocks;

			valid_array _valid_bits;
			dirty_array _dirty_bits;

			replacement_state_array _repl;
			replacement_state_array _repl_srrip;

			std::vector<reuse_tracker *> _trackers;
			std::string _report_filename;

			load_miss_predictor _lmp;

		public:
			sectored_cache ();
			sectored_cache (const uint64_t &cpu_idx);

			virtual ~sectored_cache ();

			virtual cache_layout layout () const override;

			virtual std::size_t sets () const override;
			virtual std::size_t associativity () const override;

			virtual void reset_stats () override;

			virtual void report (std::ostream& os, const size_t& i) override;
			const std::string& report_filename () const;

			virtual void update_footprint (const uint64_t& addr, const footprint_bitmap& footprint) override;

			// Interface with the outside world.

			virtual uint16_t get_way (const uint64_t& addr, const uint32_t& set) const override;
			virtual uint32_t get_set (const uint64_t& addr) const override;
			virtual uint64_t get_paddr (const uint32_t& set, const uint16_t& way) const override;
			uint32_t get_block (const uint64_t& addr) const;

			virtual void invalidate_line (const uint64_t& full_addr) final;

			virtual bool is_valid (const uint32_t& set, const uint16_t& way) override;

			virtual uint32_t block_size () const final;

			virtual void return_data (PACKET& packet) override;
			virtual void return_data (PACKET& packet, const boost::dynamic_bitset<>& valid_bits) override;

		private:
			virtual void _init_cache_impl (const pt::ptree& props) override;

			tag_type _get_tag (const uint64_t& addr) const;
			virtual tag_type _get_tag (const PACKET& packet) const override;

			virtual bool _should_slice_packet (const PACKET& packet) const override;
			virtual std::vector<PACKET> _slice_packet (const PACKET& packet) const override;

			virtual bool _is_hit (const PACKET& packet) const override;
			virtual bool _is_hit (const PACKET& packet, const uint32_t& set) const override;

			virtual hit_types __is_hit (const PACKET& packet) const override;
            virtual hit_types __is_hit (const PACKET& packet, const uint32_t& set) const override;

			bool _is_sector_valid (const uint32_t& set, const uint16_t& way) const;
			bool _is_sector_valid (const PACKET& packet) const;

			bool _is_sector_dirty (const uint32_t& set, const uint16_t& way) const;
			bool _is_sector_dirty (const PACKET& packet) const;

			void _fill_valid_bits (const uint32_t& set, const uint16_t& way, const bool& value);
			void _fill_valid_bits (const uint32_t& set, const uint16_t& way, const uint16_t& start_offset, const uint32_t& size, const bool& value);
			void _fill_valid_bits (const uint32_t& set, const uint16_t& way, const std::vector<bool>& valid_bits);
			void _fill_dirty_bits (const uint32_t& set, const uint16_t& way, const bool& value);
			void _fill_dirty_bits (const uint32_t& set, const uint16_t& way, const uint16_t& start_offset, const uint32_t& size, const bool& value);

			void _fill_tag_array (const uint32_t& set, const uint16_t& way, const uint64_t& tag);

			void _fill_footprint_bitmap (const uint32_t& set, const uint16_t& way, const uint16_t& start_offset, const uint32_t& size);
			void _clear_footprint_bitmap (const uint32_t& set, const uint16_t& way);

			virtual std::vector<PACKET>::iterator _add_mshr (const PACKET& packet) override;
			virtual void _fill_cache (const uint32_t& set, const uint16_t& way, const PACKET& packet) override;
			virtual void _initialize_replacement_state () override;
			virtual void _update_replacement_state (helpers::cache_access_descriptor& desc) override;
			virtual uint32_t _find_victim (helpers::cache_access_descriptor& desc) override;
			virtual void _final_replacement_stats () override;

			virtual void _update_fill_path_lower_mshrs(mshr_iterator it) override;
			virtual void _update_pf_offchip_pred_lower(mshr_iterator it) override;

			// Working methods that should not be visible from the outside. This methods are meant to handle the
			// different accesses to the cache and eventually modify internal structures when needed.
			virtual void _handle_fill () override;
			virtual void _handle_read () override;
			virtual void _handle_writeback () override;
			virtual void _handle_prefetch () override;

			bool _handle_l1d_miss_propagation (PACKET &packet, const cc::block_location& target = cc::is_in_dram);
			bool _handle_sdc_miss_propagation (PACKET &packet);
			bool _handle_miss_propagation (PACKET &packet, const cc::sdc_routes& route);
			bool _handle_miss_conservative_path (PACKET &packet, const cc::sdc_routes &route);

			void _return_data_helper (PACKET& packet);
		};
	}
}

#endif // __CHAMPSIM_INTERNALS_COMPONENTS_SECTORED_CACHE_HH__
