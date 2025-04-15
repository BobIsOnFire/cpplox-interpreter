module;

#include <yaml-cpp/yaml.h>

export module yaml_cpp;

export namespace YAML {

// Emitter API
using YAML::Emitter;
using YAML::EMITTER_MANIP;
using YAML::operator<<;

} // namespace YAML
