/*
 * ra1npoc - main.c
 *
 * Copyright (c) 2021 - 2022 dora2ios
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <io/iousb.h>

#include <common/common.h>
#include <common/pongo_config.h>
#include <common/list.h>
#include <common/log.h>

#include <exploit/checkm8_arm64.h>

#include <getopt.h>

#include "payload/pongoOS.h"

#if defined(S5L8960_PAYLOAD)
#include "payload/s5l8960.h"
#endif /* S5L8960_PAYLOAD */

#if defined(T7000_PAYLOAD)
#include "payload/t7000.h"
#endif /* T7000_PAYLOAD */

#if defined(T7001_PAYLOAD)
#include "payload/t7001.h"
#endif /* T7001_PAYLOAD */

#if (defined(S8000_PAYLOAD) || defined(S8003_PAYLOAD))
#include "payload/s8000.h"
#endif /* S8000_PAYLOAD */

#if defined(S8001_PAYLOAD)
#include "payload/s8001.h"
unsigned char special_pongoOS[MAX_HAXX_SIZE];
#endif /* S8001_PAYLOAD */

#if defined(T8010_PAYLOAD)
#include "payload/t8010.h"
#endif /* T8010_PAYLOAD */

#if defined(T8011_PAYLOAD)
#include "payload/t8011.h"
#endif /* T8011_PAYLOAD */

#if defined(T8012_PAYLOAD)
#include "payload/t8012.h"
#endif /* T8012_PAYLOAD */

#if defined(T8015_PAYLOAD)
#include "payload/t8015.h"
#endif /* T8015_PAYLOAD */

#if defined(KPF_FLAGS_PTR) && defined(BOOTARGS_STR_PTR)
/* tr4mpass E27: default to stopping at the pongo shell with rdsk+KPF
 * loaded, instead of auto-booting.  kpf_autoboot() honors
 * checkrain_option_pongo_shell ("Pongo shell requested, stopping
 * here!") -- this is what keeps pongoOS alive for the pipeline. */
int8_t kpf_flags = checkrain_option_pongo_shell;
const char* bootargs = NULL;
#endif

io_client_t client;
checkra1n_payload_t payload;
bool special_pongo = false;
bool debug_enabled = false;


static void list(void)
{
    printf("Devices list:\n");
#if defined(S5L8960_CODE) && defined(S5L8960_PAYLOAD)
    printf("\t\x1b[36ms5l8960x\x1b[39m - \x1b[35mApple A7\x1b[39m\n");
#endif /* S5L8960 */
#if defined(T7000_CODE) && defined(T7000_PAYLOAD)
    printf("\t\x1b[36mt7000   \x1b[39m - \x1b[35mApple A8\x1b[39m\n");
#endif /* T7000 */
#if defined(T7001_CODE) && defined(T7001_PAYLOAD)
    printf("\t\x1b[36mt7001   \x1b[39m - \x1b[35mApple A8X\x1b[39m\n");
#endif /* T7001 */
#if defined(S8000_CODE) && defined(S8000_PAYLOAD)
    printf("\t\x1b[36ms8000   \x1b[39m - \x1b[35mApple A9 (Samsung)\x1b[39m\n");
#endif /* S8000 */
#if defined(S8003_CODE) && defined(S8003_PAYLOAD)
    printf("\t\x1b[36ms8003   \x1b[39m - \x1b[35mApple A9 (TSMC)\x1b[39m\n");
#endif /* S8003 */
#if defined(S8001_CODE) && defined(S8001_PAYLOAD)
    printf("\t\x1b[36ms8001   \x1b[39m - \x1b[35mApple A9X\x1b[39m\n");
#endif /* S8001 */
#if defined(T8010_CODE) && defined(T8010_PAYLOAD)
    printf("\t\x1b[36mt8010   \x1b[39m - \x1b[35mApple A10 Fusion\x1b[39m\n");
#endif /* T8010 */
#if defined(T8011_CODE) && defined(T8011_PAYLOAD)
    printf("\t\x1b[36mt8011   \x1b[39m - \x1b[35mApple A10X Fusion\x1b[39m\n");
#endif /* T8011 */
#if defined(T8012_CODE) && defined(T8012_PAYLOAD)
    printf("\t\x1b[36mt8012   \x1b[39m - \x1b[35mApple T2\x1b[39m\n");
#endif /* T8012 */
#if defined(T8015_CODE) && defined(T8015_PAYLOAD)
    printf("\t\x1b[36mt8015   \x1b[39m - \x1b[35mApple A11 Bionic\x1b[39m\n");
#endif /* T8015 */
}

static void usage(char** argv)
{
    printf("Usage: %s [option]\n", argv[0]);
    
    printf("  -h, --help\t\t\t\x1b[36mshow usage\x1b[39m\n");
    printf("  -l, --list\t\t\t\x1b[36mshow list of supported devices\x1b[39m\n");
    printf("  -v, --verbose\t\t\t\x1b[36menable verbose boot\x1b[39m\n");
    printf("  -c, --cleandfu\t\t\x1b[36muse cleandfu [BETA]\x1b[39m\n");
    printf("  -d, --debug\t\t\t\x1b[36menable debug log\x1b[39m\n");
    printf("  -e, --extra-bootargs <args>\t\x1b[36mset extra bootargs\x1b[39m\n");
    printf("  -s, --special\t\t\t\x1b[36muse special pongo_2.5.0-0cb6126f\x1b[39m\n");
    printf("  -m, --m1usbc\t\t\t\x1b[36muse usb-c on apple silicon macs\x1b[39m\n");
    
    printf("\n");
}

int main(int argc, char** argv)
{
    int ret = 0;
    
    bool useRecovery = false;
    bool verboseBoot = false;
    bool useAppleSilicon = false;
    char* extraBootArgs = NULL;
    
    memset(&payload, '\0', sizeof(checkra1n_payload_t));
    LOG_NOFUNC("* checkRAIN clone v2.1.4 for iOS by interception");
    LOG_NOFUNC("[BUILTIN] v0.12.4");
    //LOG_NOFUNC("[COMMIT] %s", "");
    
    int opt = 0;
    static struct option longopts[] = {
        { "help",           no_argument,       NULL, 'h' },
        { "list",           no_argument,       NULL, 'l' },
        { "verbose",        no_argument,       NULL, 'v' },
        { "cleandfu",       no_argument,       NULL, 'c' },
        { "debug",          no_argument,       NULL, 'd' },
        { "extra-bootargs", required_argument, NULL, 'e' },
        { "special",        no_argument,       NULL, 's' },
        { "m1usbc",         no_argument,       NULL, 'm' },
        { NULL, 0, NULL, 0 }
    };
    
    while ((opt = getopt_long(argc, argv, "hlvdce:sm", longopts, NULL)) > 0) {
        switch (opt) {
            case 'h':
                usage(argv);
                return 0;
                
            case 'l':
                list();
                return 0;
                
            case 'v':
                verboseBoot = true;
                break;
                
            case 'd':
                debug_enabled = true;
                DEBUGLOG("enabled: debug log");
                break;
                
            case 'c':
                useRecovery = true;
                break;
                
            case 'e':
                if (optarg) {
                    extraBootArgs = strdup(optarg);
                    LOG("extraBootArgs: [%s]", extraBootArgs);
                }
                break;
                
            case 's':
                special_pongo = true;
                break;
                
            case 'm':
                useAppleSilicon = true;
                break;
                
            default:
                usage(argv);
                return -1;
        }
    }
    
    if(useRecovery) {
        if(enter_dfu_via_recovery(client)) {
            return -1;
        }
    }
    
    LOG("Waiting for device in DFU mode...");
    while(get_device(DEVICE_DFU, true)) {
        sleep(1);
    }
    
    LOG("CONNECTED: DFU mode");
    sleep(2);
    
    if(client->hasSerialStr == false){
        read_serial_number(client); // For iOS 10 and lower
    }
    
    DEBUGLOG("CPID: 0x%02x, STRG: [%s]", client->devinfo.cpid, client->devinfo.srtg);
    
    if(client->hasSerialStr != true){
        ERROR("serial number was not found!");
        return -1;
    }
    
    switch(client->devinfo.cpid) {
            
#if defined(S5L8960_CODE) && defined(S5L8960_PAYLOAD)
        case 0x8960:
            payload.overwrite_len = s5l8960_overwrite_len;
            payload.stage1_len = s5l8960_stage1_len;
            payload.stage2_len = s5l8960_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = s5l8960_overwrite;
            payload.stage1 = s5l8960_stage1;
            payload.stage2 = s5l8960_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* S5L8960 */
            
#if defined(T7000_CODE) && defined(T7000_PAYLOAD)
        case 0x7000:
            payload.overwrite_len = 0;
            payload.stage1_len = t7000_stage1_len;
            payload.stage2_len = t7000_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = NULL;
            payload.stage1 = t7000_stage1;
            payload.stage2 = t7000_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* T7000 */
 
#if defined(T7001_CODE) && defined(T7001_PAYLOAD)
        case 0x7001:
            payload.overwrite_len = 0;
            payload.stage1_len = t7001_stage1_len;
            payload.stage2_len = t7001_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = NULL;
            payload.stage1 = t7001_stage1;
            payload.stage2 = t7001_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* T7001 */
            
#if defined(S8000_CODE) && defined(S8000_PAYLOAD)
        case 0x8000:
            payload.overwrite_len = 0;
            payload.stage1_len = s8000_stage1_len;
            payload.stage2_len = s8000_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = NULL;
            payload.stage1 = s8000_stage1;
            payload.stage2 = s8000_stage2;
            payload.pongoOS = pongoOS;
            /* tr4mpass E27/E28: the s8000 full blob auto-boots (stage2
             * carries checkra1n's autoboot; kerninfo pongo_shell flag
             * at KPF_FLAGS_PTR does NOT stop it -- observed E27: pongo
             * ran "recfg done, rebooting" straight to XNU).  Use the
             * proven S8001 approach instead: upload pongoOS only
             * (0x3F2F8 = exactly pongoOS.bin), keep the shell alive,
             * and let pongoterm upload rdsk/kpf manually. */
            payload.pongoOS_len = 0x3F2F8;
            client->devinfo.checkm8_flag |= NO_AUTOBOOT;
            break;
#endif /* S8000 */
            
#if defined(S8003_CODE) && defined(S8003_PAYLOAD)
        case 0x8003:
            payload.overwrite_len = 0;
            payload.stage1_len = s8000_stage1_len;
            payload.stage2_len = s8000_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = NULL;
            payload.stage1 = s8000_stage1;
            payload.stage2 = s8000_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* S8003 */
            
#if defined(S8001_CODE) && defined(S8001_PAYLOAD)
        case 0x8001:
            payload.overwrite_len = s8001_overwrite_len;
            payload.stage1_len = s8001_stage1_len;
            payload.stage2_len = s8001_stage2_len;
            payload.overwrite = s8001_overwrite;
            payload.stage1 = s8001_stage1;
            payload.stage2 = s8001_stage2;
            if(special_pongo == true) {
                size_t pongo_size = 0;
                
                memset(&special_pongoOS, '\0', MAX_HAXX_SIZE);
                
                // 1, copy pongo
                if(pongo_2_5_0_0cb6126f_bin_len > MAX_HAXX_SIZE) {
                    ERROR("overflow: %s", "pongo.bin");
                    return -1;
                }
                memcpy(special_pongoOS, pongo_2_5_0_0cb6126f_bin, pongo_2_5_0_0cb6126f_bin_len);
                pongo_size += pongo_2_5_0_0cb6126f_bin_len;
                
                // 2, add auto-boot mark
                unsigned char auto_boot_mark[] =
                {
                    0x61, 0x75, 0x74, 0x6f,
                    0x62, 0x6f, 0x6f, 0x74,
                    0x00, 0x00, 0x20, 0x00,
                    0x00, 0x00, 0x00, 0x00
                };
                size_t auto_boot_mark_size = 0x16;
                
                if(pongo_size + auto_boot_mark_size > MAX_HAXX_SIZE) {
                    ERROR("overflow: %s", "auto_boot_mark");
                    return -1;
                }
                memcpy(special_pongoOS+pongo_size, auto_boot_mark, 16);
                pongo_size += 16;
                
                // 3, copy kpf
                if(pongo_size + KPF_SIZE > MAX_HAXX_SIZE) {
                    ERROR("overflow: %s", "kpf");
                    return -1;
                }
                memcpy(special_pongoOS+pongo_size, pongoOS+KPF_LOCATION, KPF_SIZE);
                pongo_size += KPF_SIZE;
                
                // 4, add RDSK mark
                unsigned char RDSK_mark[] =
                {
                    0x52, 0x44, 0x53, 0x4B,
                    0x52, 0x44, 0x53, 0x4B,
                    0x00, 0x00, 0x10, 0x00
                };
                size_t RDSK_mark_size = 0xc;
                
                pongo_size = RDSK_LOCATION - SPECIAL_HAXX - RDSK_mark_size;
                if(pongo_size + RDSK_mark_size > MAX_HAXX_SIZE) {
                    ERROR("overflow: %s", "rdsk_mark");
                    return -1;
                }
                memcpy(special_pongoOS+pongo_size, RDSK_mark, RDSK_mark_size);
                pongo_size += RDSK_mark_size;
                
                // 5, copy rdsk
                if(pongo_size + RDSK_SIZE > MAX_HAXX_SIZE) {
                    ERROR("overflow: %s", "rdsk");
                    return -1;
                }
                memcpy(special_pongoOS+pongo_size, pongoOS+RDSK_LOCATION, RDSK_SIZE);
                pongo_size += RDSK_SIZE;
                
                // 6, set flags etc...?
                if(pongo_size + BLANK_SIZE > MAX_HAXX_SIZE) {
                    ERROR("overflow: %s", "BLANK_SIZE");
                    return -1;
                }
                memcpy(special_pongoOS+pongo_size, pongoOS+RDSK_LOCATION+RDSK_SIZE, BLANK_SIZE);
                pongo_size += BLANK_SIZE;
                
                DEBUGLOG("use: special pongoOS");
                payload.pongoOS = special_pongoOS;
                payload.pongoOS_len = pongo_size;
                
            } else {
                payload.pongoOS = pongoOS;
                payload.pongoOS_len = 0x3F2F8; // pongoOS_len
                client->devinfo.checkm8_flag |= NO_AUTOBOOT;
            }
            break;
#endif /* S8001 */
            
#if defined(T8010_CODE) && defined(T8010_PAYLOAD)
        case 0x8010:
            payload.overwrite_len = t8010_overwrite_len;
            payload.stage1_len = t8010_stage1_len;
            payload.stage2_len = t8010_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = t8010_overwrite;
            payload.stage1 = t8010_stage1;
            payload.stage2 = t8010_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* T8010 */
            
#if defined(T8011_CODE) && defined(T8011_PAYLOAD)
        case 0x8011:
            payload.overwrite_len = t8011_overwrite_len;
            payload.stage1_len = t8011_stage1_len;
            payload.stage2_len = t8011_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = t8011_overwrite;
            payload.stage1 = t8011_stage1;
            payload.stage2 = t8011_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* T8011 */
            
#if defined(T8012_CODE) && defined(T8012_PAYLOAD)
        case 0x8012:
            payload.overwrite_len = t8012_overwrite_len;
            payload.stage1_len = t8012_stage1_len;
            payload.stage2_len = t8012_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = t8012_overwrite;
            payload.stage1 = t8012_stage1;
            payload.stage2 = t8012_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* T8012 */
            
#if defined(T8015_CODE) && defined(T8015_PAYLOAD)
        case 0x8015:
            payload.overwrite_len = t8015_overwrite_len;
            payload.stage1_len = t8015_stage1_len;
            payload.stage2_len = t8015_stage2_len;
            payload.pongoOS_len = pongoOS_len;
            payload.overwrite = t8015_overwrite;
            payload.stage1 = t8015_stage1;
            payload.stage2 = t8015_stage2;
            payload.pongoOS = pongoOS;
            break;
#endif /* T8015 */
            
        default:
            ERROR("This device is not supported!");
            return -1;
    }
    
#if defined(KPF_FLAGS_PTR) && defined(BOOTARGS_STR_PTR)
    char str[MAX_BOOTARGS_LEN];
    memset(&str, 0x0, MAX_BOOTARGS_LEN);
    
    if(strlen(DEFAULT_BOOTARGS) > MAX_BOOTARGS_LEN) {
        ERROR("DEFAULT_BOOTARGS is too large!");
        return -1;
    }
    sprintf(str, "%s", DEFAULT_BOOTARGS);
    
    if(verboseBoot == true) {
        if((strlen(str) + strlen(" -v")) > MAX_BOOTARGS_LEN) {
            ERROR("bootArgs is too large!");
            return -1;
        }
        sprintf(str, "%s -v", str);
        
        checkrain_set_option(kpf_flags, checkrain_option_verbose_boot, 1);
        DEBUGLOG("kpf_flags: %x", kpf_flags);
        LOG("enable: verbose boot");
    }
    if(extraBootArgs != NULL) {
        if((strlen(str) + strlen(extraBootArgs)) > MAX_BOOTARGS_LEN) {
            ERROR("bootArgs is too large!");
            return -1;
        }
        sprintf(str, "%s %s", str, extraBootArgs);
    }
    bootargs = str;
    LOG("bootArgs: %s", bootargs);
#endif
    
    int flags = client->devinfo.checkm8_flag; // because checkm8_flag(s) will be lost
    
    if(useAppleSilicon)
        flags |= APPLE_M1_WITH_USB_C;
    
    ret = checkm8_arm64(client, payload, flags);
    
    if((ret == 0) && (flags & NO_AUTOBOOT))
        LOG("note: probably pongoOS booted, but there is still work to be done.\nYou have to sending rdsk and kpf via pongoterm.");
    
    return ret;
}

