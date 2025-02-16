module;

#include <jinja2cpp/reflected_value.h>
#include <jinja2cpp/template.h>

export module jinja2cpp;

export namespace nonstd {

using nonstd::optional;

} // namespace nonstd

export namespace jinja2 {

using jinja2::Reflect;
using jinja2::Template;
using jinja2::Value;
using jinja2::ValuesMap;

using jinja2::GenericList;
using jinja2::GenericMap;
using jinja2::IComparable;
using jinja2::IIndexBasedAccessor;
using jinja2::IListItemAccessor;
using jinja2::IMapItemAccessor;
using jinja2::ListEnumeratorPtr;
using jinja2::ReflectedDataHolder;

namespace detail {

using detail::Enumerator;
using detail::Reflector;

} // namespace detail

} // namespace jinja2
