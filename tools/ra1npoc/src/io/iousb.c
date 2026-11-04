#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>

#include <io/iousb.h>
#include <common/log.h>

extern io_client_t client;

static const char *darwin_device_class = kIOUSBDeviceClassName;

static int nsleep(long nanoseconds)
{
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = nanoseconds;
    return nanosleep(&req, &rem);
}

static void io_async_cb(void *refcon, IOReturn ret, void *arg_0)
{
    async_transfer_t* transfer = refcon;
    
    if(transfer != NULL) {
        transfer->ret = ret;
        memcpy(&transfer->wLenDone, &arg_0, sizeof(transfer->wLenDone));
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
}

static void CFDictionarySet16(CFMutableDictionaryRef dict, const void *key, SInt16 value)
{
    CFNumberRef numberRef;
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &value);
    if (numberRef) {
        CFDictionarySetValue(dict, key, numberRef);
        CFRelease(numberRef);
    }
}

static io_iterator_t io_get_iterator_for_pid(uint16_t pid)
{
    IOReturn result;
    io_iterator_t iterator;
    CFMutableDictionaryRef matchingDict;
    
#ifdef IPHONEOS_ARM
    // Allows iOS to connect to iOS devices. // iOS 9.0 or high
    darwin_device_class = "IOUSBHostDevice";
#endif
    
    matchingDict = IOServiceMatching(darwin_device_class);
    CFDictionarySet16(matchingDict, CFSTR(kUSBVendorID), kAppleVendorID);
    CFDictionarySet16(matchingDict, CFSTR(kUSBProductID), pid);
    
    result = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDict, &iterator);
    if (result != kIOReturnSuccess){
        return IO_OBJECT_NULL;
    }
    
    return iterator;
}

static IOReturn io_create_plugin_interface(io_client_t client, io_service_t service)
{
    IOReturn result;
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score;
    
    result = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    if(result == kIOReturnSuccess){
        result = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320), (LPVOID *)&(client->handle));
        IODestroyPlugInInterface(plugin);
        if(result == kIOReturnSuccess){
            result = (*client->handle)->USBDeviceOpen(client->handle);
            if (result == kIOReturnSuccess) {
                result = (*client->handle)->SetConfiguration(client->handle, 1);
                if (result == kIOReturnSuccess) {
                    result = (*client->handle)->CreateDeviceAsyncEventSource(client->handle, &client->async_event_source);
                    if (result == kIOReturnSuccess) {
                        CFRunLoopAddSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
                    }
                }
            } else {
                (*client->handle)->Release(client->handle);
            }
        }
    }
    IOObjectRelease(service);
    
    return result;
}

static void load_devinfo(io_client_t client, const char* str)
{
    if (!client || !str) {
        return;
    }
    
    char* ptr;
    char tmp[256];
    
    memset(&client->devinfo, '\0', sizeof(struct io_devinfo));
    memset(&tmp, '\0', 256);
    
    ptr = strstr(str, "CPID:");
    if (ptr != NULL) {
        sscanf(ptr, "CPID:%x", &client->devinfo.cpid);
    }
    
    ptr = strstr(str, "BDID:");
    if (ptr != NULL) {
        sscanf(ptr, "BDID:%x", &client->devinfo.bdid);
    }
    
    ptr = strstr(str, "CPFM:");
    if (ptr != NULL) {
        sscanf(ptr, "CPFM:%x", &client->devinfo.cpfm);
    }
    
    ptr = strstr(str, "SRNM:[");
    if(ptr != NULL) {
        client->devinfo.hasSrnm = TRUE;
    } else {
        client->devinfo.hasSrnm = FALSE;
    }
    
    memset(&tmp, '\0', 256);
    ptr = strstr(str, "PWND:[");
    if(ptr != NULL) {
        client->devinfo.hasPwnd = TRUE;
        
        sscanf(ptr, "PWND:[%s]", tmp);
        ptr = strrchr(tmp, ']');
        if(ptr != NULL) {
            *ptr = '\0';
        }
        client->devinfo.pwnstr = strdup(tmp);
    
    } else {
        client->devinfo.hasPwnd = FALSE;
    }
    
    memset(&tmp, '\0', 256);
    ptr = strstr(str, "SRTG:[");
    if(ptr != NULL) {
        sscanf(ptr, "SRTG:[%s]", tmp);
        ptr = strrchr(tmp, ']');
        if(ptr != NULL) {
            *ptr = '\0';
        }
        client->devinfo.srtg = strdup(tmp);
    }
    
    client->devinfo.checkm8_flag = NO_CHECKM8;
    switch(client->devinfo.cpid) {
        case 0x8950:
            client->devinfo.checkm8_flag |= CHECKM8_A6;
            break;
        case 0x8955:
            client->devinfo.checkm8_flag |= CHECKM8_A6;
            break;
        case 0x8960:
            client->devinfo.checkm8_flag |= CHECKM8_A7;
            break;
        case 0x7000:
            client->devinfo.checkm8_flag |= CHECKM8_A8_A9;
            break;
        case 0x7001:
            client->devinfo.checkm8_flag |= CHECKM8_A8_A9;
            break;
        case 0x8000:
            client->devinfo.checkm8_flag |= CHECKM8_A8_A9;
            break;
        case 0x8003:
            client->devinfo.checkm8_flag |= CHECKM8_A8_A9;
            break;
        case 0x8001:
            client->devinfo.checkm8_flag |= CHECKM8_A9X_A10X;
            break;
        case 0x8010:
            client->devinfo.checkm8_flag |= CHECKM8_A9X_A10X;
            break;
        case 0x8011:
            client->devinfo.checkm8_flag |= CHECKM8_A9X_A10X;
            break;
        case 0x8012:
            client->devinfo.checkm8_flag |= CHECKM8_A11; // ??
            break;
        case 0x8015:
            client->devinfo.checkm8_flag |= CHECKM8_A11;
            break;
        default:
            break;
    }
}

static void io_get_serial(io_client_t client, io_service_t service)
{
    CFStringRef serialstr;
    
    char serial_str[256];
    memset(&serial_str, '\0', 256);
    
    client->hasSerialStr = false;
    
    serialstr = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, kNilOptions);
    if(serialstr){
        CFStringGetCString(serialstr, serial_str, sizeof(serial_str), kCFStringEncodingUTF8);
        CFRelease(serialstr);
        load_devinfo(client, serial_str);
        client->hasSerialStr = true;
        DEBUGLOG("Found serial number!");
    }
    
}

void send_reboot_via_recovery(io_client_t client)
{
    const char* io_setenv_cmd = "setenv auto-boot true\x00";
    const char* io_saveenv_cmd = "saveenv\x00";
    const char* io_reboot_cmd = "reboot\x00";
    usb_ctrl_transfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char*)io_setenv_cmd, strlen(io_setenv_cmd)+1);
    usb_ctrl_transfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char*)io_saveenv_cmd, strlen(io_saveenv_cmd)+1);
    usb_ctrl_transfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char*)io_reboot_cmd, strlen(io_reboot_cmd)+1);
    
}
// iOS 10 and below, it is not possible to get the serial by IORegistryEntryCreateCFProperty, so send a usb request to get the serial.
// If it successful, set hasSerialStr in the structure io_devinfo in the io_client_t structure to true.
void read_serial_number(io_client_t client)
{
    transfer_t result;
    uint8_t size;
    
    unsigned char buf[0x100];
    unsigned char str[0x100];
    
    if(client->devinfo.srtg == NULL){
        memset(&buf, '\0', 0x100);
        memset(&str, '\0', 0x100);
        result = usb_ctrl_transfer(client, 0x80, 6, 0x0306, 0x040a, buf, 0x100); // 8950 or up (PWND)
        if(result.ret != kIOReturnSuccess)
            return;
        size = *(uint8_t*)buf;
        for(int i=0;i<(size/2);i++){
            str[i] = *(uint8_t*)(buf+2+(i*2));
        }
        load_devinfo(client, (const char*)str);
    }
    
    if(client->devinfo.srtg == NULL){
        memset(&buf, '\0', 0x100);
        memset(&str, '\0', 0x100);
        result = usb_ctrl_transfer(client, 0x80, 6, 0x0304, 0x040a, buf, 0x100); // 8950 or up
        if(result.ret != kIOReturnSuccess)
            return;
        size = *(uint8_t*)buf;
        for(int i=0;i<(size/2);i++){
            str[i] = *(uint8_t*)(buf+2+(i*2));
        }
        load_devinfo(client, (const char*)str);
    }
    
    if(client->devinfo.srtg == NULL){
        memset(&buf, '\0', 0x100);
        memset(&str, '\0', 0x100);
        result = usb_ctrl_transfer(client, 0x80, 6, 0x0303, 0x040a, buf, 0x100); // 8930
        if(result.ret != kIOReturnSuccess)
            return;
        size = *(uint8_t*)buf;
        for(int i=0;i<(size/2);i++){
            str[i] = *(uint8_t*)(buf+2+(i*2));
        }
        load_devinfo(client, (const char*)str);
    }
    
    if(client->devinfo.srtg != NULL){
        client->hasSerialStr = true;
        DEBUGLOG("Found serial number!");
    }
}

IOReturn io_reenumerate(io_client_t client)
{
    if (client == NULL || client->handle == NULL){
        return kIOReturnError;
    }
    return (*client->handle)->USBDeviceReEnumerate(client->handle, 0);
}

IOReturn io_resetdevice(io_client_t client)
{
    if (client == NULL || client->handle == NULL){
        return kIOReturnError;
    }
    return (*client->handle)->ResetDevice(client->handle);
}

void io_close(io_client_t client)
{
    if (client->handle) {
        (*client->handle)->USBDeviceClose(client->handle);
        (*client->handle)->Release(client->handle);
        client->handle = NULL;
    }
    if(client->async_event_source) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
        CFRelease(client->async_event_source);
    }
    free(client);
    client = NULL;
}

void io_reset(io_client_t client, int flags)
{
    if(flags & USB_REPLUG)
    {
        printf("\x1b[32m Please disconnect and reconnect the lightning cable now\n");
        printf("\x1b[32m After that, press <Enter> key >> ");
        getchar();
        printf("\n");
        // skip
        return;
    }
    
    IOReturn result;
    if(flags & USB_RESET) {
        result = io_resetdevice(client);
        DEBUGLOG("ResetDevice: %x", result);
    }
    
    if(flags & USB_REENUMERATE) {
        result = io_reenumerate(client);
        DEBUGLOG("USBDeviceReEnumerate: %x", result);
    }
}

int io_open(io_client_t *pclient, uint16_t pid, bool srnm)
{
    io_service_t service = IO_OBJECT_NULL;
    io_iterator_t iterator;
    UInt16 mode;
    UInt32 locationID;
    io_client_t _client;
    
    client = NULL;
    iterator = io_get_iterator_for_pid(pid);
    
    if (iterator != IO_OBJECT_NULL) {
        while((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
            break;
        }
        IOObjectRelease(iterator);
        
        _client = (io_client_t) calloc(1, sizeof(struct io_client_p));
        if(srnm == true){
            io_get_serial(_client, service);
        }
        
        if(io_create_plugin_interface(_client, service) == kIOReturnSuccess) {
            (*_client->handle)->GetDeviceProduct(_client->handle, &mode);
            (*_client->handle)->GetLocationID(_client->handle, &locationID);
            _client->mode = mode;
            *pclient = _client;
            return 0;
        }
        free(_client);
    }
    
    return -1;
}

int get_device(unsigned int mode, bool srnm)
{
    if(client) {
        io_close(client);
        client = NULL;
    }
    
    io_open(&client, mode, srnm);
    if(!client) {
        return -1;
    }

    return 0;
}

int get_device_time_stage(io_client_t *pclient, unsigned int time, uint16_t stage, bool srnm)
{
    for(int i=0; i<time; i++){
        if(*pclient) {
            io_close(*pclient);
            *pclient = NULL;
        }
        if (io_open(pclient, stage, srnm) == 0) {
            return 0;
        }
        //usleep(100000);
        //usleep(250000);
        sleep(1);
    }
    return -1;
}

int io_reconnect(io_client_t *pclient,
                 int retry,
                 uint16_t stage,
                 int flags,
                 bool srnm,
                 unsigned long sec)
{
    
    if(*pclient)
    {
        io_reset(*pclient, flags);
        io_close(*pclient);
        *pclient = NULL;
    }
    
    usleep(sec);
    
    if(get_device_time_stage(pclient, retry, stage, srnm) != 0) {
        *pclient = NULL;
        return -1;
    }
    
    if(!*pclient) {
        *pclient = NULL;
        return -1;
    }
    
    return 0;
}


// no timeout
transfer_t usb_ctrl_transfer(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length)
{
    transfer_t result;
    IOUSBDevRequest req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    
    result.ret = (*client->handle)->DeviceRequest(client->handle, &req);
    result.wLenDone = req.wLenDone;
    
    return result;
}

// with timeout
transfer_t usb_ctrl_transfer_with_time(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int time)
{
    transfer_t result;
    IOUSBDevRequestTO req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    req.noDataTimeout     = time;
    req.completionTimeout = time;

    result.ret = (*client->handle)->DeviceRequestTO(client->handle, &req);
    result.wLenDone = req.wLenDone;
    
    return result;
}

IOReturn io_abort_pipe_zero(io_client_t client)
{
    return (*client->handle)->USBDeviceAbortPipeZero(client->handle);
}

transfer_t async_usb_ctrl_transfer(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, async_transfer_t* transfer)
{
    transfer_t result;
    IOUSBDevRequest req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    
    result.ret = (*client->handle)->DeviceRequestAsync(client->handle, &req, io_async_cb, transfer);
    result.wLenDone = req.wLenDone;
    
    return result;
}

UInt32 async_usb_ctrl_transfer_with_cancel(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int ns_time)
{
    transfer_t result;
    IOReturn error;
    async_transfer_t transfer;
    
    memset(&transfer, '\0', sizeof(async_transfer_t));
    
    result = async_usb_ctrl_transfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, &transfer);
    if(result.ret != kIOReturnSuccess) {
        return result.ret;
    }
    nsleep(ns_time);
    
    error = io_abort_pipe_zero(client);
    if(error != kIOReturnSuccess) {
        return -1;
    }
    
    while(transfer.ret != kIOReturnAborted){
        CFRunLoopRun();
    }
    
    return transfer.wLenDone;
}

UInt32 async_usb_ctrl_transfer_no_error(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length)
{
    async_transfer_t transfer;
    memset(&transfer, '\0', sizeof(async_transfer_t));

    async_usb_ctrl_transfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, &transfer);
    return transfer.wLenDone;
}

UInt32 async_usb_ctrl_transfer_with_cancel_noloop(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int ns_time)
{
    async_transfer_t transfer;
    memset(&transfer, '\0', sizeof(async_transfer_t));

    async_usb_ctrl_transfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, &transfer);
    nsleep(ns_time);
    io_abort_pipe_zero(client);

    return transfer.wLenDone;
}
