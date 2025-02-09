module;

#include <jinja2cpp/reflected_value.h>
#include <yaml-cpp/yaml.h>

export module jinja2_yaml_binding;

export namespace jinja2 {

using jinja2::Reflect;

}

namespace jinja2::detail {

class YamlObjectAccessor
    : public IMapItemAccessor
    , public ReflectedDataHolder<YAML::Node>
{
public:
    using ReflectedDataHolder<YAML::Node>::ReflectedDataHolder;

    auto GetSize() const -> size_t override
    {
        const auto * j = this->GetValue();
        return (j != nullptr) ? j->size() : 0ULL;
    }

    auto HasValue(const std::string & name) const -> bool override
    {
        const auto * j = this->GetValue();
        return (j != nullptr) ? (*j)[name].IsDefined() : false;
    }

    auto GetValueByName(const std::string & name) const -> Value override
    {
        const auto * j = this->GetValue();
        if (j == nullptr) {
            return {};
        }
        return Reflect(static_cast<YAML::Node>((*j)[name]));
    }

    auto GetKeys() const -> std::vector<std::string> override
    {
        const auto * j = this->GetValue();
        if (j == nullptr) {
            return {};
        }

        std::vector<std::string> result;
        result.reserve(j->size());
        for (const auto & item : *j) {
            result.emplace_back(item.first.as<std::string>());
        }
        return result;
    }

    auto IsEqual(const IComparable & other) const -> bool override
    {
        const auto * val = dynamic_cast<const YamlObjectAccessor *>(&other);
        if (val == nullptr) {
            return false;
        }
        return GetValue() == val->GetValue();
    }
};

struct YamlArrayAccessor
    : IListItemAccessor
    , IIndexBasedAccessor
    , ReflectedDataHolder<YAML::Node>
{
    using ReflectedDataHolder<YAML::Node>::ReflectedDataHolder;

    auto GetSize() const -> nonstd::optional<size_t> override
    {
        const auto * j = this->GetValue();
        return (j != nullptr) ? j->size() : nonstd::optional<size_t>();
    }

    auto GetIndexer() const -> const IIndexBasedAccessor * override { return this; }

    auto CreateEnumerator() const -> ListEnumeratorPtr override
    {
        using Enum = Enumerator<typename YAML::Node::const_iterator>;
        const auto * j = this->GetValue();
        if (j == nullptr) {
            return {};
        }

        return jinja2::ListEnumeratorPtr(new Enum(j->begin(), j->end()));
    }

    auto GetItemByIndex(int64_t idx) const -> Value override
    {
        const auto * j = this->GetValue();
        if (j == nullptr) {
            return {};
        }

        return Reflect(static_cast<YAML::Node>((*j)[idx]));
    }

    auto IsEqual(const IComparable & other) const -> bool override
    {
        const auto * val = dynamic_cast<const YamlArrayAccessor *>(&other);
        if (val == nullptr) {
            return false;
        }
        return GetValue() == val->GetValue();
    }
};

template <> struct Reflector<YAML::Node>
{
    static auto Create(YAML::Node val) -> Value
    {
        Value result;
        switch (val.Type()) {
        case YAML::NodeType::Undefined:
        case YAML::NodeType::Null: break;
        case YAML::NodeType::Map:
            result = GenericMap([accessor = YamlObjectAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Sequence:
            result = GenericList([accessor = YamlArrayAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Scalar: result = val.Scalar(); break;
        }
        return result;
    }

    static auto CreateFromPtr(const YAML::Node * val) -> Value
    {
        Value result;
        switch (val->Type()) {
        case YAML::NodeType::Undefined:
        case YAML::NodeType::Null: break;
        case YAML::NodeType::Map:
            result = GenericMap([accessor = YamlObjectAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Sequence:
            result = GenericList([accessor = YamlArrayAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Scalar: result = val->Scalar(); break;
        }
        return result;
    }
};

template <> struct Reflector<const YAML::detail::iterator_value>
{
    static auto Create(YAML::detail::iterator_value val) -> Value
    {
        Value result;
        switch (val.Type()) {
        case YAML::NodeType::Undefined:
        case YAML::NodeType::Null: break;
        case YAML::NodeType::Map:
            result = GenericMap([accessor = YamlObjectAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Sequence:
            result = GenericList([accessor = YamlArrayAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Scalar: result = val.Scalar(); break;
        }
        return result;
    }

    static auto CreateFromPtr(const YAML::Node * val) -> Value
    {
        Value result;
        switch (val->Type()) {
        case YAML::NodeType::Undefined:
        case YAML::NodeType::Null: break;
        case YAML::NodeType::Map:
            result = GenericMap([accessor = YamlObjectAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Sequence:
            result = GenericList([accessor = YamlArrayAccessor(val)]() { return &accessor; });
            break;
        case YAML::NodeType::Scalar: result = val->Scalar(); break;
        }
        return result;
    }
};

} // namespace jinja2::detail
