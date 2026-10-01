/* The boundary between an accelerator model and a simulator: plain C, no
 * simulator headers. A model exports one symbol, vcix_accel_model(). */
#ifndef VCIX_ACCEL_H
#define VCIX_ACCEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VCIX_ACCEL_ABI_VERSION 5u

typedef uint64_t vcix_cycle_t;

/* An instruction belongs to the model when (bits & mask) == match. */
typedef struct vcix_encoding {
  uint32_t match;
  uint32_t mask;
  const char *name;
} vcix_encoding;

/* An instruction as both faces receive it: its bits and the vector
 * configuration it was decoded under. A vector operand names a group of
 * 2^lmul_log2 registers when lmul_log2 > 0, one register otherwise. */
typedef struct vcix_insn {
  uint32_t bits;
  uint32_t vl;
  uint32_t sew_bits;
  int32_t lmul_log2; /* LMUL = 2^lmul_log2, from -3 to 3 */
} vcix_insn;

/* An instruction the timing simulator has issued to this model and not yet
 * committed. The simulator keeps the list, oldest first, and drops an entry
 * when the instruction commits or is squashed. */
typedef struct vcix_pending {
  vcix_insn insn;
  vcix_cycle_t issued; /* the cycle it was issued */
  vcix_cycle_t ready;  /* issued + the latency the model answered */
} vcix_pending;

/* The machine description, as the adapter read it: the value of a top-level
 * key as written in the file, or NULL when the file has no such key. The
 * model does not know the file or its format. */
typedef struct vcix_config {
  void *ctx;
  const char *(*get)(void *ctx, const char *key);
} vcix_config;

/* What the functional face may ask of the simulator. Filled by the adapter;
 * every callback takes `ctx` back as its first argument. */
typedef struct vcix_host {
  void *ctx;
  uint32_t (*lanes)(void *ctx);
  uint32_t (*vlen_bits)(void *ctx);
  /* The bytes of vector register `reg` in `lane`, vlen_bits/8 of them. */
  void *(*vreg)(void *ctx, uint32_t lane, uint32_t reg, int will_write);
  uint64_t (*xreg_read)(void *ctx, uint32_t reg);
  void (*xreg_write)(void *ctx, uint32_t reg, uint64_t value);
  /* The low 64 bits of floating-point register `reg`, as stored. */
  uint64_t (*freg_bits)(void *ctx, uint32_t reg);
  void (*mem_read)(void *ctx, uint64_t addr, void *dst, size_t bytes);
  void (*mem_write)(void *ctx, uint64_t addr, const void *src, size_t bytes);
} vcix_host;

typedef struct vcix_model {
  uint32_t abi_version;
  const char *name;
  void *self;

  const vcix_encoding *encodings;
  size_t num_encodings;

  /* Called once by either simulator, after loading and before anything else.
   * The config is valid only during the call. */
  void (*configure)(void *self, const vcix_config *config);

  /* Functional face. Called by the functional simulator only. */
  void (*execute)(void *self, const vcix_host *host, const vcix_insn *insn);

  /* Timing face. Called by the timing simulator only; it sees no data.
   * can_accept and latency are asked when the instruction is issued and must
   * not change state: an issued instruction may be squashed and issued again.
   * They are given the instructions already issued and not yet committed, so
   * the answer can account for what is in flight; an accepted instruction
   * does not hold the unit, and how many may be in flight is the model's to
   * decide. latency is asked only of an instruction can_accept just accepted.
   * commit is called once, when the instruction commits without a fault, and
   * is where the timing state changes. vl, SEW and LMUL arrive with the
   * instruction, so latency can depend on how much data it moves. */
  int (*can_accept)(void *self, const vcix_insn *insn, vcix_cycle_t now, const vcix_pending *pending,
                    size_t num_pending);
  vcix_cycle_t (*latency)(void *self, const vcix_insn *insn, vcix_cycle_t now, const vcix_pending *pending,
                          size_t num_pending);
  void (*commit)(void *self, const vcix_insn *insn, vcix_cycle_t now);

  void (*reset)(void *self);
} vcix_model;

const vcix_model *vcix_accel_model(void);

/* The encoding of `model` that `insn` matches, or NULL when the model does not
 * own it. Both adapters decide ownership with this and nothing else. */
static inline const vcix_encoding *vcix_owner(const vcix_model *model, uint32_t insn) {
  for (size_t i = 0; i < model->num_encodings; i++) {
    const vcix_encoding *e = &model->encodings[i];
    if ((insn & e->mask) == e->match) return e;
  }
  return NULL;
}

#ifdef __cplusplus
}
#endif

#endif
