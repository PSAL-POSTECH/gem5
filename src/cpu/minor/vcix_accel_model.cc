#include "cpu/minor/vcix_accel_model.hh"

#include <dlfcn.h>

#include <map>

#include "base/logging.hh"

namespace gem5
{

namespace minor
{

VcixAccelModel &
VcixAccelModel::load(const std::string &path)
{
    static std::map<std::string, VcixAccelModel> loaded;

    auto found = loaded.find(path);
    if (found != loaded.end())
        return found->second;

    fatal_if(path.empty(), "A VcixAccel functional unit names no vcixModel");

    void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    fatal_if(!lib, "Cannot load VCIX accelerator model: %s", dlerror());

    auto entry = reinterpret_cast<const vcix_model *(*)()>(
        dlsym(lib, "vcix_accel_model"));
    fatal_if(!entry, "%s does not export vcix_accel_model", path);

    const vcix_model *model = entry();
    fatal_if(!model, "%s: vcix_accel_model() returned no table", path);
    fatal_if(model->abi_version != VCIX_ACCEL_ABI_VERSION,
        "%s has ABI %u, gem5 has %u", path, model->abi_version,
        VCIX_ACCEL_ABI_VERSION);
    fatal_if(!model->can_accept || !model->latency || !model->commit,
        "%s: the table leaves a timing-face function NULL", path);

    return loaded.emplace(path, VcixAccelModel(model)).first->second;
}

void
VcixAccelModel::configure(const std::vector<std::string> &keys,
    const std::vector<std::string> &values)
{
    if (configured)
        return;
    configured = true;

    fatal_if(keys.size() != values.size(),
        "vcixConfigKeys and vcixConfigValues differ in length");

    std::map<std::string, std::string> settings;
    for (size_t i = 0; i < keys.size(); i++)
        settings[keys[i]] = values[i];

    vcix_config config = {&settings,
        [](void *ctx, const char *key) -> const char * {
            auto &map =
                *static_cast<std::map<std::string, std::string> *>(ctx);
            auto found = map.find(key);
            return found == map.end() ? nullptr : found->second.c_str();
        }};
    if (model->configure)
        model->configure(model->self, &config);

    if (model->reset)
        model->reset(model->self);
}

} // namespace minor
} // namespace gem5
