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
         * @brief An implementation of a L2C prefetcher that does nothing.
         */
		class l2c_no_prefetcher : public iprefetcher {
		public:
			virtual ~l2c_no_prefetcher ();

			virtual void operate (const prefetch_request_descriptor& desc) final;

			virtual l2c_no_prefetcher* clone () final;

			static cp::l2c_no_prefetcher* create_prefetcher ();

		private:
			l2c_no_prefetcher (const l2c_no_prefetcher& o);

		protected:
			l2c_no_prefetcher ();
		};
	}
}

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(
	champsim::prefetchers::l2c_no_prefetcher::create_prefetcher,
	create_prefetcher
)
