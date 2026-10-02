/* The C boundary between an accelerator model and a simulator. A model exports vcix_accel_model(). */
#ifndef VCIX_ACCEL_H
#define VCIX_ACCEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VCIX_ACCEL_ABI_VERSION 9u

typedef uint64_t vcix_cycle_t;
/* Names an issued instruction; later instructions have larger ids. */
typedef uint64_t vcix_id_t;
/* What issue returns when the cycle the result is ready is not known yet. */
#define VCIX_LATENCY_UNKNOWN UINT64_MAX

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
  int32_t lmul_log2;
} vcix_insn;

/* get(key): a non-null top-level scalar as written, else NULL. Valid only until create returns. */
typedef struct vcix_config {
  void *ctx;
  const char *(*get)(void *ctx, const char *key);
} vcix_config;

/* What execute may ask of the simulator. */
typedef struct vcix_host {
  void *ctx;
  uint32_t (*lanes)(void *ctx);
  uint32_t (*vlen_bits)(void *ctx);
  /* The vlen_bits/8 bytes of vector register `reg` in `lane`. */
  void *(*vreg)(void *ctx, uint32_t lane, uint32_t reg, int will_write);
  uint64_t (*xreg_read)(void *ctx, uint32_t reg);
  void (*xreg_write)(void *ctx, uint32_t reg, uint64_t value);
  uint64_t (*freg_bits)(void *ctx, uint32_t reg);
  void (*mem_read)(void *ctx, uint64_t addr, void *dst, size_t bytes);
  void (*mem_write)(void *ctx, uint64_t addr, const void *src, size_t bytes);
} vcix_host;

/* Valid while the library is loaded. Read abi_version first, and nothing else if it differs. */
typedef struct vcix_model {
  uint32_t abi_version;
  const char *name;

  /* May be NULL when num_encodings is 0. */
  const vcix_encoding *encodings;
  size_t num_encodings;

  /* A new instance, or NULL with the reason in `error`. Instances share no state. */
  void *(*create)(const vcix_config *config, char *error, size_t error_size);
  void (*destroy)(void *self);

  /* Functional simulator only. */
  void (*execute)(void *self, const vcix_host *host, const vcix_insn *insn);

  /* This and the rest down to ready: timing simulator only. Must not change state. */
  int (*can_accept)(void *self, const vcix_insn *insn, vcix_cycle_t now);
  /* Only after can_accept in this cycle. Returns the cycles until ready, or VCIX_LATENCY_UNKNOWN. */
  vcix_cycle_t (*issue)(void *self, const vcix_insn *insn, vcix_id_t id, vcix_cycle_t now);
  /* Takes back every issued instruction from `first` on: state as if they were never issued. */
  void (*squash)(void *self, vcix_id_t first, vcix_cycle_t now);
  /* The oldest issued instruction is final. Every issued instruction gets one commit or is squashed. */
  void (*commit)(void *self, const vcix_insn *insn, vcix_id_t id, vcix_cycle_t now);
  /* First in every cycle, in flight or not; then ready, commit and squash, can_accept and issue. May be NULL. */
  void (*tick)(void *self, vcix_cycle_t now);
  /* Asked every cycle after issue returned VCIX_LATENCY_UNKNOWN, until true. May be NULL if it never does. */
  int (*ready)(void *self, vcix_id_t id, vcix_cycle_t now);

  /* Back to the state create left. May be NULL. */
  void (*reset)(void *self);
} vcix_model;

const vcix_model *vcix_accel_model(void);

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
