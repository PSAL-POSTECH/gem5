#include "cpu/minor/vcix_accel_model.hh"

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

#include "base/logging.hh"

namespace gem5
{

namespace minor
{

namespace
{

/** A name as a gem5 statistic: each character outside [A-Za-z0-9_] becomes
 *  '_', the rule vcix_accel::Instance::stat_name follows */
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

const char *const kindNames[] = {"admitted", "capacity", "cycles",
    "occupancy"};

} // anonymous namespace

VcixAccelStats::UnitStats::UnitStats(statistics::Group *vcix,
    const std::string &name, const std::string &port,
    const std::string &unit_of_work) :
    statistics::Group(vcix, name.c_str()),
    admitted(this, "admitted", statistics::units::Count::get(),
        (unit_of_work + " admitted at port " + port).c_str()),
    capacity(this, "capacity",
        statistics::units::Rate<statistics::units::Count,
            statistics::units::Cycle>::get(),
        (unit_of_work + " the unit can admit per cycle").c_str()),
    cycles(this, "cycles", statistics::units::Cycle::get(),
        "cycles the model was ticked, replays after a squash included"),
    occupancy(this, "occupancy", statistics::units::Count::get(),
        (unit_of_work + " held behind the port, summed over cycles").c_str()),
    utilizedCycles(this, "utilized_cycles", statistics::units::Cycle::get(),
        "admitted / capacity, the cycles admitting at full capacity takes")
{
    utilizedCycles = admitted / capacity;
}

VcixAccelStats::CountStats::CountStats(statistics::Group *vcix,
    const std::string &name) :
    statistics::Group(vcix, name.c_str())
{
}

/** Groups the entries into units by name and the COUNT entries into one group
 *  of scalars per unit, in the order the list first names them; names are
 *  matched as statName prints them, two that meet there are fatal */
VcixAccelStats::VcixAccelStats(statistics::Group *parent,
    const vcix_model *model, void *self) :
    statistics::Group(parent, "vcix"),
    model(model),
    self(self)
{
    struct Unit
    {
        const vcix_stat *first;
        size_t at[4];
    };

    const size_t n = model->num_stats(self);
    std::vector<const vcix_stat *> entries(n);
    std::vector<Unit> found;
    std::vector<std::pair<std::string, std::vector<size_t>>> tallies;

    for (size_t i = 0; i < n; i++) {
        const vcix_stat *e = model->stat(self, i);
        fatal_if(!e || !e->unit || !e->name || !e->unit_of_work,
            "%s: statistic %d of %d is NULL or leaves a name NULL",
            model->name, i, n);
        fatal_if(e->kind > VCIX_STAT_COUNT,
            "%s: statistic %s.%s has the unknown kind %d", model->name,
            e->unit, e->name, e->kind);
        entries[i] = e;

        if (e->kind == VCIX_STAT_COUNT) {
            auto tally = std::find_if(tallies.begin(), tallies.end(),
                [e](const auto &t) { return t.first == statName(e->unit); });
            if (tally == tallies.end())
                tally = tallies.insert(tallies.end(), {statName(e->unit), {}});
            for (size_t j : tally->second) {
                fatal_if(strcmp(entries[j]->unit, e->unit),
                    "%s: counts %s and %s are both %s as statistics",
                    model->name, entries[j]->unit, e->unit, tally->first);
                fatal_if(statName(entries[j]->name) == statName(e->name),
                    "%s: counts %s::%s and %s::%s are both %s.%s as"
                    " statistics", model->name, e->unit, entries[j]->name,
                    e->unit, e->name, tally->first, statName(e->name));
            }
            tally->second.push_back(i);
            continue;
        }

        auto unit = std::find_if(found.begin(), found.end(),
            [e](const Unit &u) {
                return statName(u.first->unit) == statName(e->unit);
            });
        if (unit == found.end())
            unit = found.insert(found.end(), {e, {n, n, n, n}});
        fatal_if(strcmp(unit->first->unit, e->unit),
            "%s: units %s and %s are both %s as statistics", model->name,
            unit->first->unit, e->unit, statName(e->unit));
        fatal_if(strcmp(unit->first->name, e->name) ||
            strcmp(unit->first->unit_of_work, e->unit_of_work),
            "%s: the entries of unit %s disagree on their port or unit of"
            " work", model->name, e->unit);
        fatal_if(unit->at[e->kind] != n, "%s: unit %s has two %s entries",
            model->name, e->unit, kindNames[e->kind]);
        unit->at[e->kind] = i;
    }

    for (const auto &tally : tallies) {
        for (const Unit &unit : found) {
            fatal_if(statName(unit.first->unit) == tally.first,
                "%s: %s names both a unit and a unit of counts",
                model->name, tally.first);
        }
    }

    scalarOf.assign(n, nullptr);
    constant.assign(n, false);
    base.assign(n, 0);

    for (const Unit &unit : found) {
        for (unsigned kind = 0; kind < 4; kind++) {
            fatal_if(unit.at[kind] == n, "%s: unit %s has no %s entry",
                model->name, unit.first->unit, kindNames[kind]);
        }
        units.push_back(std::make_unique<UnitStats>(this,
            statName(unit.first->unit), unit.first->name,
            unit.first->unit_of_work));
        UnitStats &stats = *units.back();
        scalarOf[unit.at[VCIX_STAT_ADMITTED]] = &stats.admitted;
        scalarOf[unit.at[VCIX_STAT_CAPACITY]] = &stats.capacity;
        scalarOf[unit.at[VCIX_STAT_CYCLES]] = &stats.cycles;
        scalarOf[unit.at[VCIX_STAT_OCCUPANCY]] = &stats.occupancy;
        constant[unit.at[VCIX_STAT_CAPACITY]] = true;
    }

    for (const auto &[unit, at] : tallies) {
        counts.push_back(std::make_unique<CountStats>(this, unit));
        CountStats &group = *counts.back();
        for (size_t j : at) {
            const std::string desc =
                std::string(entries[j]->unit_of_work) + " counted";
            group.names.push_back(std::make_unique<statistics::Scalar>(
                &group, statName(entries[j]->name).c_str(),
                statistics::units::Count::get(), desc.c_str()));
            scalarOf[j] = group.names.back().get();
        }
    }
}

std::vector<uint64_t>
VcixAccelStats::read() const
{
    std::vector<uint64_t> values(scalarOf.size());
    model->read_stats(self, values.data());
    return values;
}

void
VcixAccelStats::resetStats()
{
    statistics::Group::resetStats();
    base = read();
}

void
VcixAccelStats::preDumpStats()
{
    statistics::Group::preDumpStats();
    const std::vector<uint64_t> values = read();
    for (size_t i = 0; i < values.size(); i++) {
        const double value = constant[i] ? double(values[i]) :
            double(values[i]) - double(base[i]);
        *scalarOf[i] = value;
    }
}

const vcix_model *
VcixAccelModel::load(const std::string &path)
{
    static std::map<std::string, const vcix_model *> loaded;

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
    fatal_if(!model->create || !model->destroy || !model->can_accept ||
        !model->issue || !model->squash || !model->commit,
        "%s: the table leaves create, destroy or a timing-face function"
        " NULL", path);

    loaded.emplace(path, model);
    return model;
}

VcixAccelModel::VcixAccelModel(const std::string &path,
    const std::vector<std::string> &keys,
    const std::vector<std::string> &values,
    statistics::Group *parent) :
    model(load(path))
{
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
        stats = std::make_unique<VcixAccelStats>(parent, model, self);
}

VcixAccelModel::~VcixAccelModel()
{
    model->destroy(self);
}

} // namespace minor
} // namespace gem5
