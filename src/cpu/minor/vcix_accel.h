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

/* An instruction belongs to the model when (bits & mask) == match. `name` is
 * what a disassembler prints for it; never NULL. */
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

/* The machine description as the simulator's side read it (the Spike adapter;
 * the gem5 config script). get(key) is the text of a top-level scalar as
 * written, with no typing, or NULL when the key is missing or its value is YAML
 * null, a mapping or a sequence. Both give the same answer for the same file.
 * The config and its strings are valid only until configure returns. */
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

/* The table a model library hands over. It and its strings stay valid while
 * the library is loaded. A caller reads abi_version first, and nothing else if
 * it differs. `configure` and `reset` may be NULL; `encodings` only when
 * num_encodings is 0; `name` and the other functions never. */
typedef struct vcix_model {
  uint32_t abi_version;
  const char *name;
  void *self;

  const vcix_encoding *encodings;
  size_t num_encodings;

  /* Called once, before any other function of this table. `name` and
   * `encodings` are fixed before it, so they cannot depend on the machine
   * description. May be NULL. */
  void (*configure)(void *self, const vcix_config *config);

  /* Functional face. Called by the functional simulator only. */
  void (*execute)(void *self, const vcix_host *host, const vcix_insn *insn);

  /* Timing face. Called by the timing simulator only; it sees no data.
   * can_accept and latency are asked at issue, with the instructions in flight
   * (issued, not yet committed), and must not change state: an issued
   * instruction may be squashed and issued again. commit is called once, when
   * the instruction commits without a fault, and is where state changes. */
  int (*can_accept)(void *self, const vcix_insn *insn, vcix_cycle_t now, const vcix_pending *pending,
                    size_t num_pending);
  vcix_cycle_t (*latency)(void *self, const vcix_insn *insn, vcix_cycle_t now, const vcix_pending *pending,
                          size_t num_pending);
  void (*commit)(void *self, const vcix_insn *insn, vcix_cycle_t now);

  /* Back to the state configure left. May be called more than once. May be NULL. */
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
