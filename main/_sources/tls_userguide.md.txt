# Encrypting Connections to iiod with stunnel

By default, the libiio network backend and `iiod` communicate using a binary protocol (the older text-based protocol, used up to version 0.26, is still supported for backwards compatibility, but is no longer the default). Regardless of which protocol is used, none of this traffic is encrypted: anyone with access to the network path between a client and an `iiod` server can read or tamper with it. This page shows how to add Transport Layer Security (TLS) encryption to an existing libiio/iiod deployment using [stunnel](https://www.stunnel.org/), a standalone TLS proxy.

Because stunnel sits outside libiio and `iiod`, **no code changes or recompilation are required**. It works by wrapping the plain `iiod` connection in a TLS tunnel:

```
┌─────────────────────────────┐         ┌─────────────────────────────┐
│       CLIENT MACHINE        │         │       SERVER MACHINE        │
│                             │         │                             │
│ ┌────────┐    ┌───────────┐ │   TLS   │ ┌───────────┐    ┌────────┐ │
│ │ libiio │───►│  stunnel  │ │════════►│ │  stunnel  │───►│  iiod  │ │
│ │ client │    │  client   │ │  :30432 │ │  server   │    │        │ │
│ └────────┘    └───────────┘ │         │ └───────────┘    └────────┘ │
│         127.0.0.1:30431     │         │ 127.0.0.1:30431             │
└─────────────────────────────┘         └─────────────────────────────┘
```

The libiio client keeps connecting to `127.0.0.1`, exactly as it would to a local `iiod`. The client-side stunnel process encrypts that traffic and forwards it over TLS (port 30432) to the server-side stunnel process, which decrypts it and forwards it to the real `iiod` (port 30431), listening only on `localhost`.

:::{note}
This setup encrypts the connection and lets the client verify the server's identity (server-authenticated TLS). Keep in mind a few practical limitations of this approach:
- The client always connects to `127.0.0.1`; the *actual* remote server is determined by the client's stunnel configuration, not by the libiio URI.
- Each remote `iiod` server needs its own local stunnel listening port on the client.
- stunnel requires a full OS userspace, so this approach does not apply to deeply embedded or RTOS targets (e.g. Zephyr).
:::

## Prerequisites

- An `iiod` server already running and reachable on the server machine (default port 30431).
- `openssl`, to generate a certificate.
- `stunnel` installed on **both** the server and the client machine.

````{tab} Linux

```shell
# Debian/Ubuntu
sudo apt-get install stunnel4 openssl

# RHEL/CentOS/Fedora
sudo yum install stunnel openssl
```

````

````{tab} macOS

```shell
brew install stunnel openssl
```

````

````{tab} Windows

Download and run the installer from the [stunnel downloads page](https://www.stunnel.org/downloads.html). Accept the default installation directory (typically `C:\Program Files\stunnel`); it comes bundled with `openssl.exe`.

````

(step-1-generate-a-tls-certificate-on-the-server)=
## Step 1: Generate a TLS certificate on the server

For testing, a self-signed certificate is enough. For a production deployment, use a certificate signed by a trusted (internal or public) Certificate Authority instead.

```shell
openssl req -new -x509 -days 365 -nodes \
    -out server.crt \
    -keyout server.key \
    -subj "/CN=iiod-server"
```

This creates `server.crt` (the certificate, safe to share with clients) and `server.key` (the private key, must stay on the server only).

````{tab} Linux

```shell
sudo mkdir -p /etc/stunnel/certs
sudo mv server.crt server.key /etc/stunnel/certs/
sudo chmod 600 /etc/stunnel/certs/server.key
sudo chmod 644 /etc/stunnel/certs/server.crt
```

````

````{tab} macOS

```shell
sudo mkdir -p /usr/local/etc/stunnel/certs
sudo mv server.crt server.key /usr/local/etc/stunnel/certs/
sudo chmod 600 /usr/local/etc/stunnel/certs/server.key
sudo chmod 644 /usr/local/etc/stunnel/certs/server.crt
```

On Apple Silicon Macs with Homebrew, replace `/usr/local` with `/opt/homebrew`.

````

````{tab} Windows

Copy `server.crt` and `server.key` into a `certs` folder inside the stunnel installation directory, e.g. `C:\Program Files\stunnel\config\certs\`.

````

You will need to copy `server.crt` (only the certificate, **not** the key) to the client machine later, so the client can verify the server's identity.

## Step 2: Configure and start stunnel on the server

Create a stunnel configuration file that accepts TLS connections and forwards the decrypted traffic to the local `iiod`.

````{tab} Linux

`/etc/stunnel/iiod.conf`:
```ini
pid = /var/run/stunnel-iiod.pid
setuid = nobody
setgid = nogroup
output = /var/log/stunnel-iiod.log

[iiod]
accept = 0.0.0.0:30432
connect = 127.0.0.1:30431
cert = /etc/stunnel/certs/server.crt
key = /etc/stunnel/certs/server.key
sslVersion = TLSv1.2
options = NO_SSLv2
options = NO_SSLv3
```

Start stunnel with:
```shell
sudo stunnel /etc/stunnel/iiod.conf
```

To run it automatically as a systemd service, create `/etc/systemd/system/stunnel-iiod.service`:
```ini
[Unit]
Description=TLS tunnel for iiod
After=network.target iiod.service
Requires=iiod.service

[Service]
Type=forking
ExecStart=/usr/bin/stunnel /etc/stunnel/iiod.conf
ExecStop=/bin/kill -TERM $MAINPID
PIDFile=/var/run/stunnel-iiod.pid
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

```shell
sudo systemctl enable --now stunnel-iiod.service
```

````

````{tab} macOS

`/usr/local/etc/stunnel/iiod.conf` (use `/opt/homebrew/etc/stunnel/iiod.conf` on Apple Silicon):
```ini
foreground = yes
output = /usr/local/var/log/stunnel-iiod.log

[iiod]
accept = 0.0.0.0:30432
connect = 127.0.0.1:30431
cert = /usr/local/etc/stunnel/certs/server.crt
key = /usr/local/etc/stunnel/certs/server.key
sslVersion = TLSv1.2
```

Start it in the foreground to test:
```shell
stunnel /usr/local/etc/stunnel/iiod.conf
```

To run it automatically at boot, create a launchd job, e.g. `/Library/LaunchDaemons/com.stunnel.iiod.plist`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.stunnel.iiod</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/stunnel</string>
        <string>/usr/local/etc/stunnel/iiod.conf</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

```shell
sudo launchctl load /Library/LaunchDaemons/com.stunnel.iiod.plist
```

````

````{tab} Windows

`C:\Program Files\stunnel\config\iiod.conf`:
```ini
output = C:\Program Files\stunnel\iiod.log

[iiod]
accept = 0.0.0.0:30432
connect = 127.0.0.1:30431
cert = C:\Program Files\stunnel\config\certs\server.crt
key = C:\Program Files\stunnel\config\certs\server.key
sslVersion = TLSv1.2
```

Run stunnel from the Start Menu shortcut, or install it as a Windows service from an elevated command prompt:
```
cd "C:\Program Files\stunnel"
stunnel.exe -install -quiet
net start stunnel
```

Manage the service afterward through the Windows *Services* panel (`services.msc`).

````

### Restrict direct (unencrypted) access to iiod

`iiod` itself listens on all interfaces and has no built-in option to restrict itself to `localhost`, so once stunnel is running, block external access to the plain port (30431) with a firewall, and only allow the TLS port (30432):

````{tab} Linux

```shell
sudo iptables -A INPUT -p tcp --dport 30431 ! -s 127.0.0.1 -j DROP
sudo iptables -A INPUT -p tcp --dport 30432 -j ACCEPT
```

````

````{tab} macOS

Add a rule to `pf` (Packet Filter), e.g. in `/etc/pf.anchors/iiod`:
```
block in proto tcp from any to any port 30431
pass in proto tcp from any to any port 30432
```
Load it with `sudo pfctl -f /etc/pf.conf` after referencing the anchor in `pf.conf`.

````

````{tab} Windows

```powershell
New-NetFirewallRule -DisplayName "Block iiod plain TCP" -Direction Inbound -Protocol TCP -LocalPort 30431 -RemoteAddress Any -Action Block
New-NetFirewallRule -DisplayName "Allow iiod TLS" -Direction Inbound -Protocol TCP -LocalPort 30432 -Action Allow
```

````

## Step 3: Configure and start stunnel on the client

Copy the server's `server.crt` (the certificate only, generated in [Step 1](#step-1-generate-a-tls-certificate-on-the-server)) to the client machine over a secure channel.

````{tab} Linux

`/etc/stunnel/iiod.conf`:
```ini
pid = /var/run/stunnel-iiod-client.pid
foreground = no

[iiod]
client = yes
accept = 127.0.0.1:30431
connect = <SERVER_IP_OR_HOSTNAME>:30432
CAfile = /etc/stunnel/certs/server.crt
verifyChain = yes
checkHost = <SERVER_HOSTNAME>
```

```shell
sudo stunnel /etc/stunnel/iiod.conf
```

````

````{tab} macOS

`/usr/local/etc/stunnel/iiod.conf`:
```ini
foreground = yes

[iiod]
client = yes
accept = 127.0.0.1:30431
connect = <SERVER_IP_OR_HOSTNAME>:30432
CAfile = /usr/local/etc/stunnel/certs/server.crt
verifyChain = yes
checkHost = <SERVER_HOSTNAME>
```

```shell
stunnel /usr/local/etc/stunnel/iiod.conf
```

````

````{tab} Windows

`C:\Program Files\stunnel\config\iiod.conf`:
```ini
[iiod]
client = yes
accept = 127.0.0.1:30431
connect = <SERVER_IP_OR_HOSTNAME>:30432
CAfile = C:\Program Files\stunnel\config\certs\server.crt
verifyChain = yes
checkHost = <SERVER_HOSTNAME>
```

Run stunnel from the Start Menu shortcut, or as a service the same way as described for the server.

````

`verifyChain = yes` together with `CAfile` and `checkHost` make the client verify that it is really talking to the expected server, instead of blindly trusting any TLS endpoint.

## Step 4: Connect with libiio

With both stunnel processes running, point any libiio application at `127.0.0.1` exactly as if `iiod` were running locally — the client-side stunnel transparently forwards the traffic over TLS:

```shell
iio_info -u ip:127.0.0.1
```

```shell
iio_readdev -u ip:127.0.0.1 -b 1024 <device> <channel>
```

## Connecting to multiple servers

Each remote server needs its own local port on the client. Add one `[section]` per server to the client's `iiod.conf`:

```ini
[server1]
client = yes
accept = 127.0.0.1:30431
connect = server1.example.com:30432
CAfile = /etc/stunnel/certs/server1.crt
verifyChain = yes

[server2]
client = yes
accept = 127.0.0.1:30441
connect = server2.example.com:30432
CAfile = /etc/stunnel/certs/server2.crt
verifyChain = yes
```

Then connect to each one using its local port:

```shell
iio_info -u ip:127.0.0.1:30431  # server1
iio_info -u ip:127.0.0.1:30441  # server2
```

## Verifying the tunnel

To check that the server is only reachable through TLS, and that the certificate is what you expect:

```shell
# Confirm iiod is bound (locally) on 30431
ss -tlnp | grep 30431

# Manually open a TLS connection to the server and inspect its certificate
openssl s_client -connect <SERVER_IP_OR_HOSTNAME>:30432 -CAfile server.crt
```
