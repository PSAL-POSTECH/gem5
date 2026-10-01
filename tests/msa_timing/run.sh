#!/bin/bash
# Time T trips on the original array, the msa at full width and at a quarter width.
# TORCHSIM_DIR must hold a gem5_script/vpu_config.py whose FU pool has an MsaUnit
# (unitType "Msa", CustomMsaVpush/CustomMsaVpop); the stock PyTorchSim one does not yet.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
GEM5=${GEM5:-$HERE/../../build/RISCV/gem5.opt}
GCC=${GCC:-/opt/torchsim-local/toolchain/riscv/bin/riscv64-unknown-elf-gcc}
: "${TORCHSIM_DIR:?set TORCHSIM_DIR to a tree with an MsaUnit in gem5_script/vpu_config.py}"
$GCC -O2 -static -march=rv64gcv -mabi=lp64d "$HERE/msa_timing.c" -o "$HERE/msa_timing.elf"
for m in 0 1 2; do
  o="$HERE/m5out/m$m"; mkdir -p "$o"
  "$GEM5" -r --stdout-file=sto.log -d "$o" "$TORCHSIM_DIR/gem5_script/script_systolic.py" \
    -c "$HERE/msa_timing.elf" -o "$m" --vlane 32 --vlen 256 > "$o/run.log" 2>&1
  echo "mode $m cycles $(grep 'system.cpu.numCycles' "$o/stats.txt" | tail -1 | awk '{print $2}')"
done
