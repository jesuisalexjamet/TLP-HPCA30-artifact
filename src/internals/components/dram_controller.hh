#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_DRAM_CONTROLLER_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_DRAM_CONTROLLER_HH__

#include <internals/components/memory_system.hh>

namespace champsim {
	namespace components {
		class dram_controller : public memory_system {
		private:
			PACKET_QUEUE 	*_write_queues,
							*_read_queues;

		public:
			dram_controller ();
			virtual ~dram_controller ();

			void operate ();

			// Interface with the outside world.
			virtual int32_t add_read_queue (PACKET& packet) override;
			virtual int32_t add_write_queue (PACKET& packet) override;
			virtual int32_t add_prefetch_queue (PACKET& packet) override;

			virtual void return_data (PACKET& packet) override;

			virtual uint32_t	read_queue_occupancy () override,
								read_queue_occupancy (const uint64_t& addr) override,
								write_queue_occupancy () override,
								write_queue_occupancy (const uint64_t& addr) override,
								prefetch_queue_occupancy () override,
								prefetch_queue_occupancy (const uint64_t& addr) override,
								read_queue_size () override,
								read_queue_size (const uint64_t& addr) override,
								write_queue_size () override,
								write_queue_size (const uint64_t& addr) override,
								prefetch_queue_size () override,
								prefetch_queue_size (const uint64_t& addr) override;

			uint32_t	dram_channel (const uint64_t& addr) const,
						dram_rank (const uint64_t& addr) const,
						dram_bank (const uint64_t& addr) const,
						dram_row (const uint64_t& addr) const,
						dram_column (const uint64_t& addr) const;
		};
	}
}

#endif // __CHAMPSIM_INTERNALS_COMPONENTS_DRAM_CONTROLLER_HH__
