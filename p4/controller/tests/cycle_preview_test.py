#!/usr/bin/env python3
"""Check production preview formatting against display zeros and units; no hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'components/h5_ui/ui.cpp').read_text()
formatter = source[source.index('static void format_cycle_preview('):source.index('static void preview_cycle()')]
harness = r'''
#include "cycle_plan.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
struct Axis {} x,z;
static double work_offset[3];
static double h5_ui_work_offset(Axis *a) { return work_offset[a==&x ? 0 : 2]; }
static const char *h5_ui_work_system() { return "G54"; }
static constexpr int MEASURE_METRIC=0;
static int measure=MEASURE_METRIC;
FORMATTER
int main() {
    h5_cycle_config_t c={};
    c.operation=H5_TURN;c.passes=10;c.starts=1;c.pitch=.1;c.aux_forward=true;
    c.x_min=0;c.x_max=1;c.z_min=-1.5;c.z_max=8.5;c.rpm_limit=500;
    h5_cycle_machine_t m={0,0,400,50,960,200,25,60,1200};
    h5_cycle_plan_t plan;char error[96],text[1100];
    work_offset[2]=-1.5; // Screenshot regression: displayed Z 0..10, machine Z -1.5..8.5.
    assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    const auto saved=plan;
    format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"Z travel bounds: 0.000 to 10.000 mm"));
    assert(strstr(text,"Approach: 0.000 mm | End: 10.000 mm"));
    assert(strstr(text,"X infeed: 0.000 to 1.000 mm | Retracted: -0.500 mm"));
    assert(strstr(text,"lead 0.1000 mm/rev"));
    assert(!strstr(text,"Run-in") && !strstr(text,"(est.)"));
    assert(strstr(text,"G54 work zero and screen units"));
    assert(!strstr(text,"machine coordinates"));
    assert(!memcmp(&saved,&plan,sizeof plan)); // Presentation must not change motion geometry.
    measure=1;format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"Z travel bounds: 0.00000 to 0.39370 in"));
    assert(strstr(text,"Retracted: -0.01969 in"));
    assert(strstr(text,"lead 0.0039 in/rev"));
    assert(!strstr(text," mm"));
    assert(!memcmp(&saved,&plan,sizeof plan));
    // Facing swaps the cutting and depth axes; apply each axis's own zero.
    measure=MEASURE_METRIC;work_offset[0]=-1;c.operation=H5_FACE;
    assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"X travel bounds: 1.000 to 2.000 mm"));
    assert(strstr(text,"Z infeed: 0.000 to 10.000 mm | Retracted: -0.500 mm"));
    c.operation=H5_THREAD;
    assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"Sync/run-up: X clear; Z 0.005 to 0.020 mm"));
    assert(strstr(text,"Full-depth thread: Z 2.420 to 7.585 mm | length 5.165 mm"));
    assert(strstr(text,"X depth: first 1.100, final 2.000 mm | X clear: 0.500 mm"));
    assert(!strstr(text,"(est.)") && !strstr(text,"Usable thread"));
    measure=1;format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"Sync/run-up: X clear; Z 0.00020 to 0.00079 in"));
    assert(strstr(text,"Full-depth thread: Z 0.09528 to 0.29862 in | length 0.20335 in"));
    measure=MEASURE_METRIC;work_offset[0]=0;work_offset[2]=0;
    c.passes=5;c.pitch=.5;c.z_min=0;c.z_max=10;c.rpm_limit=125;
    m.rpm=100;m.z_acceleration=100;
    assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"X infeed while Z moves: 0.020 to 3.020 mm"));
    assert(strstr(text,"Full-depth thread: Z 3.020 to 6.985 mm | length 3.965 mm"));
    assert(strstr(text,"X withdrawal: Z 6.985 to 9.985 mm | Z stop: 10.000 mm"));
    assert(strstr(text,"X depth: first 0.200, final 1.000 mm | X clear: -0.500 mm"));
    c.pitch=-.5;assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"Full-depth thread: Z 6.980 to 3.015 mm | length 3.965 mm"));
    assert(strstr(text,"does not resume mid-pass.")); // Entire footer fits the buffer.
    c.pitch=.5;c.rpm_limit=564;m.rpm=451;m.x_max_rate=300;
    assert(h5_cycle_plan(&c,&m,&plan,error,sizeof error));
    format_cycle_preview(plan,text,sizeof text);
    assert(strstr(text,"Full-depth thread: Z 2.945 to 7.060 mm | length 4.115 mm"));
    assert(strstr(text,"X withdrawal: Z 7.060 to 9.880 mm | Z stop: 10.000 mm"));
    puts("PASS: preview zeros, axis selection, metric/inch positions and lengths; geometry unchanged");
}
'''.replace('FORMATTER', formatter)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(harness)
    include = str(root / 'components/h5_ui')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', include,
                    '-c', str(root / 'components/h5_ui/cycle_plan.c'), '-o', str(path / 'plan.o')], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', include,
                    str(path / 'test.cpp'), str(path / 'plan.o'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
