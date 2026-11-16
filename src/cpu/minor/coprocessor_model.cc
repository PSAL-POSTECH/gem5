#include "cpu/minor/coprocessor_model.hh"

#include <dlfcn.h>

#include <cstring>
#include <map>
#include <string>

#include "base/logging.hh"
#include "base/output.hh"
#include "sim/core.hh"

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
    const vcix_model *model, void *self, CoprocessorTrace *trace) :
    statistics::Group(parent, "vcix"),
    model(model),
    self(self),
    trace(trace)
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
    if (trace)
        trace->statsEvent("reset");
}

void
CoprocessorStats::preDumpStats()
{
    statistics::Group::preDumpStats();
    const std::vector<uint64_t> values = read();
    for (size_t i = 0; i < values.size(); i++)
        *scalarOf[i] = double(values[i]) - double(base[i]);
    if (trace)
        trace->statsEvent("dump");
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
    fatal_if(!lib, "Cannot load Coprocessor model: %s", dlerror());

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
    statistics::Group *parent, const std::string &traceFile) :
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

    if (!traceFile.empty())
        trace = std::make_unique<CoprocessorTrace>(traceFile, model, self);

    if (!model->num_stats)
        return;
    fatal_if(!model->stat || !model->read_stats,
        "%s: the table has num_stats but leaves stat or read_stats NULL",
        path);
    if (model->num_stats(self) > 0)
        stats = std::make_unique<CoprocessorStats>(parent, model, self,
            trace.get());
}

namespace
{

constexpr uint64_t processUuid = 1;
constexpr uint64_t statsUuid = 2;
constexpr uint64_t laneUuid = 16;
constexpr uint64_t counterUuid = 1000;

void
putVarint(std::string &s, uint64_t v)
{
    while (v >= 0x80) {
        s.push_back(char(v | 0x80));
        v >>= 7;
    }
    s.push_back(char(v));
}

void
putInt(std::string &s, unsigned field, uint64_t v)
{
    putVarint(s, uint64_t(field) << 3);
    putVarint(s, v);
}

void
putBytes(std::string &s, unsigned field, const std::string &v)
{
    putVarint(s, uint64_t(field) << 3 | 2);
    putVarint(s, v.size());
    s += v;
}

/** DebugAnnotation: name = 10, string_value = 6, int_value = 4 */
std::string
noteStr(const char *name, const std::string &v)
{
    std::string s;
    putBytes(s, 10, name);
    putBytes(s, 6, v);
    return s;
}

std::string
noteInt(const char *name, uint64_t v)
{
    std::string s;
    putBytes(s, 10, name);
    putInt(s, 4, v);
    return s;
}

/** TrackEvent: type = 9, track_uuid = 11, name = 23, annotations = 4,
 *  counter_value = 30. type 1 slice begin, 2 end, 3 instant, 4 counter */
std::string
trackEvent(unsigned type, uint64_t track, const std::string &name,
    const std::vector<std::string> &notes, bool counter, uint64_t value)
{
    std::string e;
    putInt(e, 9, type);
    putInt(e, 11, track);
    if (!name.empty())
        putBytes(e, 23, name);
    for (const std::string &n : notes)
        putBytes(e, 4, n);
    if (counter)
        putInt(e, 30, value);
    return e;
}

} // anonymous namespace

void
CoprocessorTrace::File::close()
{
    if (!os.is_open())
        return;
    os.close();
}

/** TracePacket: timestamp = 8, trusted_packet_sequence_id = 10,
 *  track_event = 11, sequence_flags = 13, track_descriptor = 60; each
 *  packet is field 1 of Trace. */
void
CoprocessorTrace::packet(const std::string &body)
{
    std::string p = body;
    putInt(p, 10, 1);
    putInt(p, 13, firstPacket ? 1 : 2);
    firstPacket = false;
    std::string out;
    putBytes(out, 1, p);
    file->os.write(out.data(), out.size());
}

/** TrackDescriptor: uuid = 1, name = 2, process = 3 (pid = 1,
 *  process_name = 6), counter = 8, parent_uuid = 5 */
void
CoprocessorTrace::descriptor(uint64_t uuid, uint64_t parent,
    const std::string &name, bool process, bool counter)
{
    std::string d;
    putInt(d, 1, uuid);
    putBytes(d, 2, name);
    if (process) {
        std::string pd;
        putInt(pd, 1, 1);
        putBytes(pd, 6, name);
        putBytes(d, 3, pd);
    }
    if (parent)
        putInt(d, 5, parent);
    if (counter)
        putBytes(d, 8, std::string());
    std::string p;
    putBytes(p, 60, d);
    packet(p);
}

CoprocessorTrace::CoprocessorTrace(const std::string &path,
    const vcix_model *model, void *self) :
    model(model), self(self), file(std::make_shared<File>())
{
    const std::string resolved = simout.resolve(path);
    file->os.open(resolved, std::ios::binary);
    fatal_if(!file->os, "coprocessorTraceFile: cannot open %s", resolved);

    descriptor(processUuid, 0, std::string("coprocessor ") + model->name,
        true, false);
    descriptor(statsUuid, processUuid, "stats", false, false);
    std::string p;
    putInt(p, 8, 0);
    putBytes(p, 11, trackEvent(3, statsUuid, "schema",
        {noteStr("schema", schema), noteStr("model", model->name),
         noteStr("ts_unit", "cycle (1 ns per cycle)")}, false, 0));
    packet(p);

    const size_t n = model->num_stats ? model->num_stats(self) : 0;
    prev.assign(n, 0);
    lastEmitted.assign(n, 0);
    counterNames.resize(n);
    for (size_t i = 0; i < n; i++) {
        const vcix_stat *e = model->stat(self, i);
        counterNames[i] = std::string(e->group) + "." + e->name;
        if (!strcmp(e->group, "committed"))
            continue;
        descriptor(counterUuid + counted.size(), processUuid,
            counterNames[i], false, true);
        counted.push_back(i);
    }

    std::shared_ptr<File> shared = file;
    registerExitCallback([shared]() { shared->close(); });
}

CoprocessorTrace::~CoprocessorTrace()
{
    file->close();
}

void
CoprocessorTrace::slice(uint64_t id, const Pending &p, uint64_t now,
    bool squashed)
{
    char bits[16];
    snprintf(bits, sizeof(bits), "0x%08x", p.bits);
    std::vector<std::string> notes = {noteInt("id", id),
        noteStr("bits", bits)};
    if (p.latency == VCIX_LATENCY_UNKNOWN)
        notes.push_back(noteStr("latency", "unknown"));
    else
        notes.push_back(noteInt("latency", p.latency));
    if (squashed)
        notes.push_back(noteInt("squashed", 1));
    const uint64_t track = laneUuid + p.lane;

    std::string b;
    putInt(b, 8, p.ts);
    putBytes(b, 11, trackEvent(1, track, p.name, notes, false, 0));
    packet(b);
    std::string e;
    putInt(e, 8, now);
    putBytes(e, 11, trackEvent(2, track, "", {}, false, 0));
    packet(e);
    laneBusy[p.lane] = false;
}

void
CoprocessorTrace::issued(const vcix_insn &insn, uint64_t id, uint64_t now,
    uint64_t latency)
{
    const vcix_encoding *e = vcix_owner(model, insn.bits);
    size_t lane = 0;
    while (lane < laneBusy.size() && laneBusy[lane])
        lane++;
    if (lane == laneBusy.size()) {
        laneBusy.push_back(false);
        descriptor(laneUuid + lane, processUuid,
            lane ? "insn " + std::to_string(lane + 1) : "insn", false, false);
    }
    laneBusy[lane] = true;
    pending[id] = Pending{insn.bits, now, latency, e ? e->name : "?", lane};
}

void
CoprocessorTrace::committed(uint64_t id, uint64_t now)
{
    auto it = pending.find(id);
    if (it == pending.end())
        return;
    slice(id, it->second, now, false);
    pending.erase(it);
}

void
CoprocessorTrace::squashed(uint64_t first, uint64_t now)
{
    auto it = pending.lower_bound(first);
    for (; it != pending.end(); it = pending.erase(it))
        slice(it->first, it->second, now, true);
}

void
CoprocessorTrace::cycle(uint64_t now)
{
    lastNow = now;
    const size_t n = prev.size();
    if (n == 0)
        return;
    std::vector<uint64_t> values(n);
    model->read_stats(self, values.data());
    if (!primed) {
        prev = values;
        primed = true;
        return;
    }
    for (size_t t = 0; t < counted.size(); t++) {
        const size_t i = counted[t];
        const uint64_t delta = values[i] - prev[i];
        if (delta == lastEmitted[i])
            continue;
        lastEmitted[i] = delta;
        std::string p;
        putInt(p, 8, now);
        putBytes(p, 11, trackEvent(4, counterUuid + t, "", {}, true, delta));
        packet(p);
    }
    prev = values;
}

void
CoprocessorTrace::statsEvent(const char *what)
{
    const bool dump = !strcmp(what, "dump");
    std::string p;
    putInt(p, 8, lastNow);
    putBytes(p, 11, trackEvent(3, statsUuid, what,
        {noteInt("index", dump ? dumps++ : resets++)}, false, 0));
    packet(p);
}

CoprocessorModel::~CoprocessorModel()
{
    model->destroy(self);
}

} // namespace minor
} // namespace gem5
