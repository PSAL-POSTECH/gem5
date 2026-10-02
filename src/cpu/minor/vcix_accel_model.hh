/* One instance of a VCIX accelerator model (vcix_accel.h), made from a shared
 * library. */

#ifndef __CPU_MINOR_VCIX_ACCEL_MODEL_HH__
#define __CPU_MINOR_VCIX_ACCEL_MODEL_HH__

#include <cstdint>
#include <string>
#include <vector>

#include "cpu/minor/vcix_accel.h"

namespace gem5
{

namespace minor
{

class VcixAccelModel
{
  public:
    /** Fatal if the library cannot be used or the model refuses the keys */
    VcixAccelModel(const std::string &path,
        const std::vector<std::string> &keys,
        const std::vector<std::string> &values);

    ~VcixAccelModel();

    VcixAccelModel(const VcixAccelModel &) = delete;
    VcixAccelModel &operator=(const VcixAccelModel &) = delete;

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
};

} // namespace minor
} // namespace gem5

#endif /* __CPU_MINOR_VCIX_ACCEL_MODEL_HH__ */
