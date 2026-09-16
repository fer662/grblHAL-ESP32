#!/usr/bin/env python3
"""Exercise production entry queuing, spindle phase setup, index wait, planner and ISR; no hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[3]
p4=root/'p4/controller'
with tempfile.TemporaryDirectory() as directory:
    binary=Path(directory)/'native-thread'
    spindle=(p4/'main/spindle.c').read_text()
    core=(root/'main/grbl/state_machine.c').read_text()
    text=(p4/'tests/thread_entry_native_test.c').read_text()
    text=text.replace('ENTRY_ARM',spindle[spindle.index('void h5_spindle_entry_arm('):spindle.index('static volatile float profile_last_rpm')])
    text=text.replace('ENTRY_RESET',spindle[spindle.index('static void reset_data('):spindle.index('static void set_state(')])
    text=text.replace('ENTRY_BLOCK',spindle[spindle.index('void IRAM_ATTR h5_spindle_block('):spindle.index('void IRAM_ATTR h5_spindle_edge(')])
    a=core.index('uint32_t ms = hal.get_elapsed_ticks();',core.index('case STATE_CYCLE:'))
    b=core.index('} else if(block->spindle.hal->get_data(SpindleData_RPM)',a)
    text=text.replace('INDEX_WAIT',core[a:b])
    harness=Path(directory)/'entry.c';harness.write_text(text)
    sources=[harness,p4/'main/thread_entry.c',p4/'components/h5_ui/cycle_plan.c']
    sources += [root/'main/grbl'/name for name in ('planner.c','stepper.c','pid.c','nuts_bolts.c')]
    command=['cc','-std=c11','-g','-fsanitize=address','-ffunction-sections','-fdata-sections',
             '-I',str(root/'main'),'-I',str(p4/'main'),'-I',str(p4/'components/h5_ui'),
             '-include',str(p4/'main/machine.h'),*[str(p) for p in sources],
             '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-lm','-o',str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True)
