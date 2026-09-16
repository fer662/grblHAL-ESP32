#!/usr/bin/env python3
"""Exercise the production assisted manual-jog command branch."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'main/follow.c').read_text()
branch = source[source.index('    if (jog) {'):source.index('    if (held)\n')]
harness = r'''
#include "jog_rate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int lock,jog_sign=1;
static char jog_axis;
static double jog_distance,positions[3];
static bool manual_pending,release_requested,manual_motion;
static unsigned stage;
static struct {unsigned mode;double pitch,x_min,x_max,z_min,z_max;} config;
static struct {struct {double max_rate;} axis[3];} settings;
static char command[128];
static double pos(unsigned a) {return positions[a];}
static void h5_critical_enter(int *p,unsigned n) {(void)p;(void)n;}
static void h5_critical_exit(int *p) {(void)p;}
static void send(const char *s) {snprintf(command,sizeof command,"%s",s);}
static void say(const char *s,bool active) {(void)s;(void)active;}
static void run(void) {bool jog=true;char line[118];BRANCH}
int main(void) {
 config.pitch=.3;config.x_min=config.z_min=-100;config.x_max=config.z_max=100;
 for(unsigned mode=0;mode<3;mode++) for(unsigned axis=0;axis<2;axis++) {
  config.mode=mode;jog_axis=axis?'Z':'X';jog_distance=.1;
  settings.axis[0].max_rate=300;settings.axis[2].max_rate=960;
  run();char a;double distance,feed;
  assert(sscanf(command,"$J=G21G91%c%lfF%lf",&a,&distance,&feed)==3);
  assert(a==jog_axis && feed==(axis?960:60));
  assert(fabs(distance-(axis && mode!=2?.3:.1))<1e-6);
  settings.axis[axis?2:0].max_rate=30;run();
  assert(sscanf(command,"$J=G21G91%c%lfF%lf",&a,&distance,&feed)==3 && feed==30);
 }
 puts("PASS: Gearbox/Cone/Async manual override uses normal X/Z jog speed, retaining pitch rounding and axis caps");
}
'''.replace('BRANCH', branch)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'components/h5_ui'),
                    str(path / 'test.c'), '-lm', '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
