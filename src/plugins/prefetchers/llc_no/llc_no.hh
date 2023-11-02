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
         * @brief An implementation of a LLC prefetcher that does nothing.
         */
		class llc_no_prefetcher : public iprefetcher {
		public:
			virtual ~llc_no_prefetcher ();

			virtual void operate (const prefetch_request_descriptor& desc) final;

			virtual llc_no_prefetcher* clone () final;

			static iprefetcher* create_prefetcher ();

		protected:
			llc_no_prefetcher ();

		private:
			llc_no_prefetcher (const llc_no_prefetcher& o);
		};
	}
}

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(
	champsim::prefetchers::llc_no_prefetcher::create_prefetcher,
	create_prefetcher
)
