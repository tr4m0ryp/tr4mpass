#!/usr/bin/env python3
"""
HTTPS activation server for iOS bypass.

Extends portal.py to serve over HTTPS using the project's generated
TLS certificates. When /etc/hosts redirects Apple activation domains
to localhost, the iOS activation tools (ideviceactivation or
pymobiledevice3) connect here instead of Apple's servers.

Serves on both port 80 (HTTP) and port 443 (HTTPS) simultaneously
using threading. The HTTPS server uses certs/server.crt signed by
certs/ca.crt.
"""

import os
import ssl
import sys
import threading
import logging
from http.server import HTTPServer

# Import the handler from the existing portal server
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
from portal import PortalHandler  # noqa: E402

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
CERTS_DIR = os.path.join(PROJECT_DIR, "certs")
SERVER_CERT = os.path.join(CERTS_DIR, "server.crt")
SERVER_KEY = os.path.join(CERTS_DIR, "server.key")
CA_CERT = os.path.join(CERTS_DIR, "ca.crt")

logger = logging.getLogger("portal_https")


def _check_certs():
    """Verify TLS certificates exist. Return True if valid."""
    missing = []
    for path in (SERVER_CERT, SERVER_KEY, CA_CERT):
        if not os.path.isfile(path):
            missing.append(path)
    if missing:
        for p in missing:
            logger.error("Missing certificate: %s", p)
        logger.error("Run certs/generate_ca.sh first")
        return False
    return True


def create_ssl_context():
    """Build SSLContext for the HTTPS server."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=SERVER_CERT, keyfile=SERVER_KEY)
    # Allow all clients -- we do not verify client certs
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    # Accept TLS 1.2+ (iOS uses TLS 1.2 or 1.3 for activation)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    return ctx


def _run_server(server, label):
    """Serve forever in a thread, logging startup."""
    addr, port = server.server_address
    logger.info("%s server listening on %s:%d", label, addr, port)
    try:
        server.serve_forever()
    except Exception as e:
        logger.error("%s server error: %s", label, e)


def start_servers(host="0.0.0.0", http_port=80, https_port=443):
    """
    Start both HTTP and HTTPS servers.

    Returns:
        tuple: (http_server, https_server, http_thread, https_thread)
    """
    if not _check_certs():
        sys.exit(1)

    # HTTP server
    http_server = HTTPServer((host, http_port), PortalHandler)

    # HTTPS server
    https_server = HTTPServer((host, https_port), PortalHandler)
    ssl_ctx = create_ssl_context()
    https_server.socket = ssl_ctx.wrap_socket(
        https_server.socket, server_side=True
    )

    http_thread = threading.Thread(
        target=_run_server,
        args=(http_server, "HTTP"),
        daemon=True,
    )
    https_thread = threading.Thread(
        target=_run_server,
        args=(https_server, "HTTPS"),
        daemon=True,
    )

    http_thread.start()
    https_thread.start()

    return http_server, https_server, http_thread, https_thread


def stop_servers(http_server, https_server):
    """Shut down both servers."""
    logger.info("Shutting down servers")
    try:
        http_server.shutdown()
    except Exception:
        pass
    try:
        https_server.shutdown()
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s | %(name)-14s | %(message)s",
        datefmt="%H:%M:%S",
    )

    host = "0.0.0.0"
    http_port = int(os.environ.get("PORTAL_PORT", 80))
    https_port = int(os.environ.get("PORTAL_HTTPS_PORT", 443))

    print("=" * 56)
    print("  Captive Portal HTTPS Server")
    print("=" * 56)
    print(f"  HTTP  : {host}:{http_port}")
    print(f"  HTTPS : {host}:{https_port}")
    print(f"  Cert  : {SERVER_CERT}")
    print(f"  Key   : {SERVER_KEY}")
    print()

    http_srv, https_srv, http_t, https_t = start_servers(
        host, http_port, https_port
    )

    try:
        # Block on the HTTP thread (both are daemons, so main must stay alive)
        http_t.join()
    except KeyboardInterrupt:
        print("\nShutting down.")
        stop_servers(http_srv, https_srv)


if __name__ == "__main__":
    main()
