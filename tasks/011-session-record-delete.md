# Task 011: Session Protocol + Record Builder + Deletescript

## Objective
Implement three related modules: (1) the drmHandshake session activation
protocol, (2) the activation record plist builder, and (3) the iOS-version
deletescript. These are the post-exploit activation components.

## Scope
**Files to create:**
- Itr4m/include/activation/session.h
- Itr4m/src/activation/session.c
- Itr4m/include/activation/record.h
- Itr4m/src/activation/record.c
- Itr4m/include/bypass/deletescript.h
- Itr4m/src/bypass/deletescript.c

**Files to read (not modify):**
- tasks/rules.md
- notes/Itr4m.md (drmHandshake protocol, activation record format, deletescript)
- Itr4m/include/activation/activation.h (from task 009)
- Itr4m/include/util/plist_helpers.h
- Itr4m/include/bypass/afc_utils.h

**Out of scope:**
- mobileactivation base client (task 009)
- Path A/B orchestration (tasks 012, 013)

## Dependencies
- **Requires:** 009 (activation client)
- **Produces:** Session, record, and deletescript functions used by both paths

## Technical Details

### session.h -- drmHandshake protocol
```c
/* Get session info blob from device (CreateTunnel1SessionInfoRequest) */
int session_get_info(device_info_t *dev, plist_t *session_info);

/* Perform local drmHandshake using session info.
   In offline mode, we construct the handshake response locally. */
int session_drm_handshake(device_info_t *dev, plist_t session_info,
                          plist_t *handshake_response);

/* Create activation info with session (CreateTunnel1ActivationInfoRequest) */
int session_create_activation_info(device_info_t *dev,
                                   plist_t handshake_response,
                                   plist_t *activation_info);

/* Finalize activation with session (HandleActivationInfoWithSessionRequest) */
int session_activate(device_info_t *dev, plist_t activation_record,
                     plist_t handshake_response);
```

The session protocol wraps libimobiledevice's:
- mobileactivation_create_activation_session_info()
- mobileactivation_create_activation_info_with_session()
- mobileactivation_activate_with_session()

### record.h -- activation record builder
```c
/* Build a complete activation record plist from device info.
   Returns a new plist dict that must be freed by caller. */
plist_t record_build(const device_info_t *dev);

/* Build A12+ specific activation record (FActivation variant) */
plist_t record_build_a12(const device_info_t *dev);

/* Serialize record to XML string. Caller must free. */
char *record_to_xml(plist_t record, uint32_t *len);

/* Free a record */
void record_free(plist_t record);
```

Record contains: AccountToken, DeviceCertificate, FairPlayKeyData,
UniqueDeviceCertificate, etc. See Itr4m.md activation record format section.
Use placeholder values for certificates (these come from the device in practice).

### deletescript.h -- post-bypass cleanup
```c
typedef struct {
    int ios_major;
    int ios_minor;
    int ios_patch;
} ios_version_t;

/* Parse iOS version string "26.1" into components */
int deletescript_parse_version(const char *version_str, ios_version_t *ver);

/* Run the appropriate deletescript for the device's iOS version.
   Requires AFC connection to device. */
int deletescript_run(device_info_t *dev);

/* Individual cleanup operations (called by deletescript_run) */
int deletescript_remove_setup_app(device_info_t *dev);
int deletescript_set_purplebuddy_done(device_info_t *dev);
int deletescript_purge_activation_records(device_info_t *dev);
int deletescript_clear_caches(device_info_t *dev);
```

Targets (from research):
- /Applications/Setup.app -> rename to Setup.app.bak
- /private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist -> set SetupDone
- /var/root/Library/Lockdown/activation_records/ -> purge
- /var/mobile/Library/mad/activation_records/ -> purge

## Rules
- Read tasks/rules.md before starting
- Each .c file MUST be under 300 lines
- Use plist_helpers for plist construction
- Use afc_utils for file operations in deletescript
- Mark placeholder certificate data with TODO

## Verification
- [ ] session.c under 300 lines
- [ ] record.c under 300 lines
- [ ] deletescript.c under 300 lines
- [ ] All headers have proper guards
- [ ] Plist construction uses libplist API correctly

## Stop Conditions
- If any file approaches 300 lines, split immediately
