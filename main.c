/*
 * This file is part of PRO CFW.

 * PRO CFW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * PRO CFW is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PRO CFW. If not, see <http://www.gnu.org/licenses/ .
 */

#include "rebootex.h"

#ifdef REBOOTEX
#define END_BUF_STR "ApplyPspRelSection"
#ifdef MS_IPL
#include "syscon.h"
#endif
#else
#define END_BUF_STR "StopBoot"
#define SYSCON_CTRL_RTRG 0x00000400
#define SYSCON_CTRL_HOME 0x00001000

ARKConfig _arkconf = {
    .magic = ARK_CONFIG_MAGIC,
#ifndef MS_IPL
    .arkpath = "ms0:/PSP/SAVEDATA/ARK_01234/", // default path for ARK files
    .exploit_id = CIPL_EXPLOIT_ID,
#else
    .arkpath = ARK_DC_PATH "/ARK_01234/", // default path for ARK files
    .exploit_id = DC_EXPLOIT_ID,
#endif
    .launcher = {0},
    .exec_mode = PSP_ORIG, // run ARK in PSP mode
    .recovery = 0,
};
#endif

RebootConfigARK* reboot_conf = (RebootConfigARK*)REBOOTEX_CONFIG;
ARKConfig* ark_config = (ARKConfig*)ARK_CONFIG;

// sceReboot Main Function
int (* sceReboot)(int, int, int, int, int, int, int) = (void *)(REBOOT_TEXT);

// Instruction Cache Invalidator
void (* sceRebootIcacheInvalidateAll)(void) = NULL;

// Data Cache Invalidator
void (* sceRebootDacheWritebackInvalidateAll)(void) = NULL;

// Sony PRX Decrypter Function Pointer
int (* SonyPRXDecrypt)(void *, unsigned int, unsigned int *) = NULL;
int (* origCheckExecFile)(unsigned char * addr, void * arg2) = NULL;
int (* extraPRXDecrypt)(void *, unsigned int, unsigned int *) = NULL;
int (* extraCheckExec)(unsigned char * addr, void * arg2) = NULL;

// UnpackBootConfig
int (* UnpackBootConfig)(char * buffer, int length) = NULL;
extern int UnpackBootConfigPatched(char **p_buffer, int length);
u32 UnpackBootConfigCall = 0;
u32 UnpackBootConfigArg = 0;
u32 reboot_start = 0;
u32 reboot_end = 0;
u32 loadcore_text = 0;

//io flags
int rebootmodule_set = 0;
int rebootmodule_open = 0;
char *p_rmod = NULL;
int size_rmod = 0;
void* rtm_buf = NULL;
int rtm_size = 0;

//io functions
int (* sceBootLfatOpen)(char * filename) = NULL;
int (* sceBootLfatRead)(char * buffer, int length) = NULL;
int (* sceBootLfatClose)(void) = NULL;

// implementation specific patches
extern patchRebootBuffer();

// Custom PRX Support
int ARKPRXDecrypt(PSP_Header* prx, unsigned int size, unsigned int * newsize)
{
    // Custom Packed PRX File
    if ( (_lb((u8*)prx + 0x150) == 0x1F && _lb((u8*)prx + 0x151) == 0x8B) // GZIP
            || prx->oe_tag == 0xC01DB15D // PRO-type PRX
            || prx->oe_tag == 0xC6BA41D3 // ME-type PRX
    ){

        #ifdef PAYLOADEX
        #ifndef MS_IPL
        if (prx->oe_tag == 0xC6BA41D3 && extraPRXDecrypt){ // decrypt ME firmware file
            extraPRXDecrypt(prx, size, newsize);
        }
        #endif
        #endif

        // Read GZIP Size
        *newsize = prx->comp_size;
        
        // Remove PRX Header
        memcpy(prx, (u8*)prx + 0x150, prx->comp_size);
        
        // Fake Decrypt Success
        return 0;
    }
    
    // Decrypt Sony PRX Files
    return SonyPRXDecrypt(prx, size, newsize);
}


int CheckExecFilePatched(unsigned char * addr, void * arg2)
{
#ifndef MS_IPL
    //scan structure
    //6.31 kernel modules use type 3 PRX... 0xd4~0x10C is zero padded
    int pos = 0; for(; pos < 0x38; pos++)
    {
        //nonzero byte encountered
        if(addr[pos + 212])
        {
            //forward to unsign function?
            return origCheckExecFile(addr, arg2);
        }
    }
#endif

    #ifdef PAYLOADEX
    #ifndef MS_IPL
    if (extraCheckExec){
        extraCheckExec(addr, arg2);
    }
    #endif
    #endif

    //return success
    return 0;
}

void unPatchLoadCorePRXDecrypt(){
    u32 decrypt_call = JAL(ARKPRXDecrypt);
    u32 text_addr = loadcore_text;
    u32 top_addr = text_addr+0x8000;

    for (u32 addr = text_addr; addr<top_addr; addr+=4) {
        if (_lw(addr) == decrypt_call){
            _sw(JAL(SonyPRXDecrypt), addr);
        }
    }

}

void unPatchLoadCoreCheckExec(){
    u32 check_call = JAL(CheckExecFilePatched);
    u32 text_addr = loadcore_text;
    u32 top_addr = text_addr+0x8000;

    for (u32 addr = text_addr; addr<top_addr; addr+=4) {
        if (_lw(addr) == check_call){
            _sw(JAL(origCheckExecFile), addr);
        }
    }

}

u32 loadCoreModuleStartCommon(u32 module_start){

    // Calculate Text Address and size
    u32 text_addr = module_start-0xAF8;
    u32 top_addr = text_addr+0x8000; // read 32KB at most (more than enough to scan loadcore)
    
    // Fetch Original Decrypt Function Stub
    SonyPRXDecrypt = (void *)FindImportRange("memlmd", 0xEF73E85B, text_addr, top_addr);
    origCheckExecFile = (void *)FindImportRange("memlmd", 0x6192F715, text_addr, top_addr);

    u32 decrypt_call = JAL(SonyPRXDecrypt);
    u32 check_call = JAL(origCheckExecFile);

    int devkit_patched = 0;

    // Hook Signcheck Function Calls
    for (u32 addr = text_addr; addr<top_addr; addr+=4){
        u32 data = _lw(addr);
        if (data == decrypt_call){
            _sw(JAL(ARKPRXDecrypt), addr);
        }
        else if (data == check_call){
            _sw(JAL(CheckExecFilePatched), addr);
        }
        else if (!devkit_patched && data == 0x24040015){
            // Don't break on unresolved syscalls
            u32 a = addr;
            do { a-=4; } while (_lw(a) != 0x27BD0030);
            _sw(0x00001021, a+4);
            devkit_patched = 1;
        }
    }

    loadcore_text = text_addr;
    return text_addr;
}

// Invalidate Instruction and Data Cache
void flushCache(void)
{
    // Invalidate Data Cache
    sceRebootDacheWritebackInvalidateAll();
    // Invalidate Instruction Cache
    sceRebootIcacheInvalidateAll();
}

// Common rebootex patches for PSP and Vita
u32 findRebootFunctions(u32 reboot_start){
    register void (* Icache)(void) = NULL;
    register void (* Dcache)(void) = NULL;
    u32 reboot_end = 0;
    // find functions
    for (u32 addr = reboot_start; ; addr+=4){
        u32 data = _lw(addr);
        if (data == 0xBD01FFC0){ // sceRebootDacheWritebackInvalidateAll
            u32 a = addr;
            do {a-=4;} while (_lw(a) != 0x40088000);
            Dcache = (void*)a;
        }
        else if (data == 0xBD14FFC0){ // sceRebootIcacheInvalidateAll
            u32 a = addr;
            do {a-=4;} while (_lw(a) != 0x40088000);
            Icache = (void*)a;
        }
#ifdef REBOOTEX
        else if (data == 0x8FA50008 && _lw(addr+8) == 0x8FA40004){ // UnpackBootConfig
            UnpackBootConfigArg = addr+8;
            u32 a = addr;
            do { a+=4; } while (_lw(a) != 0x24060001);
            UnpackBootConfig = K_EXTRACT_CALL(a-4);
            UnpackBootConfigCall = a-4;
        }
#else
        else if (data == 0x8FA40004){ // UnpackBootConfig
            if (_lw(addr+8) == 0x8FA50008) {
                UnpackBootConfigArg = addr;
                UnpackBootConfig = K_EXTRACT_CALL(addr+4);
                UnpackBootConfigCall = addr+4;
            }
            else if (_lw(addr+4) == 0x8FA50008) {
                UnpackBootConfigArg = addr;
                UnpackBootConfig = K_EXTRACT_CALL(addr+8);
                UnpackBootConfigCall = addr+8;
            }
        }
#endif
        else if ((data == _lw(addr+4)) && (data & 0xFC000000) == 0xAC000000){ // Patch ~PSP header check
            // Returns size of the buffer on loading whatever modules
            _sw(0xAFA50000, addr+4); // sw a1, 0(sp)
            _sw(0x20A30000, addr+8); // move v1, a1
        }
        else if (strcmp(END_BUF_STR, (char*)addr) == 0){
            reboot_end = (addr & -0x4); // found end of reboot buffer
            break;
        }
    }
    sceRebootIcacheInvalidateAll = Icache;
    sceRebootDacheWritebackInvalidateAll = Dcache;
    Icache();
    Dcache();
    return reboot_end;
}

static void checkRebootConfig(){
    if (IS_ARK_CONFIG(reboot_conf)){
        // fix MODE_NP9660 (Galaxy driver no longer exists, redirect to either inferno or normal)
        if (reboot_conf->iso_mode == MODE_NP9660){
            if (reboot_conf->iso_path[0] == 0){
                // no ISO -> normal mode
                reboot_conf->iso_mode = MODE_UMD;
            }
            else{
                // attempting to load an ISO with NP9660 is no longer possible, use inferno instead
                reboot_conf->iso_mode = MODE_INFERNO;
            }
        }
    }
}

extern void copyPSPVram(u32*);
#define REG32(addr)                 *((volatile uint32_t *)(addr))

// Entry Point
int _arkReboot(int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7)
{
    #ifdef DEBUG
    colorDebug(0xff00);
    #endif
    
#if defined(REBOOTEX) && defined(MS_IPL)
    // GPIO enable
    REG32(0xbc10007c) |= 0xc8;
    __asm("sync"::);
    
    syscon_init();
    
    syscon_ctrl_ms_power(1);
#endif

#ifdef PAYLOADEX
    u32 ctrl = _lw(BOOT_KEY_BUFFER);

    if ((ctrl & SYSCON_CTRL_HOME) == 0) {
        return sceReboot(arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }

    if ((ctrl & SYSCON_CTRL_RTRG) == 0) {
        _arkconf.recovery = 1;
    }

    memcpy(ark_config, &_arkconf, sizeof(ARKConfig));
#endif

    reboot_start = REBOOT_TEXT;
    reboot_end = findRebootFunctions(reboot_start); // scan for reboot functions
    
    // patch reboot buffer
    checkRebootConfig();
    patchRebootBuffer();
    
    // Forward Call
    return sceReboot(arg1, arg2, arg3, arg4, arg5, arg6, arg7);
}
