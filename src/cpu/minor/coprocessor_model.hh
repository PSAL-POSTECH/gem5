/* One instance of a Coprocessor model (coprocessor.h), made from a shared
 * library. */

#ifndef __CPU_MINOR_COPROCESSOR_MODEL_HH__
#define __CPU_MINOR_COPROCESSOR_MODEL_HH__

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/statistics.hh"
#include "cpu/minor/coprocessor.h"

namespace gem5
{

namespace minor
{

class CoprocessorTrace;

/** A model's counters as the group 'vcix', one scalar vcix.<group>.<name> per
 *  entry. A reset takes the model's cumulative values as the base; a dump
 *  shows each value less its base. */
class CoprocessorStats : public statistics::Group
{
  public:
    /** Fatal if the model's list of statistics breaks coprocessor.h */
    CoprocessorStats(statistics::Group *parent, const vcix_model *model,
        void *self, CoprocessorTrace *trace);

    void resetStats() override;
    void preDumpStats() override;

  private:
    struct CounterGroup : public statistics::Group
    {
        CounterGroup(statistics::Group *vcix, const std::string &name);

        std::vector<std::unique_ptr<statistics::Scalar>> counters;
    };

    /** Each entry's value as of now, cumulative since create */
    std::vector<uint64_t> read() const;

    const vcix_model *model;
    void *self;
    CoprocessorTrace *trace;

    std::vector<std::unique_ptr<CounterGroup>> groups;

    /** Per entry, the statistic that shows it */
    std::vector<statistics::Scalar *> scalarOf;

    std::vector<uint64_t> base;
};

/* Chrome Trace Event JSON "coprocessor-trace/1": X slices per instruction (issue to commit/squash),
 * C events per unit group when a per-cycle counter delta changes, i instants at stats dump/reset.
 * ts is a Minor cycle. */
class CoprocessorTrace
{
  public:
    static constexpr const char *schema = "coprocessor-trace/1";

    CoprocessorTrace(const std::string &file, const vcix_model *model,
        void *self);
    ~CoprocessorTrace();

    void issued(const vcix_insn &insn, uint64_t id, uint64_t now,
        uint64_t latency);
    void committed(uint64_t id, uint64_t now);
    void squashed(uint64_t first, uint64_t now);
    void cycle(uint64_t now);
    void statsEvent(const char *what);

  private:
    struct Pending
    {
        uint32_t bits;
        uint64_t ts;
        uint64_t latency;
        const char *name;
    };

    struct File
    {
        std::ofstream os;
        bool first = true;
        void close();
    };

    void slice(uint64_t id, const Pending &p, uint64_t now, bool squashed);

    const vcix_model *model;
    void *self;
    std::shared_ptr<File> file;
    std::map<uint64_t, Pending> pending;
    uint64_t lastNow = 0;
    uint64_t dumps = 0;
    uint64_t resets = 0;
    bool primed = false;

    std::vector<std::vector<size_t>> members;
    std::vector<std::string> groupNames;
    std::vector<std::string> counterNames;
    std::vector<uint64_t> prev;
    std::vector<uint64_t> lastEmitted;
};

class CoprocessorModel
{
  public:
    /** Fatal if the library cannot be used or the model refuses the keys;
     *  the model's statistics, if it has any, go under parent */
    CoprocessorModel(const std::string &path,
        const std::vector<std::string> &keys,
        const std::vector<std::string> &values,
        statistics::Group *parent, const std::string &traceFile = "");

    ~CoprocessorModel();

    CoprocessorModel(const CoprocessorModel &) = delete;
    CoprocessorModel &operator=(const CoprocessorModel &) = delete;

    bool
    owns(uint32_t bits) const
    {
        return vcix_owner(model, bits) != nullptr;
    }

    bool
    canAccept(const vcix_insn &insn, uint64_t now) const
    {
        return model->can_accept(self, &insn, now);
    }

    /** The instruction enters; cycles until its result is ready */
    uint64_t
    issue(const vcix_insn &insn, uint64_t id, uint64_t now)
    {
        const uint64_t latency = model->issue(self, &insn, id, now);
        if (trace)
            trace->issued(insn, id, now, latency);
        return latency;
    }

    /** Every issued instruction from first on is taken back */
    void
    squash(uint64_t first, uint64_t now)
    {
        model->squash(self, first, now);
        if (trace)
            trace->squashed(first, now);
    }

    void
    commit(const vcix_insn &insn, uint64_t id, uint64_t now)
    {
        model->commit(self, &insn, id, now);
        if (trace)
            trace->committed(id, now);
    }

    bool ticks() const { return model->tick != nullptr || trace; }

    /** Whether the result of an instruction issued with no latency is ready */
    bool
    ready(uint64_t id, uint64_t now) const
    {
        return model->ready(self, id, now);
    }

    bool answersReady() const { return model->ready != nullptr; }

    /** One cycle, before the cycle's other calls */
    void
    tick(uint64_t now)
    {
        if (model->tick)
            model->tick(self, now);
        if (trace)
            trace->cycle(now);
    }

  private:
    /** The table of the library at path; a library is loaded once */
    static const vcix_model *load(const std::string &path);

    const vcix_model *model;
    void *self;

    std::unique_ptr<CoprocessorTrace> trace;
    std::unique_ptr<CoprocessorStats> stats;
};

} // namespace minor
} // namespace gem5

#endif /* __CPU_MINOR_COPROCESSOR_MODEL_HH__ */
