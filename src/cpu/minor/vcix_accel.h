/* The boundary between an accelerator model and a simulator: plain C, no
 * simulator headers. A model exports one symbol, vcix_accel_model(). */
#ifndef VCIX_ACCEL_H
#define VCIX_ACCEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VCIX_ACCEL_ABI_VERSION 6u

typedef uint64_t vcix_cycle_t;

/* The model owns an instruction when (bits & mask) == match. `name` is never NULL. */
typedef struct vcix_encoding {
  uint32_t match;
  uint32_t mask;
  const char *name;
} vcix_encoding;

/* A vector operand is a group of 2^lmul_log2 registers when lmul_log2 > 0, one otherwise. */
typedef struct vcix_insn {
  uint32_t bits;
  uint32_t vl;
  uint32_t sew_bits;
  int32_t lmul_log2; /* LMUL = 2^lmul_log2, from -3 to 3 */
} vcix_insn;

/* An instruction issued to this instance and not yet committed. The list is oldest first. */
typedef struct vcix_pending {
  vcix_insn insn;
  vcix_cycle_t issued; /* the cycle it was issued */
  vcix_cycle_t ready;  /* issued + the latency the model answered */
} vcix_pending;

/* get(key) is the text of a top-level scalar as written, or NULL when the key is missing or its
 * value is YAML null, a mapping or a sequence. Valid, with its strings, only until create returns. */
typedef struct vcix_config {
  void *ctx;
  const char *(*get)(void *ctx, const char *key);
} vcix_config;

/* What execute may ask of the simulator. Every callback takes `ctx` back. */
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

/* Valid, with its strings, while the library is loaded. Read abi_version first, and nothing else
 * if it differs. Only `reset` may be NULL, and `encodings` when num_encodings is 0. */
typedef struct vcix_model {
  uint32_t abi_version;
  const char *name;

  const vcix_encoding *encodings;
  size_t num_encodings;

  /* A new instance, or NULL with the reason in `error` (NUL-terminated). Instances share no state. */
  void *(*create)(const vcix_config *config, char *error, size_t error_size);
  void (*destroy)(void *self);

  /* Called by the functional simulator only. */
  void (*execute)(void *self, const vcix_host *host, const vcix_insn *insn);

  /* Called by the timing simulator only. can_accept and latency must not change state. commit is
   * called once, when the instruction commits without a fault. */
  int (*can_accept)(void *self, const vcix_insn *insn, vcix_cycle_t now, const vcix_pending *pending,
                    size_t num_pending);
  vcix_cycle_t (*latency)(void *self, const vcix_insn *insn, vcix_cycle_t now, const vcix_pending *pending,
                          size_t num_pending);
  void (*commit)(void *self, const vcix_insn *insn, vcix_cycle_t now);

  /* Back to the state create left. May be NULL. */
  void (*reset)(void *self);
} vcix_model;

const vcix_model *vcix_accel_model(void);

/* The encoding of `model` that `insn` matches, or NULL. */
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
