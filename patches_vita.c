#include <string.h>

#include <cfwmacros.h>

#include "rebootex.h"
#include "pspbtcnf.h"

extern int redirect_flash;

int (*pspemuLfatOpen)(BootFile* filename, u32 a1, u32 a2, u32 a3, u32 t0) = NULL;
int (*SetMemoryPartitionTable)(void *sysmem_config, SceSysmemPartTable *table) = NULL;

// Load Core module_start Hook
int loadcoreModuleStartVita(unsigned int args, void* argp, int (* start)(SceSize, void *))
{
    loadCoreModuleStartCommon((u32)start);
    flushCache();
    return start(args, argp);
}

void relocateFlashFile(BootFile* file){
    static u8* curbuf = (u8*)PTR_ALIGN_64(ARK_FLASH+MAX_FLASH0_SIZE);
    memcpy((void *)curbuf, file->buffer, file->size);
    file->buffer = (void *)curbuf;
    curbuf += file->size + 64;
    curbuf = PTR_ALIGN_64(curbuf);
}

int _pspemuLfatOpen(BootFile* file, u32 a1, u32 a2, u32 a3, u32 t0)
{
    char* p = file->name;
    if (pspemuLfatOpenExtra(file) == 0){
        return 0;
    }
    else if (strcmp(p, REBOOT_MODULE) == 0){
        file->buffer = reboot_conf->rtm_mod.buffer;
        file->size = reboot_conf->rtm_mod.size;
        relocateFlashFile(file);
        reboot_conf->rtm_mod.buffer = NULL;
        reboot_conf->rtm_mod.size = 0;
        return 0;
    }
    pspemuLfatOpen(file, a1, a2, a3, t0);
    return 0;
}

int UnpackBootConfigVita(char **p_buffer, int length){
    int res = (*UnpackBootConfig)(*p_buffer, length);
    if(reboot_conf->rtm_mod.before && reboot_conf->rtm_mod.buffer && reboot_conf->rtm_mod.size)
    {
        //add reboot prx entry
        res = AddPRX(*p_buffer, reboot_conf->rtm_mod.before, REBOOT_MODULE, reboot_conf->rtm_mod.flags);
    }
    return res;
}

//extra ram through flash0 ramfs on Vita
void SetMemoryPartitionTablePatched(void *sysmem_config, SceSysmemPartTable *table)
{
    // Add flash0 ramfs as partition 11, only the first 16MB are safe to use
    SetMemoryPartitionTable(sysmem_config, table);
    table->extVshell.addr = EXTRA_RAM;
    table->extVshell.size = EXTRA_RAM_SIZE/2;
}

int PatchSysMem(void *a0, void *sysmem_config)
{
    int (* module_bootstart)(SceSize args, void *sysmem_config) = (void *)_lw((u32)a0 + 0x28);
    u32 text_addr = SYSMEM_TEXT;
    u32 top_addr = text_addr+0x14000;
    int patches = 1;
    for (u32 addr = text_addr; addr<top_addr && patches; addr += 4) {
        u32 data = _lw(addr);
        if (data == 0x247300FF){
            SetMemoryPartitionTable = (void*)K_EXTRACT_CALL(addr-20);
            _sw(JAL(SetMemoryPartitionTablePatched), addr-20);
            patches--;
        }
    }

    flushCache();

    return module_bootstart(4, sysmem_config);
}

/*
	0x89FF0000: Apitype
	0x89FF0004: vsh: 2, update: 3, pops: 4, licence: 5, app: 6, umd: 7, mlnapp: 8
	0x89FF0008: Path #1
	0x89FF0048: Path #2
	0x89FF0088: Path #3
	0x89FF00C8: SFO buffer
	0x89FF14C8: 0x000001F4
	0x89FF14CC: 0x000000DC
	0x89FF14D0: 0x00060313
	0x89FF1510: TITLEID
	0x89FF1520: 0x00000003
	0x89FF1540: Path #4
	0x89FF1590: Version
*/
#ifdef PAYLOADEX
int loadParamsPatched(int a0) {
	int v0 = _lw(a0 + 12);
	int v1 = _lw(a0 + 16);
	_sw(_lw(0x89FF0000), (v1 + (v0 << 5)) - 12);
	return 0;
}
#endif

// patch reboot on ps vita
void patchRebootBuffer(){

    #ifdef PAYLOADEX
    *(u32 *)0x89FF0000 = 0x200;
	*(u32 *)0x89FF0004 = 0x2;
    #endif

    // hijack UnpackBootConfig to insert modules at runtime
    _sw(0x27A40004, UnpackBootConfigArg); // addiu $a0, $sp, 4
    _sw(JAL(UnpackBootConfigVita), UnpackBootConfigCall); // Hook UnpackBootConfig

    for (u32 addr = reboot_start; addr<reboot_end; addr+=4){
        u32 data = _lw(addr);
        if (data == 0xAFBF0000 && _lw(addr + 8) == 0x00000000) {
        	pspemuLfatOpen = (void *)K_EXTRACT_CALL(addr + 4);
        	_sw(JAL(_pspemuLfatOpen), addr + 4);
        }
        else if (data == 0x00600008){ // found loadcore jump on Vita
            // Move LoadCore module_start Address into third Argument
            _sw(0x00603021, addr); // move a2, v1
            // Hook LoadCore module_start Call
            _sw(JUMP(loadcoreModuleStartVita), addr+8);
        }
        else if (data == 0x24040004) {
            _sw(0x02402021, addr); //move $a0, $s2
            _sw(JAL(PatchSysMem), addr + 0x64); // Patch call to SysMem module_bootstart
        }
        else if ((data & 0x0000FFFF) == 0x8B00 && redirect_flash){
            _sb(0xA0, addr); // Link Filesystem Buffer to 0x8BA00000
        }
        #ifdef PAYLOADEX
        // Find sceBoot
		else if (data == 0x27BD01C0) {
			extern void* sceReboot;
            sceReboot = (void *)(addr + 4);
		}
        // Don't load pspemu params
		else if (data == 0x240500CF) {
			MAKE_CALL(addr + 4, loadParamsPatched);
		}
        #endif
    }
    // Flush Cache
    flushCache();
}

