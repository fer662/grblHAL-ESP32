#!/usr/bin/env python3
"""One-time USB migration, preserving original H5 data and a full restore backup."""
import argparse,hashlib,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('port');p.add_argument('--backup',required=True,type=Path);p.add_argument('--build',default=Path(__file__).parent/'build',type=Path);a=p.parse_args()
backup=a.backup/'full-flash.bin'
assert backup.stat().st_size==32*1024*1024,'A complete 32 MiB backup is required'
checks=(a.backup/'SHA256SUMS').read_text();assert hashlib.sha256(backup.read_bytes()).hexdigest() in checks,'Backup checksum mismatch'
app=a.build/'h5_grblhal_p4.bin';boot=a.build/'bootloader/bootloader.bin';table=a.build/'partition_table/partition-table.bin';ota=a.build/'ota_data_initial.bin'
assert 1024<app.stat().st_size<2*1024*1024 and boot.stat().st_size<=0x6000
assert table.stat().st_size<=0x1000 and ota.stat().st_size==0x2000
base=[sys.executable,'-m','esptool','--chip','esp32p4','--port',a.port,'--baud','2000000']
def run(*args):subprocess.run(base+list(args),check=True)
before=a.backup/'pre-migration-layout.bin';run('read_flash','0','0x10000',str(before))
# Original settings and partition table must still match the backup. Existing
# probe installations replace only the factory app.
assert before.read_bytes()[0x8000:]==backup.read_bytes()[0x8000:0x10000],'Unexpected existing layout; inspect before migration'
size=(app.stat().st_size+4095)//4096*4096
(a.backup/'original-overwrite-window.bin').write_bytes(backup.read_bytes()[0x10000:0x10000+size])
(a.backup/'original-new-partition-region.bin').write_bytes(backup.read_bytes()[0xf10000:0xf22000])
run('erase_region','0xf10000','0x12000')
run('--after','hard_reset','write_flash','0x2000',str(boot),'0x8000',str(table),'0x10000',str(app),'0xf10000',str(ota))
print('Migrated. Original nvs, phy and storage partitions preserved; new settings are separate.')
