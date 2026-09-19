#ifndef TR4MPASS_CLI_DOCTOR_H
#define TR4MPASS_CLI_DOCTOR_H

/*
 * doctor.h -- `tr4mpass doctor` preflight diagnostic subcommand (T10).
 *
 * Runs a dependency-light environment report covering: pkg-config
 * presence/version for the 7 libraries tr4mpass links against,
 * libplist/libirecovery compat-shim status (see include/compat/),
 * the build-time probe results recorded in .build-flags, usbmuxd
 * state, USB visibility of Apple (05ac) devices, WSL detection, and
 * a binary-shadowing check (./tr4mpass must be a regular file, not a
 * directory).
 *
 * Callable standalone before any USB/device init runs -- `main()`
 * dispatches to it directly when argv[1] == "doctor", bypassing the
 * rest of the normal startup path entirely.
 */

/*
 * Run the full preflight report and print it to stdout. When
 * `verbose` is non-zero, print additional detail (e.g. every
 * .build-flags entry) under checks that support it.
 *
 * Returns 0 if every check passed or only produced warnings, 1 if at
 * least one check hard-failed. Never exits the process itself.
 */
int doctor_run(int verbose);

#endif /* TR4MPASS_CLI_DOCTOR_H */
