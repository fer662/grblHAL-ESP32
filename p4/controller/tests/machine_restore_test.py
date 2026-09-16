#!/usr/bin/env python3
"""Exercise production machine-state persistence against an in-memory NVS."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/storage.c').read_text()
state = source[source.index('typedef struct {'):source.index('static bool valid_preferences')]
init = source[source.index('void h5_storage_init('):source.index('bool h5_preferences_get(')]
poll = source[source.index('void h5_storage_poll('):source.index('status_code_t h5_storage_command(')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "preferences.h"
#define N_AXIS 3
#define X_AXIS 0
#define Z_AXIS 2
#define ESP_OK 0
#define NVS_READWRITE 1
#define pdMS_TO_TICKS(ms) (ms)
static int handle,lock;
static uint32_t now,writes,failures,changed_at;
static bool ready,valid,dirty,moving,queued,serial_pending,fail_write;
static h5_preferences_t preferences;
static struct {bool driver_started;int32_t position[3];} sys;
static uint32_t xTaskGetTickCount(void) {return now;}
static void h5_critical_enter(int *l,int tag) {(void)l;(void)tag;}
static void h5_critical_exit(int *l) {(void)l;}
static bool flash_idle(void) {return !moving;}
static bool h5_bridge_empty(void) {return !queued;}
static bool h5_serial_pending(void) {return serial_pending;}
static bool valid_preferences(const h5_preferences_t *p) {return p->version==1;}
STATE
static machine_state_t disk;
static size_t disk_size;
static bool disk_present,mutate_during_write;
static int nvs_flash_init_partition(const char *p) {(void)p;return 0;}
static int nvs_open_from_partition(const char *p,const char *n,int mode,int *h) {(void)p;(void)n;(void)mode;*h=1;return 0;}
static int nvs_get_blob(int h,const char *key,void *p,size_t *size) {
 (void)h;
 if(strcmp(key,"machine_v1") || !disk_present)return -1;
 memcpy(p,&disk,disk_size<*size?disk_size:*size);*size=disk_size;return 0;
}
static int nvs_set_blob(int h,const char *key,const void *p,size_t size) {
 (void)h;
 if(fail_write)return -1;
 if(!strcmp(key,"machine_v1")) {
  assert(size==sizeof(disk));memcpy(&disk,p,size);disk_size=size;disk_present=true;
  if(mutate_during_write) {mutate_during_write=false;h5_saved_disabled_set(5);}
 }
 return 0;
}
static int nvs_commit(int h) {(void)h;return 0;}
INIT
POLL
static void boot(void) {
 machine=(machine_state_t){.version=1,.limits={INT32_MIN,INT32_MAX,INT32_MIN,INT32_MAX}};
 machine_valid=machine_dirty=position_restored=false;machine_changed_at=machine_generation=0;
 ready=valid=dirty=false;now=0;memset(&sys,0,sizeof(sys));
 h5_storage_init();h5_storage_restore_position(&sys.position);sys.driver_started=true;
}
int main(void) {
 boot();assert(!machine_valid && !h5_saved_disabled_get());
 int32_t limits[4];h5_saved_limits_get(limits);assert(limits[0]==INT32_MIN && limits[3]==INT32_MAX);
 sys.position[0]=-12345;sys.position[2]=54321;
 const int32_t bounds[]={-12000,24000,INT32_MIN,100000};
 h5_saved_limits_set(bounds);h5_saved_disabled_set(1);
 h5_storage_poll();assert(!disk_present);now=499;h5_storage_poll();assert(!disk_present);
 now=500;h5_storage_poll();assert(disk_present && writes==1 && !machine_dirty);
 for(unsigned i=0;i<10;i++) {now+=1000;h5_storage_poll();}assert(writes==1);
 boot();assert(sys.position[0]==-12345 && sys.position[2]==54321);
 assert(h5_saved_disabled_get()==1);h5_saved_limits_get(limits);assert(!memcmp(limits,bounds,sizeof bounds));
 // Motion cannot save partial positions, nor can queued commands race a flash write.
 moving=true;sys.position[2]=999;now=1000;h5_storage_poll();assert(writes==1);
 moving=false;queued=true;h5_storage_poll();assert(writes==1);
 queued=false;h5_storage_poll();now+=500;fail_write=true;h5_storage_poll();
 assert(machine_dirty && failures==1 && writes==1);
 fail_write=false;h5_storage_poll();assert(!machine_dirty && disk.position[2]==999 && writes==2);
 // A UI update concurrent with flash commit remains dirty and is saved next.
 h5_saved_disabled_set(4);now+=500;mutate_during_write=true;h5_storage_poll();
 assert(machine_dirty && disk.disabled==4);now+=500;h5_storage_poll();assert(!machine_dirty && disk.disabled==5);
 boot();assert(h5_saved_disabled_get()==5 && sys.position[2]==999);
 // Crossed limits are a valid editable UI state: preserve them without losing position.
 const int32_t crossed[]={24000,-12000,0,0};h5_saved_limits_set(crossed);now+=500;h5_storage_poll();
 boot();h5_saved_limits_get(limits);assert(!memcmp(limits,crossed,sizeof crossed) && sys.position[2]==999);
 // Invalid version/size/mask are rejected together, never partially restored.
 const machine_state_t good=disk;
 for(unsigned invalid=0;invalid<4;invalid++) {
  disk=good;disk_size=sizeof(disk);
  if(invalid==0)disk.version=2;
  if(invalid==1)disk_size--;
  if(invalid==2)disk.disabled=2;
  if(invalid==3)disk.position[1]=1;
  boot();assert(!machine_valid && !h5_saved_disabled_get());
  assert(!sys.position[0] && !sys.position[2]);
 }
 puts("PASS: positions/limits/disabled round-trip, idle-only writes, debounce, retry, concurrent edits and corrupt records");
}
'''.replace('STATE', state).replace('INIT', init).replace('POLL', poll)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'components/h5_ui'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
