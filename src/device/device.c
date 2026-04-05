/*
 * device.c -- Device detection and info querying via libimobiledevice.
 *
 * Extended from research_icloud0/src/device.c with CPID, ECID, MEID,
 * chip database integration, cellular detection, and DFU mode awareness.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <plist/plist.h>

#include "device/device.h"
#include "device/chip_db.h"
#include "util/log.h"

/* ------------------------------------------------------------------ */
/* Lockdownd helpers                                                  */
/* ------------------------------------------------------------------ */

/* Extract a string value from lockdownd for a given key. */
static int query_string_value(lockdownd_client_t lockdown,
                              const char *domain, const char *key,
                              char *out, size_t out_len)
{
    plist_t val = NULL;
    lockdownd_error_t err;

    err = lockdownd_get_value(lockdown, domain, key, &val);
    if (err != LOCKDOWN_E_SUCCESS || !val) {
        out[0] = '\0';
        return -1;
    }

    if (plist_get_node_type(val) == PLIST_STRING) {
        char *str = NULL;
        plist_get_string_val(val, &str);
        if (str) {
            snprintf(out, out_len, "%s", str);
            free(str);
        } else {
            out[0] = '\0';
        }
    } else if (plist_get_node_type(val) == PLIST_UINT) {
        uint64_t uval = 0;
        plist_get_uint_val(val, &uval);
        snprintf(out, out_len, "0x%llx", (unsigned long long)uval);
    } else {
        out[0] = '\0';
        plist_free(val);
        return -1;
    }

    plist_free(val);
    return 0;
}

/* Extract a uint64 value from lockdownd for a given key. */
static int query_uint_value(lockdownd_client_t lockdown,
                            const char *domain, const char *key,
                            uint64_t *out)
{
    plist_t val = NULL;
    lockdownd_error_t err;

    *out = 0;
    err = lockdownd_get_value(lockdown, domain, key, &val);
    if (err != LOCKDOWN_E_SUCCESS || !val)
        return -1;

    if (plist_get_node_type(val) == PLIST_UINT) {
        plist_get_uint_val(val, out);
        plist_free(val);
        return 0;
    }

    plist_free(val);
    return -1;
}

/* ------------------------------------------------------------------ */
/* device_detect                                                      */
/* ------------------------------------------------------------------ */

int device_detect(device_info_t *dev)
{
    char **devices = NULL;
    int count = 0;
    idevice_error_t ierr;
    lockdownd_error_t lerr;

    if (!dev) {
        log_error("NULL device_info_t pointer");
        return -1;
    }

    memset(dev, 0, sizeof(*dev));

    /* Enumerate connected USB devices */
    ierr = idevice_get_device_list(&devices, &count);
    if (ierr != IDEVICE_E_SUCCESS || count == 0) {
        log_error("No devices found (idevice error %d)", (int)ierr);
        if (devices)
            idevice_device_list_free(devices);
        return -1;
    }

    log_info("Found %d device(s), using first: %s", count, devices[0]);
    snprintf(dev->udid, sizeof(dev->udid), "%s", devices[0]);
    idevice_device_list_free(devices);

    /* Open device handle */
    ierr = idevice_new(&dev->handle, dev->udid);
    if (ierr != IDEVICE_E_SUCCESS) {
        log_error("idevice_new failed (error %d)", (int)ierr);
        return -1;
    }

    /* Start lockdown session with handshake (validates pairing) */
    lerr = lockdownd_client_new_with_handshake(dev->handle,
                                                &dev->lockdown,
                                                "tr4mpass");
    if (lerr != LOCKDOWN_E_SUCCESS) {
        log_error("lockdownd handshake failed (error %d)", (int)lerr);
        idevice_free(dev->handle);
        dev->handle = NULL;
        return -1;
    }

    log_info("Connected to device %s", dev->udid);
    return 0;
}

/* ------------------------------------------------------------------ */
/* device_query_info                                                  */
/* ------------------------------------------------------------------ */

int device_query_info(device_info_t *dev)
{
    int failures = 0;
    uint64_t raw_cpid = 0;

    if (!dev || !dev->lockdown) {
        log_error("No lockdown session");
        return -1;
    }

    /* --- Existing string queries --- */

    if (query_string_value(dev->lockdown, NULL, "ProductType",
                           dev->product_type, sizeof(dev->product_type)) < 0) {
        log_warn("Could not query ProductType");
        failures++;
    }

    if (query_string_value(dev->lockdown, NULL, "ChipID",
                           dev->chip_id, sizeof(dev->chip_id)) < 0) {
        log_warn("Could not query ChipID (string)");
        failures++;
    }

    if (query_string_value(dev->lockdown, NULL, "ProductVersion",
                           dev->ios_version, sizeof(dev->ios_version)) < 0) {
        log_warn("Could not query ProductVersion");
        failures++;
    }

    if (query_string_value(dev->lockdown, NULL, "ActivationState",
                           dev->activation_state,
                           sizeof(dev->activation_state)) < 0) {
        log_warn("Could not query ActivationState");
        failures++;
    }

    if (query_string_value(dev->lockdown, NULL, "SerialNumber",
                           dev->serial, sizeof(dev->serial)) < 0) {
        log_warn("Could not query SerialNumber");
        failures++;
    }

    /* IMEI may not exist on WiFi-only iPads */
    query_string_value(dev->lockdown, NULL,
                       "InternationalMobileEquipmentIdentity",
                       dev->imei, sizeof(dev->imei));

    if (query_string_value(dev->lockdown, NULL, "HardwareModel",
                           dev->hardware_model,
                           sizeof(dev->hardware_model)) < 0) {
        log_warn("Could not query HardwareModel");
        failures++;
    }

    if (query_string_value(dev->lockdown, NULL, "DeviceName",
                           dev->device_name, sizeof(dev->device_name)) < 0) {
        log_warn("Could not query DeviceName");
        failures++;
    }

    /* --- New numeric queries (task 004) --- */

    if (query_uint_value(dev->lockdown, NULL, "ChipID", &raw_cpid) == 0) {
        dev->cpid = (uint32_t)raw_cpid;
    } else {
        log_warn("Could not query ChipID (numeric)");
    }

    if (query_uint_value(dev->lockdown, NULL, "UniqueChipID",
                         &dev->ecid) < 0) {
        log_warn("Could not query UniqueChipID (ECID)");
    }

    /* MEID may not exist on GSM-only or WiFi-only devices */
    query_string_value(dev->lockdown, NULL,
                       "MobileEquipmentIdentifier",
                       dev->meid, sizeof(dev->meid));

    /* --- Chip database lookup --- */

    if (dev->cpid != 0) {
        const chip_info_t *chip = chip_db_lookup(dev->cpid);
        if (chip) {
            snprintf(dev->chip_name, sizeof(dev->chip_name), "%s",
                     chip->name);
            dev->checkm8_vulnerable = chip->checkm8_vulnerable;
        } else {
            snprintf(dev->chip_name, sizeof(dev->chip_name), "Unknown");
            dev->checkm8_vulnerable = 0;
            log_warn("CPID 0x%04X not found in chip database", dev->cpid);
        }
    }

    /* --- Cellular detection --- */

    dev->is_cellular = (dev->imei[0] != '\0' || dev->meid[0] != '\0');

    /* DFU mode and USB handle are set externally by usb_dfu module */
    dev->is_dfu_mode = 0;
    dev->usb = NULL;

    return (failures > 3) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* device_print_info                                                  */
/* ------------------------------------------------------------------ */

void device_print_info(const device_info_t *dev)
{
    if (!dev) return;

    printf("\n--- Device Information ---\n");
    printf("  UDID:             %s\n", dev->udid);
    printf("  Product Type:     %s\n", dev->product_type);
    printf("  Chip ID:          %s\n", dev->chip_id);
    printf("  iOS Version:      %s\n", dev->ios_version);
    printf("  Activation State: %s\n", dev->activation_state);
    printf("  Serial:           %s\n", dev->serial);
    printf("  IMEI:             %s\n",
           dev->imei[0] ? dev->imei : "(none / WiFi-only)");
    printf("  MEID:             %s\n",
           dev->meid[0] ? dev->meid : "(none)");
    printf("  Hardware Model:   %s\n", dev->hardware_model);
    printf("  Device Name:      %s\n", dev->device_name);

    /* New fields */
    printf("  CPID:             0x%04X\n", dev->cpid);
    printf("  ECID:             0x%llX\n", (unsigned long long)dev->ecid);
    printf("  Chip Name:        %s\n",
           dev->chip_name[0] ? dev->chip_name : "(unknown)");
    printf("  checkm8 vuln:     %s\n",
           dev->checkm8_vulnerable ? "YES" : "NO");
    printf("  Cellular:         %s\n",
           dev->is_cellular ? "YES" : "NO (WiFi-only)");
    printf("  DFU Mode:         %s\n",
           dev->is_dfu_mode ? "YES" : "NO");
    printf("-------------------------\n\n");
}

/* ------------------------------------------------------------------ */
/* device_free                                                        */
/* ------------------------------------------------------------------ */

void device_free(device_info_t *dev)
{
    if (!dev) return;

    if (dev->lockdown) {
        lockdownd_client_free(dev->lockdown);
        dev->lockdown = NULL;
    }
    if (dev->handle) {
        idevice_free(dev->handle);
        dev->handle = NULL;
    }

    /* usb handle owned by usb_dfu module; not freed here */
    dev->usb = NULL;
}
