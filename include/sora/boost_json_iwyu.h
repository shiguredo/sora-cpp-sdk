// Boost.JSON は <boost/json.hpp> だけを include するのを期待しているので、
// IWYU が変に include しないようにするために Boost.JSON のヘッダを
// export しておく

// IWYU pragma: begin_exports
#include <boost/json.hpp>

#include <boost/json/array.hpp>
#include <boost/json/basic_parser.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/error.hpp>
#include <boost/json/fwd.hpp>
#include <boost/json/is_deallocate_trivial.hpp>
#include <boost/json/kind.hpp>
#include <boost/json/monotonic_resource.hpp>
#include <boost/json/null_resource.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/parse_into.hpp>
#include <boost/json/parse_options.hpp>
#include <boost/json/parser.hpp>
#include <boost/json/pilfer.hpp>
#include <boost/json/result_for.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/serializer.hpp>
#include <boost/json/set_pointer_options.hpp>
#include <boost/json/static_resource.hpp>
#include <boost/json/storage_ptr.hpp>
#include <boost/json/stream_parser.hpp>
#include <boost/json/string.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_ref.hpp>
#include <boost/json/value_stack.hpp>
#include <boost/json/value_to.hpp>
#include <boost/json/visit.hpp>

// IWYU pragma: end_exports
