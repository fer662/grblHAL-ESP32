#!/usr/bin/env python3
"""Exercise a single synchronized Z block in the pinned native planner/step ISR; no hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[3]
p4=root/'p4/controller'
with tempfile.TemporaryDirectory() as directory:
    binary=Path(directory)/'native-thread'
    config=p4/'main/machine.h'
    if '--without-preload' in sys.argv:
        config=Path(directory)/'machine.h'
        config.write_text((p4/'main/machine.h').read_text().replace('#define SPINDLE_SYNC_PRELOAD 1', '#define SPINDLE_SYNC_PRELOAD 0'))
    sources=[p4/'tests/thread_native_test.c',p4/'components/h5_ui/cycle_plan.c']
    sources += [root/'main/grbl'/name for name in ('planner.c','stepper.c','pid.c','nuts_bolts.c')]
    command=['cc','-std=c11','-g','-fsanitize=address','-ffunction-sections','-fdata-sections',
             '-I',str(root/'main'),'-I',str(p4/'main'),'-I',str(p4/'components/h5_ui'),
             '-include',str(config),*[str(p) for p in sources],
             '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-lm','-o',str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True)
