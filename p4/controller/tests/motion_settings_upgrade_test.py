#!/usr/bin/env python3
"""Exercise the production one-time native motion-setting migrations without NVS/hardware."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'main/storage.c').read_text()
upgrade=source[source.index('void h5_storage_upgrade_motion('):source.index('void h5_storage_poll(')]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
enum {ESP_OK,ESP_ERR_NVS_NOT_FOUND,ESP_FAIL,Status_OK=0,Status_Error=4};
enum {X_AXIS=0,Z_AXIS=2,Setting_AxisMaxRate=110,Setting_AxisAcceleration=120};
static bool ready=true,core_write_ok=true,marker_write_ok=true,setter_ok=true;
static int handle,read_result=ESP_ERR_NVS_NOT_FOUND;
static uint32_t writes,failures;
static unsigned setter_calls,sync_calls,marker_calls;
static uint8_t stored_revision;
static struct {struct {float acceleration,max_rate;} axis[3];} settings;
static esp_err_t nvs_get_u8(int h,const char *key,uint8_t *value) {
 assert(h==handle && !strcmp(key,"motion_rev"));*value=stored_revision;return read_result;
}
static esp_err_t nvs_set_u8(int h,const char *key,uint8_t value) {
 assert(h==handle && !strcmp(key,"motion_rev") && value==2);marker_calls++;
 if(!marker_write_ok)return ESP_FAIL;
 stored_revision=value;read_result=ESP_OK;return ESP_OK;
}
static esp_err_t nvs_commit(int h) {assert(h==handle);return ESP_OK;}
static int settings_store_setting(int id,char *value) {
 assert((id==122 && !strcmp(value,"100")) || (id==110 && !strcmp(value,"300")));setter_calls++;
 if(!setter_ok)return Status_Error;
 if(id==122) settings.axis[2].acceleration=100*3600;
 else settings.axis[0].max_rate=300;
 return Status_OK;
}
static void nvs_buffer_sync_physical(void) {sync_calls++;if(core_write_ok)writes++;}
UPGRADE
static void reset(void) {
 ready=core_write_ok=marker_write_ok=setter_ok=true;read_result=ESP_ERR_NVS_NOT_FOUND;
 stored_revision=0;writes=failures=setter_calls=sync_calls=marker_calls=0;
 settings.axis[0].max_rate=60;settings.axis[0].acceleration=25*3600;settings.axis[2].acceleration=50*3600;
}
int main(void) {
 reset();h5_storage_upgrade_motion();
 if(H5_BENCH_ONLY) {
  assert(settings.axis[2].acceleration==50*3600 && !setter_calls && !marker_calls);
  puts("PASS: benchmark settings are not migrated");return 0;
 }
 assert(settings.axis[0].acceleration==25*3600 && settings.axis[2].acceleration==100*3600);
 assert(settings.axis[0].max_rate==300);
 assert(setter_calls==2 && sync_calls==1 && writes==1 && stored_revision==2);
 // Later explicit tuning must survive subsequent boots, including old defaults.
 settings.axis[2].acceleration=50*3600;settings.axis[0].max_rate=60;
 h5_storage_upgrade_motion();
 assert(setter_calls==2 && settings.axis[2].acceleration==50*3600 && settings.axis[0].max_rate==60);
 // Revision 1 has already migrated Z; do not repeat that migration.
 reset();stored_revision=1;read_result=ESP_OK;h5_storage_upgrade_motion();
 assert(setter_calls==1 && settings.axis[2].acceleration==50*3600);
 assert(settings.axis[0].max_rate==300 && stored_revision==2);
 for(unsigned custom=0;custom<2;custom++) {
  reset();settings.axis[2].acceleration=(custom?100:75)*3600;
  settings.axis[0].max_rate=custom?300:180;
  h5_storage_upgrade_motion();assert(!setter_calls && !sync_calls && stored_revision==2);
  assert(settings.axis[2].acceleration==(custom?100:75)*3600);
  assert(settings.axis[0].max_rate==(custom?300:180));
 }
 reset();ready=false;h5_storage_upgrade_motion();assert(!setter_calls && !marker_calls);
 reset();read_result=ESP_FAIL;h5_storage_upgrade_motion();assert(!setter_calls && !marker_calls);
 reset();stored_revision=2;read_result=ESP_OK;h5_storage_upgrade_motion();assert(!setter_calls && !marker_calls);
 reset();setter_ok=false;h5_storage_upgrade_motion();assert(setter_calls==1 && !sync_calls && !marker_calls);
 reset();stored_revision=1;read_result=ESP_OK;setter_ok=false;
 h5_storage_upgrade_motion();assert(setter_calls==1 && !sync_calls && !marker_calls);
 reset();core_write_ok=false;h5_storage_upgrade_motion();assert(sync_calls==1 && !writes && !marker_calls);
 // Next boot retries old persisted values when a core write failed.
 settings.axis[2].acceleration=50*3600;settings.axis[0].max_rate=60;
 core_write_ok=true;h5_storage_upgrade_motion();
 assert(setter_calls==4 && writes==1 && stored_revision==2);
 reset();marker_write_ok=false;h5_storage_upgrade_motion();assert(writes==1 && failures==1 && !stored_revision);
 marker_write_ok=true;h5_storage_upgrade_motion();assert(setter_calls==2 && stored_revision==2);
 puts("PASS: native $122/$110 migrations, custom tuning preservation, write failures and retry");
}
'''.replace('UPGRADE',upgrade)
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.c').write_text(harness)
    for bench in (0,1):
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',f'-DH5_BENCH_ONLY={bench}',str(path/'test.c'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
