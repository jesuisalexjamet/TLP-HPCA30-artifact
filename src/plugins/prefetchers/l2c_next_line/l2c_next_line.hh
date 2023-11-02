#include <internals/prefetchers/iprefetcher.hh>
#
#include <boost/shared_ptr.hpp>
#
#include <boost/property_tree/ptree.hpp>
#
#include <boost/dll.hpp>
#include <boost/dll/alias.hpp>

namespace dll = boost::dll;
namespace pt = boost::property_tree;

namespace champsim {
	namespace prefetchers {
        /**
         * @brief An implementation of a next line L2C prefetcher.
         */
		class l2c_next_line_prefetcher : public iprefetcher {
		public:
			virtual ~l2c_next_line_prefetcher ();

			virtual void operate (const prefetch_request_descriptor& desc) final;

			virtual l2c_next_line_prefetcher* clone () final;

			static iprefetcher* create_prefetcher ();

		private:
			l2c_next_line_prefetcher (const l2c_next_line_prefetcher& o);

			virtual void _init (const pt::ptree& props, cc::cache* cache_inst) final;

		protected:
			l2c_next_line_prefetcher ();

		private:
			uint64_t _degree;
		};
	}
}

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(
	champsim::prefetchers::l2c_next_line_prefetcher::create_prefetcher,
	create_prefetcher
)
