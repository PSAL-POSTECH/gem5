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

VcixAccelStats::PortStats::PortStats(statistics::Group *unit,
    const std::string &name, const std::string &unit_of_work,
    bool is_primary) :
    statistics::Group(unit, name.c_str()),
    isPrimary(is_primary),
    admitted(this, "admitted", statistics::units::Count::get(),
        (unit_of_work + " the port admitted").c_str()),
    capacity(this, "capacity",
        statistics::units::Rate<statistics::units::Count,
            statistics::units::Cycle>::get(),
        (unit_of_work + " the port can admit per cycle").c_str()),
    cycles(this, "cycles", statistics::units::Cycle::get(),
        "cycles the model was ticked, replays after a squash included"),
    occupancy(this, "occupancy", statistics::units::Count::get(),
        (unit_of_work + " held behind the port, summed over cycles").c_str()),
    primary(this, "primary", statistics::units::Count::get(),
        "1 if the unit's utilized cycles are this port's, else 0"),
    utilizedCycles(this, "utilized_cycles", statistics::units::Cycle::get(),
        "admitted / capacity, the cycles admitting at full capacity takes")
{
    utilizedCycles = admitted / capacity;
}

VcixAccelStats::UnitStats::UnitStats(statistics::Group *vcix,
    const std::string &name) :
    statistics::Group(vcix, name.c_str()),
    name(name),
    utilizedCycles(this, "utilized_cycles", statistics::units::Cycle::get(),
        "the utilized cycles of the unit's primary port")
{
}

VcixAccelStats::CountStats::CountStats(statistics::Group *vcix,
    const std::string &name) :
    statistics::Group(vcix, name.c_str())
{
}

/** Groups the entries into ports by (unit, name) and the COUNT entries into
 *  one group of scalars per unit, in the order the list first names them;
 *  names are matched as statName prints them, two that meet there are fatal */
VcixAccelStats::VcixAccelStats(statistics::Group *parent,
    const vcix_model *model, void *self) :
    statistics::Group(parent, "vcix"),
    model(model),
    self(self)
{
    struct Port
    {
        const vcix_stat *first;
        size_t at[4];
    };

    const size_t n = model->num_stats(self);
    std::vector<const vcix_stat *> entries(n);
    std::vector<Port> ports;
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

        auto port = std::find_if(ports.begin(), ports.end(),
            [e](const Port &p) {
                return statName(p.first->unit) == statName(e->unit) &&
                    statName(p.first->name) == statName(e->name);
            });
        if (port == ports.end())
            port = ports.insert(ports.end(), {e, {n, n, n, n}});
        fatal_if(strcmp(port->first->unit, e->unit) ||
            strcmp(port->first->name, e->name),
            "%s: ports %s.%s and %s.%s are both %s.%s as statistics",
            model->name, port->first->unit, port->first->name, e->unit,
            e->name, statName(e->unit), statName(e->name));
        fatal_if(port->at[e->kind] != n,
            "%s: port %s.%s has two %s entries", model->name, e->unit,
            e->name, kindNames[e->kind]);
        fatal_if(e->primary != port->first->primary,
            "%s: the entries of port %s.%s disagree on primary",
            model->name, e->unit, e->name);
        port->at[e->kind] = i;
    }

    for (const auto &tally : tallies) {
        for (const Port &port : ports) {
            fatal_if(statName(port.first->unit) == tally.first,
                "%s: %s names both a unit of ports and a unit of counts",
                model->name, tally.first);
        }
    }

    scalarOf.assign(n, nullptr);
    constant.assign(n, false);
    base.assign(n, 0);

    for (const Port &port : ports) {
        for (unsigned kind = 0; kind < 4; kind++) {
            fatal_if(port.at[kind] == n, "%s: port %s.%s has no %s entry",
                model->name, port.first->unit, port.first->name,
                kindNames[kind]);
        }

        const std::string unit_name = statName(port.first->unit);
        auto unit = std::find_if(units.begin(), units.end(),
            [&unit_name](const std::unique_ptr<UnitStats> &u) {
                return u->name == unit_name;
            });
        if (unit == units.end()) {
            unsigned primaries = 0;
            for (const Port &other : ports) {
                if (statName(other.first->unit) != unit_name)
                    continue;
                fatal_if(strcmp(other.first->unit, port.first->unit),
                    "%s: units %s and %s are both %s as statistics",
                    model->name, port.first->unit, other.first->unit,
                    unit_name);
                primaries += other.first->primary;
            }
            fatal_if(primaries != 1, "%s: unit %s has %d primary ports,"
                " not one", model->name, port.first->unit, primaries);
            units.push_back(std::make_unique<UnitStats>(this, unit_name));
            unit = units.end() - 1;
        }

        auto stats = std::make_unique<PortStats>(unit->get(),
            statName(port.first->name), port.first->unit_of_work,
            port.first->primary);
        scalarOf[port.at[VCIX_STAT_ADMITTED]] = &stats->admitted;
        scalarOf[port.at[VCIX_STAT_CAPACITY]] = &stats->capacity;
        scalarOf[port.at[VCIX_STAT_CYCLES]] = &stats->cycles;
        scalarOf[port.at[VCIX_STAT_OCCUPANCY]] = &stats->occupancy;
        constant[port.at[VCIX_STAT_CAPACITY]] = true;
        if (port.first->primary) {
            (*unit)->utilizedCycles = stats->admitted / stats->capacity;
        }
        (*unit)->ports.push_back(std::move(stats));
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
    for (const auto &unit : units) {
        for (const auto &port : unit->ports)
            port->primary = port->isPrimary ? 1 : 0;
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
