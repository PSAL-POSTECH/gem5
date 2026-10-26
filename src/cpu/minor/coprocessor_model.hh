/* One instance of a VCIX accelerator model (coprocessor.h), made from a shared
 * library. */

#ifndef __CPU_MINOR_COPROCESSOR_MODEL_HH__
#define __CPU_MINOR_COPROCESSOR_MODEL_HH__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/statistics.hh"
#include "cpu/minor/coprocessor.h"

namespace gem5
{

namespace minor
{

/** The statistics a model counts at each unit's primary port, as the group
 *  'vcix'. A reset takes the model's cumulative values as the base; a dump
 *  shows each value less its base, except a unit's capacity, a constant. */
class CoprocessorStats : public statistics::Group
{
  public:
    /** Fatal if the model's list of statistics breaks coprocessor.h */
    CoprocessorStats(statistics::Group *parent, const vcix_model *model,
        void *self);

    void resetStats() override;
    void preDumpStats() override;

  private:
    struct UnitStats : public statistics::Group
    {
        UnitStats(statistics::Group *vcix, const std::string &name,
            const std::string &port, const std::string &unit_of_work);

        statistics::Scalar admitted;
        statistics::Scalar capacity;
        statistics::Scalar cycles;
        statistics::Scalar occupancy;
        statistics::Formula utilizedCycles;
    };

    struct CountStats : public statistics::Group
    {
        CountStats(statistics::Group *vcix, const std::string &name);

        std::vector<std::unique_ptr<statistics::Scalar>> names;
    };

    /** Each entry's value as of now, cumulative since create */
    std::vector<uint64_t> read() const;

    const vcix_model *model;
    void *self;

    std::vector<std::unique_ptr<UnitStats>> units;
    std::vector<std::unique_ptr<CountStats>> counts;

    /** Per entry, the statistic that shows it */
    std::vector<statistics::Scalar *> scalarOf;
    std::vector<bool> constant;

    std::vector<uint64_t> base;
};

class CoprocessorModel
{
  public:
    /** Fatal if the library cannot be used or the model refuses the keys;
     *  the model's statistics, if it has any, go under parent */
    CoprocessorModel(const std::string &path,
        const std::vector<std::string> &keys,
        const std::vector<std::string> &values,
        statistics::Group *parent);

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
        return model->issue(self, &insn, id, now);
    }

    /** Every issued instruction from first on is taken back */
    void
    squash(uint64_t first, uint64_t now)
    {
        model->squash(self, first, now);
    }

    void
    commit(const vcix_insn &insn, uint64_t id, uint64_t now)
    {
        model->commit(self, &insn, id, now);
    }

    bool ticks() const { return model->tick != nullptr; }

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
        model->tick(self, now);
    }

  private:
    /** The table of the library at path; a library is loaded once */
    static const vcix_model *load(const std::string &path);

    const vcix_model *model;
    void *self;

    std::unique_ptr<CoprocessorStats> stats;
};

} // namespace minor
} // namespace gem5

#endif /* __CPU_MINOR_COPROCESSOR_MODEL_HH__ */
