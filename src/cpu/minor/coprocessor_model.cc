#include "cpu/minor/coprocessor_model.hh"

#include <dlfcn.h>

#include <cstring>
#include <map>
#include <string>

#include "base/logging.hh"

namespace gem5
{

namespace minor
{

namespace
{

/** A name as a gem5 statistic: each character outside [A-Za-z0-9_] becomes
 *  '_', the rule cp::Instance::stat_name follows */
std::string
statName(const char *name)
{
    std::string out(name);
    for (char &c : out) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_'))
            c = '_';
    }
    return out;
}

} // anonymous namespace

CoprocessorStats::CounterGroup::CounterGroup(statistics::Group *vcix,
    const std::string &name) :
    statistics::Group(vcix, name.c_str())
{
}

/** One group per entry group, in the order the list first names them; names
 *  are matched as statName prints them, two that meet there are fatal */
CoprocessorStats::CoprocessorStats(statistics::Group *parent,
    const vcix_model *model, void *self) :
    statistics::Group(parent, "vcix"),
    model(model),
    self(self)
{
    const size_t n = model->num_stats(self);
    std::vector<const vcix_stat *> entries(n);
    std::vector<std::vector<size_t>> members;
    scalarOf.assign(n, nullptr);
    base.assign(n, 0);

    for (size_t i = 0; i < n; i++) {
        const vcix_stat *e = model->stat(self, i);
        fatal_if(!e || !e->group || !e->name,
            "%s: statistic %d of %d is NULL or leaves a name NULL",
            model->name, i, n);
        entries[i] = e;

        size_t g = 0;
        while (g < groups.size() &&
                statName(entries[members[g][0]]->group) != statName(e->group))
            g++;
        if (g == groups.size()) {
            groups.push_back(
                std::make_unique<CounterGroup>(this, statName(e->group)));
            members.emplace_back();
        }
        for (size_t j : members[g]) {
            fatal_if(strcmp(entries[j]->group, e->group),
                "%s: groups %s and %s are both %s as statistics",
                model->name, entries[j]->group, e->group, statName(e->group));
            fatal_if(statName(entries[j]->name) == statName(e->name),
                "%s: counters %s.%s and %s.%s are both %s.%s as statistics",
                model->name, e->group, entries[j]->name, e->group, e->name,
                statName(e->group), statName(e->name));
        }
        members[g].push_back(i);

        const std::string desc = e->capacity == 0 ? std::string(e->name) :
            std::string(e->name) + ", at most " +
            std::to_string(e->capacity) + " a cycle";
        CounterGroup &group = *groups[g];
        group.counters.push_back(std::make_unique<statistics::Scalar>(
            &group, statName(e->name).c_str(),
            statistics::units::Count::get(), desc.c_str()));
        scalarOf[i] = group.counters.back().get();
    }
}

std::vector<uint64_t>
CoprocessorStats::read() const
{
    std::vector<uint64_t> values(scalarOf.size());
    model->read_stats(self, values.data());
    return values;
}

void
CoprocessorStats::resetStats()
{
    statistics::Group::resetStats();
    base = read();
}

void
CoprocessorStats::preDumpStats()
{
    statistics::Group::preDumpStats();
    const std::vector<uint64_t> values = read();
    for (size_t i = 0; i < values.size(); i++)
        *scalarOf[i] = double(values[i]) - double(base[i]);
}

const vcix_model *
CoprocessorModel::load(const std::string &path)
{
    static std::map<std::string, const vcix_model *> loaded;

    auto found = loaded.find(path);
    if (found != loaded.end())
        return found->second;

    fatal_if(path.empty(), "A Coprocessor functional unit names no coprocessorModel");

    void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    fatal_if(!lib, "Cannot load VCIX accelerator model: %s", dlerror());

    auto entry = reinterpret_cast<const vcix_model *(*)()>(
        dlsym(lib, "coprocessor_model"));
    fatal_if(!entry, "%s does not export coprocessor_model", path);

    const vcix_model *model = entry();
    fatal_if(!model, "%s: coprocessor_model() returned no table", path);
    fatal_if(model->abi_version != COPROCESSOR_ABI_VERSION,
        "%s has ABI %u, gem5 has %u", path, model->abi_version,
        COPROCESSOR_ABI_VERSION);
    fatal_if(!model->create || !model->destroy || !model->can_accept ||
        !model->issue || !model->squash || !model->commit,
        "%s: the table leaves create, destroy or a timing-face function"
        " NULL", path);

    loaded.emplace(path, model);
    return model;
}

CoprocessorModel::CoprocessorModel(const std::string &path,
    const std::vector<std::string> &keys,
    const std::vector<std::string> &values,
    statistics::Group *parent) :
    model(load(path))
{
    fatal_if(keys.size() != values.size(),
        "coprocessorConfigKeys and coprocessorConfigValues differ in length");

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
    char error[256] = "";

    self = model->create(&config, error, sizeof(error));
    if (!self)
        fatal("%s: %s: %s", path, model->name, error);

    if (!model->num_stats)
        return;
    fatal_if(!model->stat || !model->read_stats,
        "%s: the table has num_stats but leaves stat or read_stats NULL",
        path);
    if (model->num_stats(self) > 0)
        stats = std::make_unique<CoprocessorStats>(parent, model, self);
}

CoprocessorModel::~CoprocessorModel()
{
    model->destroy(self);
}

} // namespace minor
} // namespace gem5
