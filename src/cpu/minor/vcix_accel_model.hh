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
    canAccept(const vcix_insn &insn, uint64_t now,
        const std::vector<vcix_pending> &pending) const
    {
        return model->can_accept(self, &insn, now, pending.data(),
            pending.size());
    }

    /** Cycles until the result of a just-accepted instruction is ready */
    uint64_t
    latency(const vcix_insn &insn, uint64_t now,
        const std::vector<vcix_pending> &pending) const
    {
        return model->latency(self, &insn, now, pending.data(),
            pending.size());
    }

    /** The instruction committed: the instance is busy from the next cycle */
    void
    commit(const vcix_insn &insn, uint64_t now)
    {
        model->commit(self, &insn, now);
        busy = model->tick != nullptr;
    }

    /** Whether the instance must be ticked in the next cycle */
    bool isBusy() const { return busy; }

    /** One cycle of a busy instance, before the cycle's other calls */
    void
    tick(uint64_t now)
    {
        busy = model->tick(self, now);
    }

  private:
    /** The table of the library at path; a library is loaded once */
    static const vcix_model *load(const std::string &path);

    const vcix_model *model;
    void *self;
    bool busy = false;
};

} // namespace minor
} // namespace gem5

#endif /* __CPU_MINOR_VCIX_ACCEL_MODEL_HH__ */
