#!/usr/bin/env python3
"""Compile the actual P4 enable callback against GPIO stubs; never touches hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/driver.c').read_text()
start = source.index('static void IRAM_ATTR enable(')
body = source.index('{', start)
level = 1
end = body + 1
while level:
    level += (source[end] == '{') - (source[end] == '}')
    end += 1
callback = source[start:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define IRAM_ATTR
#define H5_X_ENABLE 47
#define H5_Z_ENABLE 30
static bool fault, outputs_ready;
typedef union { uint8_t mask; struct { uint8_t x:1,y:1,z:1,unused:5; }; } axes_signals_t;
static axes_signals_t enable_invert = {.mask=1};
static unsigned nesting, writes, pins[64], GPIO;
static void irq_disable(void) { nesting++; }
static void irq_enable(void) { assert(nesting); nesting--; }
static void gpio_ll_set_level(unsigned *gpio, unsigned pin, unsigned value)
{ (void)gpio; assert(nesting); assert(pin==47 || pin==30); assert(value<=1);pins[pin]=value;writes++; }
CALLBACK
int main(void)
{
    for (unsigned inverted=0; inverted<8; inverted++) {
        enable_invert.mask=inverted;
        for (unsigned ready=0; ready<2; ready++) {
            outputs_ready=ready;
            for (unsigned failed=0; failed<2; failed++) {
                fault=failed;
                for (unsigned hold=0; hold<2; hold++) {
                    for (unsigned requested=0; requested<8; requested++) {
                        writes=0;
                        enable((axes_signals_t){.mask=requested},hold);
                        bool blocked=H5_BENCH_ONLY || failed || !ready;
                        assert(pins[47]==(blocked ? 1 : ((requested^inverted)&1)));
                        assert(pins[30]==(blocked ? 0 : (((requested^inverted)>>2)&1)));
                        assert(writes==2 && nesting==0);
                    }
                }
            }
        }
    }
    // A fault must remove an enable that was already asserted.
    enable_invert.mask=1;outputs_ready=true;fault=false;
    enable((axes_signals_t){.mask=5},false);
    fault=true;
    enable((axes_signals_t){.mask=5},true);
    assert(pins[47]==1 && pins[30]==0);
}
'''.replace('CALLBACK', callback)
with tempfile.TemporaryDirectory(prefix='h5-enable-test-') as directory:
    code=Path(directory)/'test.c'; code.write_text(harness)
    for bench in (0,1):
        executable=Path(directory)/f'enable-{bench}'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                        f'-DH5_BENCH_ONLY={bench}',str(code),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
print('PASS: actual enable callback, all axis/polarity masks, hold, startup inhibition, fault shutdown and bench lock')
