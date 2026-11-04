/*
 * cli_flags.h -- Pipeline-pipeline CLI flag parsing for tr4mpass.
 *
 * main.c owns the existing short flags (--verbose, --dry-run,
 * --detect-only, --force-path-a/b, --cpid, --ecid) and passes argv
 * here for a second parse that translates --ramdisk, --ssh-host etc
 * into setenv() calls.  The env vars are consumed later by Phase 2B
 * (ramdisk staging) and Phase 2C (SSH delivery).
 *
 * The helper here is intentionally additive: unknown flags are
 * ignored (getopt_long returns '?' when the short option list is
 * empty) and the existing flags in main.c remain untouched.
 */

#ifndef TR4MPASS_CLI_FLAGS_H
#define TR4MPASS_CLI_FLAGS_H

/*
 * Parse argv for pipeline-pipeline env overrides and apply them via
 * setenv(..., 1).  This is called after main.c's own getopt_long
 * loop so the short flags already consumed by main.c remain visible
 * to getopt via argv -- we simply skip anything we do not recognize.
 *
 * Returns 0 on success, -1 on argument errors (e.g. flag missing
 * value).  Help output for the new flags is produced by
 * cli_flags_print_help() so main.c --help can delegate.
 */
int cli_parse_pipeline_flags(int argc, char *argv[]);

/*
 * Print the pipeline-pipeline flag block used by --help.  Kept separate
 * so main.c keeps responsibility for the overall usage banner.
 */
void cli_flags_print_help(void);

#endif /* TR4MPASS_CLI_FLAGS_H */
