#!/bin/bash
#
# Single-host demo of doc/source/tls_userguide.md: builds an emu-backed iiod,
# wraps it in a stunnel TLS tunnel, and queries it through the tunnel with
# iio_info. Everything (iiod, stunnel server, stunnel client) runs on this
# machine; the tunnel is torn down again at the end. Linux/Ubuntu only.
#
# Usage: ./tls_demo.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-tls-demo"
XML_FILE="$REPO_ROOT/tests/resources/xmls/hwmon.xml"

IIOD_PORT=30431
TLS_PORT=30432
CLIENT_PORT=40431
CERT_CN=iiod-server

log() { echo "[tls_demo] $*"; }
die() { echo "[tls_demo] ERROR: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 0. Prerequisites
# ---------------------------------------------------------------------------

[ -f "$XML_FILE" ] || die "demo context file not found: $XML_FILE"
command -v iio_info >/dev/null 2>&1 || die "iio_info not found on PATH; install libiio-utils first"

missing_pkgs=()
command -v openssl >/dev/null 2>&1 || missing_pkgs+=(openssl)
command -v cmake >/dev/null 2>&1 || missing_pkgs+=(cmake)
command -v gcc >/dev/null 2>&1 || missing_pkgs+=(build-essential)
command -v bison >/dev/null 2>&1 || missing_pkgs+=(bison)
command -v flex >/dev/null 2>&1 || missing_pkgs+=(flex)
command -v pkg-config >/dev/null 2>&1 || missing_pkgs+=(pkg-config)
pkg-config --exists libxml-2.0 2>/dev/null || missing_pkgs+=(libxml2-dev)

STUNNEL_BIN="$(command -v stunnel || command -v stunnel4 || true)"
[ -n "$STUNNEL_BIN" ] || missing_pkgs+=(stunnel4)

if [ "${#missing_pkgs[@]}" -gt 0 ]; then
    log "installing missing packages: ${missing_pkgs[*]}"
    sudo apt-get update
    sudo apt-get install -y "${missing_pkgs[@]}"
fi

[ -n "$STUNNEL_BIN" ] || STUNNEL_BIN="$(command -v stunnel || command -v stunnel4)"

# ---------------------------------------------------------------------------
# 1. Build an emu-capable iiod (reused across runs)
# ---------------------------------------------------------------------------

IIOD_BIN="$BUILD_DIR/iiod/iiod"

if [ ! -x "$IIOD_BIN" ]; then
    log "configuring and building iiod with the emu backend in $BUILD_DIR"
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DWITH_EMU_BACKEND=ON \
        -DWITH_IIOD=ON \
        -DWITH_UTILS=OFF \
        -DWITH_EXAMPLES=OFF \
        -DWITH_MAN=OFF \
        -DWITH_TESTS=OFF \
        -DCPP_BINDINGS=OFF
    cmake --build "$BUILD_DIR" --target iiod -j"$(nproc)"
else
    log "reusing existing iiod build at $IIOD_BIN"
fi

# ---------------------------------------------------------------------------
# 2. Work area: cert, key, stunnel configs, logs (removed on exit)
# ---------------------------------------------------------------------------

WORKDIR="$(mktemp -d /tmp/tls_demo.XXXXXX)"

IIOD_PID=""
SERVER_STUNNEL_PID=""
CLIENT_STUNNEL_PID=""

cleanup() {
    log "tearing down"
    [ -n "$CLIENT_STUNNEL_PID" ] && kill "$CLIENT_STUNNEL_PID" 2>/dev/null || true
    [ -n "$SERVER_STUNNEL_PID" ] && kill "$SERVER_STUNNEL_PID" 2>/dev/null || true
    [ -n "$IIOD_PID" ] && kill "$IIOD_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$WORKDIR"
}
trap cleanup EXIT INT TERM

wait_for_port() {
    local port=$1 tries=50
    while ! exec 3<>"/dev/tcp/127.0.0.1/$port" 2>/dev/null; do
        tries=$((tries - 1))
        [ "$tries" -gt 0 ] || die "nothing listening on 127.0.0.1:$port after 5s"
        sleep 0.1
    done
    exec 3>&- 3<&-
}

# Step 1 (guide): generate a self-signed certificate
openssl req -new -x509 -days 365 -nodes \
    -out "$WORKDIR/server.crt" \
    -keyout "$WORKDIR/server.key" \
    -subj "/CN=$CERT_CN" 2>"$WORKDIR/openssl.log"

# Step 2 (guide): server-side stunnel, iiod -> TLS
cat > "$WORKDIR/stunnel-server.conf" <<EOF
foreground = yes
pid = $WORKDIR/stunnel-server.pid
output = $WORKDIR/stunnel-server.log

[iiod]
accept = 127.0.0.1:$TLS_PORT
connect = 127.0.0.1:$IIOD_PORT
cert = $WORKDIR/server.crt
key = $WORKDIR/server.key
sslVersion = TLSv1.2
options = NO_SSLv2
options = NO_SSLv3
EOF

# Step 3 (guide): client-side stunnel, TLS -> local plain port
cat > "$WORKDIR/stunnel-client.conf" <<EOF
foreground = yes
pid = $WORKDIR/stunnel-client.pid
output = $WORKDIR/stunnel-client.log

[iiod]
client = yes
accept = 127.0.0.1:$CLIENT_PORT
connect = 127.0.0.1:$TLS_PORT
CAfile = $WORKDIR/server.crt
verifyChain = yes
checkHost = $CERT_CN
EOF

# ---------------------------------------------------------------------------
# 3. Start iiod and both stunnel legs
# ---------------------------------------------------------------------------

log "starting iiod on 127.0.0.1:$IIOD_PORT (emu context: $(basename "$XML_FILE"))"
"$IIOD_BIN" -u "emu:$XML_FILE" -p "$IIOD_PORT" >"$WORKDIR/iiod.log" 2>&1 &
IIOD_PID=$!
wait_for_port "$IIOD_PORT"

log "starting server-side stunnel: 127.0.0.1:$TLS_PORT (TLS) -> 127.0.0.1:$IIOD_PORT"
"$STUNNEL_BIN" "$WORKDIR/stunnel-server.conf" &
SERVER_STUNNEL_PID=$!
wait_for_port "$TLS_PORT"

log "starting client-side stunnel: 127.0.0.1:$CLIENT_PORT -> 127.0.0.1:$TLS_PORT (TLS)"
"$STUNNEL_BIN" "$WORKDIR/stunnel-client.conf" &
CLIENT_STUNNEL_PID=$!
wait_for_port "$CLIENT_PORT"

# ---------------------------------------------------------------------------
# 4. Step 4 (guide): connect with libiio through the tunnel
# ---------------------------------------------------------------------------

log "querying iiod through the TLS tunnel: iio_info -u ip:127.0.0.1:$CLIENT_PORT"
echo
iio_info -u "ip:127.0.0.1:$CLIENT_PORT"
echo
log "success: the connection above went through the TLS tunnel, not straight to iiod"
