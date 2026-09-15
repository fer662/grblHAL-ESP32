#!/usr/bin/env python3
"""Exercise the actual upstream state_cycle function with coincident RT events.

Extract only this static function so it can run on the host without the embedded
HAL. Stubs model the state_set contract; the event dispatch ordering is production
code. Optionally pass another state_machine.c to reproduce a baseline failure.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3] / 'main/grbl/state_machine.c'
text = source.read_text()
start = text.index('static void state_cycle (uint_fast16_t rt_exec)\n{')
end = text.index('\n}', start) + 2
function = text[start:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define On true
#define EXEC_CYCLE_START 1
#define EXEC_TOOL_CHANGE 2
#define EXEC_CYCLE_COMPLETE 4
#define EXEC_MOTION_CANCEL 8
#define EXEC_MOTION_CANCEL_FAST 16
#define EXEC_FEED_HOLD 32
#define STATE_IDLE 0
#define STATE_TOOL_CHANGE 1
#define STATE_HOLD 2
static struct { bool suspend; struct { bool execute_hold; } step_control; } sys;
static struct { bool tool_change; } gc_state;
static void suspend_read(bool ignored) { (void)ignored; }
static struct { struct { void (*suspend_read)(bool); } stream; } hal = {{suspend_read}};
static unsigned replans, state;
static void state_await_motion_cancel(uint_fast16_t ignored) { (void)ignored; }
static void (*stateHandler)(uint_fast16_t);
static void st_update_plan_block_parameters(bool fast) { (void)fast; replans++; }
static void state_set(unsigned next) {
    state = next;
    sys.suspend = sys.step_control.execute_hold = false;
    stateHandler = 0;
}
'''
harness += function
harness += r'''
int main(void) {
    unsigned cancels[] = {EXEC_MOTION_CANCEL, EXEC_MOTION_CANCEL_FAST,
                         EXEC_MOTION_CANCEL | EXEC_MOTION_CANCEL_FAST};
    for (unsigned i = 0; i < 3; i++) {
        state_set(STATE_IDLE); replans = 0;
        state_cycle(cancels[i]);
        assert(replans == 1 && sys.suspend && sys.step_control.execute_hold);
        assert(stateHandler == state_await_motion_cancel);
        for (unsigned tool = 0; tool < 2; tool++) {
            state_set(STATE_IDLE); replans = 0; gc_state.tool_change = tool;
            state_cycle(cancels[i] | EXEC_CYCLE_COMPLETE);
            assert(state == (tool ? STATE_TOOL_CHANGE : STATE_IDLE));
            assert(replans == 0 && !sys.suspend && !sys.step_control.execute_hold);
            assert(stateHandler != state_await_motion_cancel);
        }
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(root / 'test.c'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True, cwd=root)
print('PASS: normal cancellation decelerates; coincident completion does not await another completion')
