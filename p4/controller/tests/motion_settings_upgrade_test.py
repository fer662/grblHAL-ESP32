#!/usr/bin/env python3
"""Exercise the production one-time native motion-setting migrations without NVS/hardware."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'main/storage.c').read_text()
validator=source[source.index('static bool valid_preferences('):source.index('bool h5_storage_ready(')]
upgrade=source[source.index('void h5_storage_upgrade_motion('):source.index('void h5_storage_poll(')]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "preferences.h"
VALIDATOR
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
 assert(h==handle && !strcmp(key,"motion_rev") && value==3);marker_calls++;
 if(!marker_write_ok)return ESP_FAIL;
 stored_revision=value;read_result=ESP_OK;return ESP_OK;
}
static esp_err_t nvs_commit(int h) {assert(h==handle);return ESP_OK;}
static int settings_store_setting(int id,char *value) {
 assert((id==122 && !strcmp(value,"100")) || (id==110 && !strcmp(value,"300")) || (id==120 && !strcmp(value,"500")));setter_calls++;
 if(!setter_ok)return Status_Error;
 if(id==122) settings.axis[2].acceleration=100*3600;
 else if(id==110) settings.axis[0].max_rate=300;
 else settings.axis[0].acceleration=500*3600;
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
 h5_preferences_t prefs={.version=1,.mode=0,.measure=0,.pitch_type=0,.pitch=1000,
  .move_step=0,.passes=5,.starts=1,.cone_ratio=1,.jog_mode=1};
 assert(valid_preferences(&prefs)); // Rapids must survive saved-preference validation.
 prefs.move_step=10000;assert(valid_preferences(&prefs));
 prefs.move_step=-1;assert(!valid_preferences(&prefs));
 prefs.move_step=10000001;assert(!valid_preferences(&prefs));
 reset();h5_storage_upgrade_motion();
 if(H5_BENCH_ONLY) {
  assert(settings.axis[2].acceleration==50*3600 && !setter_calls && !marker_calls);
  puts("PASS: benchmark settings are not migrated");return 0;
 }
 assert(settings.axis[0].acceleration==500*3600 && settings.axis[2].acceleration==100*3600);
 assert(settings.axis[0].max_rate==300);
 assert(setter_calls==3 && sync_calls==1 && writes==1 && stored_revision==3);
 // Tuning after this migration survives future boots, including former defaults.
 settings.axis[0].acceleration=25*3600;settings.axis[2].acceleration=50*3600;settings.axis[0].max_rate=60;
 h5_storage_upgrade_motion();assert(setter_calls==3);
 assert(settings.axis[0].acceleration==25*3600 && settings.axis[0].max_rate==60);
 for(unsigned revision=1;revision<=2;revision++) {
  reset();stored_revision=revision;read_result=ESP_OK;settings.axis[0].max_rate=300;
  h5_storage_upgrade_motion();
  assert(setter_calls==1 && settings.axis[2].acceleration==50*3600);
  assert(settings.axis[0].max_rate==300 && settings.axis[0].acceleration==500*3600 && stored_revision==3);
 }
 // Revision 2 explicitly restored X speed to 60: retain that tuning.
 reset();stored_revision=2;read_result=ESP_OK;h5_storage_upgrade_motion();
 assert(settings.axis[0].max_rate==60 && setter_calls==1);
 for(unsigned custom=0;custom<2;custom++) {
  reset();settings.axis[2].acceleration=(custom?100:75)*3600;
  settings.axis[0].max_rate=custom?300:180;settings.axis[0].acceleration=(custom?500:80)*3600;
  h5_storage_upgrade_motion();assert(!setter_calls && !sync_calls && stored_revision==3);
  assert(settings.axis[2].acceleration==(custom?100:75)*3600);
  assert(settings.axis[0].max_rate==(custom?300:180));
  assert(settings.axis[0].acceleration==(custom?500:80)*3600);
 }
 reset();ready=false;h5_storage_upgrade_motion();assert(!setter_calls && !marker_calls);
 reset();read_result=ESP_FAIL;h5_storage_upgrade_motion();assert(!setter_calls && !marker_calls);
 reset();stored_revision=3;read_result=ESP_OK;h5_storage_upgrade_motion();assert(!setter_calls && !marker_calls);
 for(unsigned revision=0;revision<=2;revision++) {
  reset();stored_revision=revision;read_result=ESP_OK;setter_ok=false;
  h5_storage_upgrade_motion();assert(setter_calls==1 && !sync_calls && !marker_calls);
 }
 reset();core_write_ok=false;h5_storage_upgrade_motion();assert(sync_calls==1 && !writes && !marker_calls);
 // Next boot retries old persisted values when a core write failed.
 settings.axis[2].acceleration=50*3600;settings.axis[0].max_rate=60;settings.axis[0].acceleration=25*3600;
 core_write_ok=true;h5_storage_upgrade_motion();
 assert(setter_calls==6 && writes==1 && stored_revision==3);
 reset();marker_write_ok=false;h5_storage_upgrade_motion();assert(writes==1 && failures==1 && !stored_revision);
 marker_write_ok=true;h5_storage_upgrade_motion();assert(setter_calls==3 && stored_revision==3);
 puts("PASS: native $122/$110/$120 migrations, tuning preservation, write failures and retry");
}
'''.replace('UPGRADE',upgrade).replace('VALIDATOR',validator)
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.c').write_text(harness)
    for bench in (0,1):
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'components/h5_ui'),f'-DH5_BENCH_ONLY={bench}',str(path/'test.c'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
