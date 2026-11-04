/*
 * ra1npoc - common.c
 *
 * Copyright (c) 2021 dora2ios
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
//#include <common/pongo_config.h>
#include <common/log.h>

#include <time.h>

struct checkm8_device_list {
    uint16_t        cpid;
    uint8_t         bdid;
    int     enterdfuType;
};

static struct checkm8_device_list devlists[] = {
    // 3GS
    { 0x8920, 0xff, DFU_LEGACY }, // legacy
    // iPT3
    { 0x8920, 0xff, DFU_LEGACY }, // legacy
    // Apple A4
    { 0x8930, 0xff, DFU_LEGACY }, // legacy
    // Apple A5
    { 0x8940, 0xff, DFU_LEGACY }, // legacy
    // Apple A5r
    { 0x8942, 0xff, DFU_LEGACY }, // legacy
    // Apple A5X
    { 0x8945, 0xff, DFU_LEGACY }, // legacy
    // Apple A6
    { 0x8950, 0xff, DFU_LEGACY }, // legacy
    // Apple A6X
    { 0x8955, 0xff, DFU_LEGACY }, // legacy
    // Apple A7
    { 0x8960, 0xff, DFU_LEGACY }, // all have legacy type
    // Apple A8
    { 0x7000, 0xff, DFU_LEGACY }, // all have legacy type
    // Apple A8X
    { 0x7001, 0xff, DFU_LEGACY }, // all have legacy type
    // Apple A9(S)
    { 0x8000, 0xff, DFU_LEGACY }, // all have legacy type
    // Apple A9(T)
    { 0x8003, 0xff, DFU_LEGACY }, // all have legacy type
    // Apple A9X
    { 0x8001, 0xff, DFU_LEGACY },
    // Apple A10 Fusion
    { 0x8010, 0x08, DFU_IPHONE7 }, // iPhone 7
    { 0x8010, 0x0c, DFU_IPHONE7 }, // iPhone 7
    { 0x8010, 0x0a, DFU_IPHONE7 }, // iPhone 7 Plus
    { 0x8010, 0x0e, DFU_IPHONE7 }, // iPhone 7 Plus
    { 0x8010, 0x16, DFU_IPHONE7 }, // iPod touch 7
    { 0x8010, 0x18, DFU_LEGACY  }, // iPad 6
    { 0x8010, 0x1a, DFU_LEGACY  }, // iPad 6
    { 0x8010, 0x1c, DFU_LEGACY  }, // iPad 7
    { 0x8010, 0x1e, DFU_LEGACY  }, // iPad 7
    // Apple A10X Fusion
    { 0x8011, 0xff, DFU_LEGACY }, // all have legacy type
    // Apple T2
    { 0x8012, 0xff, DFU_UNKOWN_TYPE }, // i don't know :/
    // Apple A11 Bionic
    { 0x8015, 0xff, DFU_IPHONEX }, // iPhone 8/X
    // err
    { 0xffff, 0xff, DFU_UNKOWN_TYPE }
};

#if defined(KPF_FLAGS_PTR) && defined(BOOTARGS_STR_PTR)
extern int8_t kpf_flags;
extern const char* bootargs;
extern bool special_pongo;
#endif

long cpuTime;

static void interval(double sec)
{
    long now;
    double n_sec;
    double b_sec = (double)cpuTime / CLOCKS_PER_SEC;
    while(1) {
        now = clock();
        n_sec = (double)now / CLOCKS_PER_SEC;
        if ((n_sec-b_sec) > sec)
            break;
    }
    cpuTime = now;
}

static void prog(int sec)
{
    int i=0;
    int j=0;
    
    cpuTime = clock();
    
    for(i=0; i<sec; i++) {
        printf("[");
        for (j=0;j<i+1;j++)
            printf("=");
        for (;j<sec;j++)
            printf(" ");
        printf("] (%d/%d sec)\r", i+1, sec);
        fflush(stdout);
        interval(1);
    }
    printf("\n");
}

transfer_t send_data(io_client_t client, unsigned char* buf, size_t size)
{
    return usb_ctrl_transfer(client, 0x21, 1, 0x0000, 0x0000, buf, size);
}

transfer_t send_data_with_time(io_client_t client, unsigned char* buf, size_t size, int timeout)
{
    return usb_ctrl_transfer_with_time(client, 0x21, 1, 0x0000, 0x0000, buf, size, timeout);
}

transfer_t get_status(io_client_t client, unsigned char* buf, size_t size)
{
    return usb_ctrl_transfer(client, 0xa1, 3, 0x0000, 0x0000, buf, size);
}

transfer_t send_abort(io_client_t client)
{
    return usb_ctrl_transfer_with_time(client, 0x21, 4, 0x0000, 0x0000, NULL, 0, 0);
}


int enter_dfu_via_recovery(io_client_t client)
{
    LOG("Waiting for device in Recovery mode...");
    while(1) {
        if(client) {
            io_close(client);
            client = NULL;
        }
        io_open(&client, DEVICE_RECOVERY_MODE_2, true);
        if(client) break;
    }
    if(!client) {
        ERROR("Failed to find the device in Recovery mode");
        return -1;
    }
    
    LOG("CONNECTED: Recovery mode");
    
    if(client->hasSerialStr == false){
        read_serial_number(client);
    }
    
    DEBUGLOG("CPID: 0x%02x, BDID: 0x%01x", client->devinfo.cpid, client->devinfo.bdid);
    
    int enterdfuType = DFU_UNKOWN_TYPE;
    for (int i=0; devlists[i].cpid != 0xffff; i++) {
        if (client->devinfo.cpid == devlists[i].cpid) {
            // check t8010: multiple types are mixed :/
            if (client->devinfo.cpid == 0x8010) {
                for(int j=i; devlists[j].bdid != 0xff; j++) {
                    if (client->devinfo.bdid == devlists[j].bdid) {
                        enterdfuType = devlists[i].enterdfuType;
                        break;
                    }
                }
            } else {
                enterdfuType = devlists[i].enterdfuType;
                break;
            }
        }
    }
    DEBUGLOG("enterdfuType: %x", enterdfuType);
    
    const char* btn = NULL;
    int step2_sec = 4;
    int step3_sec = 10;
    
    if (enterdfuType == DFU_LEGACY)
    { /* DFU_LEGACY */
        btn = "Home button";
    }
    else if (enterdfuType == DFU_IPHONE7 ||
             enterdfuType == DFU_IPHONEX)
    { /* DFU_IPHONE7, DFU_IPHONEX */
        btn = "Volume down button";
    }
    else
    { /* DFU_UNKOWN_TYPE */
        ERROR("This mode is not yet supported for this device.");
        return -1;
    } /* DFU_UNKOWN_TYPE */
    
    printf("\n");
    
    printf("================\n");
    printf("::\n");
    printf(":: %s\n", __FUNCTION__);
    printf(":: CPID: 0x%02x BDID: 0x%02x TYPE: 0x%02x\n", client->devinfo.cpid, client->devinfo.bdid, enterdfuType);
    printf("::\n");
    printf(":: \x1b[34mTime to put the device into DFU mode\x1b[39m\n");
    printf(":: \x1b[34mPlease follow the instructions below to operate the device.\x1b[39m\n");
    printf("::\n");
    printf(":: \x1b[32mSTEP1 Press \x1b[31m<enter>\x1b[32m key.\x1b[39m\n");
    printf(":: \x1b[32mSTEP2 Press and hold Side(or Top) and %ss together \x1b[31m(%dsec)\x1b[39m\n", btn, step2_sec);
    printf(":: \x1b[32mSTEP3 Release Side(or Top) button But keep holding %s \x1b[31m(%dsec)\x1b[39m\n", btn, step3_sec);
    printf("================\n");
    printf("\n");
    printf("\x1b[34mready? it starts 3 seconds after press <enter> key.\x1b[39m\n");
    printf("\x1b[32m[STEP1] Press <enter> key\x1b[39m >> ");
    getchar();
    printf("\n");
    // initialization for interval()
    cpuTime = clock();
    
    for(int i=0; i<3; i++) {
        printf("preparing... (STEP2 will start after %d seconds)\r", 3-i);
        fflush(stdout);
        interval(1);
    }
    
    LOG_NOFUNC("[STEP2] Press and hold Side(or Top) and %ss together (%dsec)", btn, step2_sec);
    int j=0;
    for(int i=0; i<step2_sec; i++) {
        if(i==1)
            send_reboot_via_recovery(client);
        printf("[");
        for (j=0;j<i+1;j++)
            printf("=");
        for (;j<step2_sec;j++)
            printf(" ");
        printf("] (%d/%d sec)\r", i+1, step2_sec);
        fflush(stdout);
        interval(1);
    }
    printf("\n");
    
    LOG_NOFUNC("[STEP3] Release Side(or Top) button But keep holding %s (%dsec)", btn, step3_sec);
    prog(step3_sec);
    
    LOG("reconnecting");
    io_reconnect(&client, 5, DEVICE_DFU, USB_NO_RESET, false, 10000);
    if(!client) {
        ERROR("Failed to put the device into DFU mode");
        return -1;
    }
    
    LOG("DONE!");
    io_close(client);
    client = NULL;
    return 0;
}

int payload_stage2(io_client_t client, checkra1n_payload_t payload)
{
    transfer_t result;
    
    {
        size_t len = 0;
        size_t size;
        while(len < payload.stage2_len) {
            size = ((payload.stage2_len - len) > DFU_MAX_TRANSFER_SZ) ? DFU_MAX_TRANSFER_SZ : (payload.stage2_len - len);
            result = send_data(client, (unsigned char*)&payload.stage2[len], size);
            if(result.wLenDone != size || result.ret != kIOReturnSuccess) {
                ERROR("Failed to send stage2 [%x, %x]", result.ret, (unsigned int)result.wLenDone);
                return -1;
            }
            len += size;
        }
    }
    DEBUGLOG("(1/2) %x", result.ret);
    usleep(1000);
    
    result = send_abort(client);
    DEBUGLOG("(2/2) %x", result.ret);
    usleep(1000);
    
    return 0;
}

int pongo(io_client_t client, checkra1n_payload_t payload)
{
    transfer_t result;
    
    unsigned char blank[8];
    memset(&blank, '\0', 8);
    
    result = usb_ctrl_transfer(client, 0x40, 64, 0x03e8, 0x01f4, NULL, 0); // ?
    DEBUGLOG("(1/6) %x", result.ret);
    
#if defined(KPF_FLAGS_PTR) && defined(BOOTARGS_STR_PTR)
    
    uint32_t bootargs_offset = BOOTARGS_STR_PTR;
    uint32_t kpf_flags_offset = KPF_FLAGS_PTR;
    
    if(special_pongo == true) {
        bootargs_offset -= SPECIAL_HAXX;
        kpf_flags_offset -= SPECIAL_HAXX;
    }
    
    const char* newArgs = bootargs; // set xnu boot-arg commandline
    size_t newArgsLen = strlen(newArgs) + 1;
    size_t newArgBufLen = (newArgsLen + 3) / 4 * 4;
    char bootArgsBuf[newArgBufLen];
    strlcpy(bootArgsBuf, newArgs, newArgBufLen);
    memset(bootArgsBuf + newArgsLen, 0, newArgBufLen - newArgsLen);
    memcpy(payload.pongoOS+bootargs_offset, bootArgsBuf, newArgBufLen);
    
    *(int8_t*)(payload.pongoOS+kpf_flags_offset) = kpf_flags;
#endif
    
    {
        size_t len = 0;
        size_t size;
        while(len < payload.pongoOS_len) {
            size = ((payload.pongoOS_len - len) > DFU_MAX_TRANSFER_SZ) ? DFU_MAX_TRANSFER_SZ : (payload.pongoOS_len - len);
            result = send_data(client, (unsigned char*)&payload.pongoOS[len], size);
            if(result.wLenDone != size || result.ret != kIOReturnSuccess) {
                ERROR("Failed to send pongoOS [%x, %x]", result.ret, (unsigned int)result.wLenDone);
                return -1;
            }
            len += size;
        }
    }
    
    DEBUGLOG("(2/6) %x", result.ret);
    usleep(10000);
    
    result = send_data(client, NULL, 0);
    DEBUGLOG("(3/6) %x", result.ret);
    
    result = get_status(client, blank, 8);
    DEBUGLOG("(4/6) %x", result.ret);

    result = get_status(client, blank, 8);
    DEBUGLOG("(5/6) %x", result.ret);

    result = get_status(client, blank, 8);
    DEBUGLOG("(6/6) %x", result.ret);
    usleep(10000);
    
    return 0;
}

int connect_to_stage2(io_client_t client, checkra1n_payload_t payload)
{
    LOG("reconnecting");
    io_reconnect(&client, 15, DEVICE_STAGE2, USB_RESET|USB_REENUMERATE, false, 5000000);
    if(!client) {
        ERROR("Failed to connect to checkra1n DFU");
        return -1;
    }
    LOG("connected to Stage2");
    
    usleep(10000);
    
    LOG("sending pongoOS");
    if(pongo(client, payload)) {
        return -1;
    }
    
    io_reset(client, USB_RESET|USB_REENUMERATE);
    io_close(client);
    
    LOG("BOOTED?");
    
    return 0;
}
