/*
 * path_b_bootimg.c -- IPSW/iBSS resolution for Path B recovery boot.
 */

#include "bypass/path_b_bootimg.h"
#include "util/env_config.h"
#include "util/log.h"

#include <dirent.h>
#include <errno.h>
#include <libirecovery.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define IBSS_ZIP_PREFIX   "Firmware/dfu/iBSS."
#define IBSS_ZIP_SUFFIX   ".RELEASE.img4"
#define MAX_IBSS_ENTRIES  16
#define IBSS_ENTRY_LEN    256

static int file_readable(const char *path)
{
    return path && path[0] && access(path, R_OK) == 0;
}

static int copy_string(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0 || !src)
        return -1;
    if (snprintf(dst, dst_len, "%s", src) >= (int)dst_len)
        return -1;
    return 0;
}

static int newest_ipsw_in_dir(const char *dir, char *out, size_t out_len)
{
    DIR           *dp;
    struct dirent *ent;
    struct stat    st;
    char           candidate[512];
    char           best[512];
    time_t         best_mtime = 0;
    int            found = 0;

    if (!dir || !out || out_len == 0)
        return -1;

    dp = opendir(dir);
    if (!dp)
        return -1;

    best[0] = '\0';
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        size_t      nlen = strlen(name);

        if (nlen < 12)
            continue;
        if (strcasecmp(name + nlen - 5, ".ipsw") != 0)
            continue;
        if (strstr(name, "Restore") == NULL)
            continue;

        if (snprintf(candidate, sizeof(candidate), "%s/%s", dir, name)
            >= (int)sizeof(candidate))
            continue;
        if (stat(candidate, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        if (!found || st.st_mtime >= best_mtime) {
            if (copy_string(best, sizeof(best), candidate) != 0)
                continue;
            best_mtime = st.st_mtime;
            found = 1;
        }
    }

    closedir(dp);
    if (!found)
        return -1;
    return copy_string(out, out_len, best);
}

static int autodetect_ipsw(char *out, size_t out_len)
{
    static const char *dirs[] = {
        "/mnt/c/3uTools9/Firmware",
        "/mnt/c/3uTools/Firmware",
        NULL
    };
    const char *home;
    char        home_dir[512];
    size_t      i;

    for (i = 0; dirs[i]; i++) {
        if (newest_ipsw_in_dir(dirs[i], out, out_len) == 0)
            return 0;
    }

    home = getenv("HOME");
    if (home && home[0]) {
        if (snprintf(home_dir, sizeof(home_dir), "%s/Firmware", home)
            < (int)sizeof(home_dir)) {
            if (newest_ipsw_in_dir(home_dir, out, out_len) == 0)
                return 0;
        }
    }

    return -1;
}

static int is_ibss_zip_entry(const char *entry)
{
    size_t len;

    if (!entry)
        return 0;
    len = strlen(entry);
    if (len < strlen(IBSS_ZIP_PREFIX) + strlen(IBSS_ZIP_SUFFIX))
        return 0;
    if (strncmp(entry, IBSS_ZIP_PREFIX, strlen(IBSS_ZIP_PREFIX)) != 0)
        return 0;
    if (strcmp(entry + len - strlen(IBSS_ZIP_SUFFIX), IBSS_ZIP_SUFFIX) != 0)
        return 0;
    return 1;
}

static int list_ibss_entries(const char *ipsw,
                             char entries[][IBSS_ENTRY_LEN], int *count)
{
    FILE *fp;
    char  line[512];
    char  cmd[768];
    int   n = 0;

    if (!ipsw || !entries || !count)
        return -1;

    *count = 0;
    if (snprintf(cmd, sizeof(cmd), "unzip -Z1 \"%s\" 2>/dev/null", ipsw)
        >= (int)sizeof(cmd))
        return -1;

    fp = popen(cmd, "r");
    if (!fp) {
        log_error("[path_b_bootimg] popen(unzip) failed: %s", strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp) && n < MAX_IBSS_ENTRIES) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (!is_ibss_zip_entry(line))
            continue;
        if (copy_string(entries[n], IBSS_ENTRY_LEN, line) != 0)
            continue;
        n++;
    }

    pclose(fp);
    *count = n;
    return (n > 0) ? 0 : -1;
}

static int score_ibss_entry(const char *entry, uint32_t cpid)
{
    irecv_device_t table;
    int            i;
    int            best = 0;

    if (!entry || cpid == 0)
        return 0;

    table = irecv_devices_get_all();
    if (!table)
        return 0;

    for (i = 0; table[i].hardware_model != NULL; i++) {
        if (table[i].chip_id != cpid)
            continue;
        if (table[i].hardware_model[0] == '\0')
            continue;
        {
            size_t mlen = strlen(table[i].hardware_model);
            size_t j;

            for (j = 0; entry[j]; j++) {
                if (strncasecmp(entry + j, table[i].hardware_model, mlen) == 0)
                    return 100;
            }
        }
        if (best < 10)
            best = 10;
    }

    return best;
}

static int pick_ibss_entry(const char entries[][IBSS_ENTRY_LEN], int count,
                          uint32_t cpid, char *out, size_t out_len)
{
    int best_idx = 0;
    int best_score = -1;
    int i;

    if (!entries || count <= 0 || !out || out_len == 0)
        return -1;

    for (i = 0; i < count; i++) {
        int score = score_ibss_entry(entries[i], cpid);
        if (count == 1 || score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    return copy_string(out, out_len, entries[best_idx]);
}

static int cache_path_for(const char *ipsw, const char *zip_entry,
                          char *out, size_t out_len)
{
    const char *base;
    char        safe[128];
    size_t      i;
    size_t      pos = 0;

    base = strrchr(ipsw, '/');
    base = base ? base + 1 : ipsw;

    for (i = 0; base[i] && pos + 1 < sizeof(safe); i++) {
        char c = base[i];
        if (c == '.' || c == '-' || c == '_')
            safe[pos++] = c;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9'))
            safe[pos++] = c;
        else
            safe[pos++] = '_';
    }
    safe[pos] = '\0';

    {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || !tmp[0])
            tmp = getenv("TEMP");
        if (!tmp || !tmp[0])
            tmp = "/tmp";

        if (snprintf(out, out_len, "%s/tr4mpass/%s_iBSS.img4", tmp, safe)
            >= (int)out_len)
            return -1;
    }

    (void)zip_entry;
    return 0;
}

static int ensure_parent_dir(const char *path)
{
    char  dir[512];
    char *slash;

    if (!path)
        return -1;
    if (copy_string(dir, sizeof(dir), path) != 0)
        return -1;

    slash = strrchr(dir, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    if (dir[0] == '\0')
        return 0;

    if (mkdir(dir, 0700) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int extract_ibss(const char *ipsw, const char *zip_entry,
                        char *out, size_t out_len)
{
    struct stat ipsw_st;
    struct stat cache_st;
    char        cache[512];
    char        cmd[1024];
    int         rc;

    if (!ipsw || !zip_entry || !out || out_len == 0)
        return -1;

    if (cache_path_for(ipsw, zip_entry, cache, sizeof(cache)) != 0)
        return -1;

    if (stat(ipsw, &ipsw_st) != 0)
        return -1;

    if (stat(cache, &cache_st) == 0 && S_ISREG(cache_st.st_mode)
        && cache_st.st_mtime >= ipsw_st.st_mtime
        && cache_st.st_size > 0) {
        log_info("[path_b_bootimg] Using cached iBSS: %s", cache);
        return copy_string(out, out_len, cache);
    }

    if (ensure_parent_dir(cache) != 0) {
        log_error("[path_b_bootimg] Cannot create cache dir for %s", cache);
        return -1;
    }

    if (snprintf(cmd, sizeof(cmd),
                 "unzip -p \"%s\" \"%s\" > \"%s\" 2>/dev/null",
                 ipsw, zip_entry, cache) >= (int)sizeof(cmd))
        return -1;

    log_info("[path_b_bootimg] Extracting %s from IPSW...", zip_entry);
    rc = system(cmd);
    if (rc != 0 || !file_readable(cache)) {
        log_error("[path_b_bootimg] unzip extraction failed (rc=%d)", rc);
        unlink(cache);
        return -1;
    }

    return copy_string(out, out_len, cache);
}

static int resolve_from_ipsw(const char *ipsw, uint32_t cpid,
                             path_b_bootimg_result_t *out,
                             path_b_bootimg_source_t source)
{
    char entries[MAX_IBSS_ENTRIES][IBSS_ENTRY_LEN];
    char zip_entry[IBSS_ENTRY_LEN];
    int  count = 0;

    if (!file_readable(ipsw)) {
        log_error("[path_b_bootimg] IPSW not readable: %s", ipsw);
        return -1;
    }

    if (list_ibss_entries(ipsw, entries, &count) != 0) {
        log_error("[path_b_bootimg] No iBSS entry in IPSW: %s", ipsw);
        return -1;
    }

    if (pick_ibss_entry(entries, count, cpid, zip_entry, sizeof(zip_entry)) != 0)
        return -1;

    if (extract_ibss(ipsw, zip_entry, out->path, sizeof(out->path)) != 0)
        return -1;

    out->source = source;
    log_info("[path_b_bootimg] Resolved iBSS via %s: %s",
             path_b_bootimg_source_name(source), out->path);
    return 0;
}

const char *path_b_bootimg_source_name(path_b_bootimg_source_t source)
{
    switch (source) {
    case PATH_B_BOOTIMG_DIRECT:    return "TR4MPASS_IBSS_PATH";
    case PATH_B_BOOTIMG_FROM_IPSW: return "TR4MPASS_IPSW_PATH";
    case PATH_B_BOOTIMG_AUTODETECT: return "auto-detected IPSW";
    default:                       return "none";
    }
}

int path_b_resolve_ibss(const device_info_t *dev, path_b_bootimg_result_t *out)
{
    const char *ibss_env;
    const char *ipsw_env;
    char        ipsw_auto[512];

    if (!out)
        return -1;

    out->path[0]   = '\0';
    out->source    = PATH_B_BOOTIMG_NONE;

    ibss_env = env_get(TR4MPASS_ENV_IBSS_PATH, NULL);
    if (ibss_env && ibss_env[0]) {
        if (!file_readable(ibss_env)) {
            log_error("[path_b_bootimg] TR4MPASS_IBSS_PATH not readable: %s",
                      ibss_env);
            return -1;
        }
        if (copy_string(out->path, sizeof(out->path), ibss_env) != 0)
            return -1;
        out->source = PATH_B_BOOTIMG_DIRECT;
        log_info("[path_b_bootimg] Using iBSS from %s",
                 path_b_bootimg_source_name(out->source));
        return 0;
    }

    ipsw_env = env_get(TR4MPASS_ENV_IPSW_PATH, NULL);
    if (ipsw_env && ipsw_env[0])
        return resolve_from_ipsw(ipsw_env, dev ? dev->cpid : 0, out,
                                 PATH_B_BOOTIMG_FROM_IPSW);

    if (autodetect_ipsw(ipsw_auto, sizeof(ipsw_auto)) == 0) {
        log_info("[path_b_bootimg] Auto-detected IPSW: %s", ipsw_auto);
        return resolve_from_ipsw(ipsw_auto, dev ? dev->cpid : 0, out,
                                 PATH_B_BOOTIMG_AUTODETECT);
    }

    log_info("[path_b_bootimg] No iBSS/IPSW found -- will attempt automatic USB reset, then manual fallback after 60s");
    return 1;
}
