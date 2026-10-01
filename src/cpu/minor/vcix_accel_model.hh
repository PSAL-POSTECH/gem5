/* The timing face of a VCIX accelerator model (vcix_accel.h), loaded from a
 * shared library. Execute asks it at issue whether an instruction can start
 * and how long it takes, and tells it at commit that the instruction ran. */

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
    /** The model in the library at path; loaded once per path */
    static VcixAccelModel &load(const std::string &path);

    /** Hand the model its machine description, as parallel keys and
     *  values. Only the first call for a model does anything */
    void configure(const std::vector<std::string> &keys,
        const std::vector<std::string> &values);

    /** Does the model own this instruction */
    bool
    owns(uint32_t bits) const
    {
        return vcix_owner(model, bits) != nullptr;
    }

    bool
    canAccept(const vcix_insn &insn, uint64_t now) const
    {
        return model->can_accept(model->self, &insn, now);
    }

    uint64_t
    latency(const vcix_insn &insn, uint64_t now) const
    {
        return model->latency(model->self, &insn, now);
    }

    /** The instruction committed: the only call that changes the model */
    void
    commit(const vcix_insn &insn, uint64_t now)
    {
        model->commit(model->self, &insn, now);
    }

  private:
    explicit VcixAccelModel(const vcix_model *m) : model(m) {}

    const vcix_model *model;
    bool configured = false;
};

} // namespace minor
} // namespace gem5

#endif /* __CPU_MINOR_VCIX_ACCEL_MODEL_HH__ */
