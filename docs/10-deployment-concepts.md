# Layer 10: Deployment, from the ground up

[Layer 9](9-deploy.md) is the recipe: which commands to type. This layer explains **what each piece is, why it's there, and how it all serves your project to a browser**, starting from zero.

Read it in order the first time. Every idea builds on the ones before it, and every section ends by pointing at the file in this repo that uses it.

| Part | What it covers |
|---|---|
| A. The basics | servers, IP addresses, ports, firewalls, SSH, Linux users and folders, building on the server |
| B. Keeping it running | processes, systemd, logs, resource limits, where data lives |
| C. Serving it on the web | HTTP, DNS, HTTPS certificates, reverse proxies, passwords, WebSockets, ttyd, one keystroke's full journey |
| D. Containers | Docker images, containers, volumes, ports, and why PID 1 matters |
| E. Security | who attacks servers, and the layers that stop them |
| F. Operating it | updates, rollbacks, monitoring, automation, scaling |

---

# Part A: The basics

## 1. What "deploying" means

On your laptop, the server runs while your terminal is open and only you can reach it. **Deploying** means running it somewhere that is:

1. **always on**: a machine that doesn't sleep or shut down,
2. **reachable**: other people can connect to it over the internet,
3. **safe**: only the people and actions you intend get through,
4. **self-healing**: if it crashes or the machine reboots, it comes back by itself.

Everything in this layer answers one of three questions:
- **Where does it run?** A virtual machine in the cloud (sections 2–7).
- **How does it keep running?** systemd (sections 8–10).
- **How do people reach it, safely?** DNS, HTTPS, a reverse proxy, a password, a web terminal (sections 11–18).

## 2. Servers, virtual machines and "the cloud"

A **server** is just a computer that runs programs for other computers. Nothing about the hardware is special.

A **virtual machine (VM)** is a computer simulated in software. One big physical machine runs a **hypervisor** that splits its CPUs, memory and disk into many VMs. Each VM behaves like a separate computer with its own operating system, and can't see the others.

**"The cloud"** means renting VMs from a company that owns the physical machines: Oracle, Google, Amazon (AWS), Microsoft, DigitalOcean. You choose:

| Choice | Meaning | For mini-redis |
|---|---|---|
| **Image** | the operating system installed on first boot | Ubuntu 24.04 LTS. We need Linux because the server uses `epoll`. "LTS" = long-term support, security updates for years. |
| **Shape / instance type** | how many virtual CPUs and how much RAM | the smallest. mini-redis uses one thread and very little memory. |
| **Architecture** | x86-64 (Intel/AMD) or ARM (e.g. Oracle Ampere) | either. We compile on the VM, so the binary matches its CPU (section 7). |
| **Region** | which data centre | any. Closer to your users means lower latency. |

Free tiers exist because providers want you to learn on their platform. They're plenty for a demo.

## 3. Addresses: IPs, ports, and "who is listening"

### IP addresses
Every machine on a network has an **IP address**, like a street address. `203.0.113.10` is IPv4 (four numbers 0–255). IPv6 addresses are longer (`2001:db8::1`).
- A **public IP** is reachable from the whole internet. Your VM gets one.
- A **private IP** (`10.x.x.x`, `192.168.x.x`, `172.16–31.x.x`) only works inside one network, like your home Wi-Fi.
- **`127.0.0.1` (localhost / loopback)** always means "this same machine". Traffic to it never leaves the computer.

### Ports
One machine runs many network programs, so each one listens on a **port**: a number from 1 to 65535. The IP picks the machine; the port picks the program on it. Only one program can listen on a given IP + port.

| Port | Who uses it here | Open to the internet? |
|---|---|---|
| 22 | SSH, so you can log in | yes (key login only) |
| 80 | HTTP. Caddy uses it to prove you own the name and to redirect to HTTPS. | yes |
| 443 | HTTPS. Caddy serves the demo here. | yes |
| 6379 | mini-redis (Redis's standard port) | **no** |
| 7681 | ttyd (web terminal) | **no** |

### "Binding": which address a program listens on
When a server calls `bind()`, it chooses not only a port but also **which of the machine's addresses** to accept connections on:

| Bind address | Who can connect |
|---|---|
| `127.0.0.1` | only programs on the same machine |
| `0.0.0.0` | anyone who can reach any of the machine's addresses |
| a specific IP | only connections arriving at that address |

This is why `mini-redis-server` has `--bind` and why its default is `127.0.0.1` (see `server/net.cpp`, `create_listen_socket`). Caddy reaches ttyd on localhost, and ttyd's CLI reaches the server on localhost, so neither of those ever needs to listen publicly.

You can see exactly what is listening on a machine with:

```bash
sudo ss -ltnp     # -l listening, -t TCP, -n numeric, -p show the program
# 127.0.0.1:6379  mini-redis-server   ← localhost only: good
# 127.0.0.1:7681  ttyd                ← localhost only: good
# *:443           caddy               ← public: intended
```

This one command is the best way to check a deployment is safe.

## 4. Firewalls

A **firewall** decides which incoming connections are allowed to reach the machine at all. There are usually two:

1. **The provider's firewall** (called "security group", "security list" or "VPC firewall rules"). It's configured in the web console, and blocks traffic before it reaches your VM.
2. **The VM's own firewall** (`iptables`/`nftables`, often managed by `ufw`). It runs inside Linux.

Good practice is **default deny**: block everything inbound, then allow only what you need (22, 80, 443). Even if a program accidentally listens on `0.0.0.0`, the firewall still stops outsiders. Binding to localhost *and* having a firewall are two independent locks. If one fails, the other still holds.

Oracle's Ubuntu images ship with strict `iptables` rules, which is why Layer 9 has an extra step there.

## 5. SSH: your remote control

**SSH** (Secure Shell) gives you an encrypted terminal on a remote machine. Everything you do on the VM goes through it.

**Key pairs** instead of passwords:
- You have two files: a **private key** (secret, stays on your laptop) and a **public key** (safe to share, goes on the server in `~/.ssh/authorized_keys`).
- When you connect, the server sends a challenge that only the private key can answer. The private key never travels over the network.
- Keys can't be guessed the way passwords can. Bots try millions of passwords against port 22 every day; they get nowhere against key-only logins.

```bash
ssh -i ~/.ssh/my-vm-key ubuntu@203.0.113.10
```
`-i` = which private key to use; `ubuntu` = the user name on the VM.

**SSH tunnels** (Layer 9, section 4) reuse this encrypted connection to carry *other* traffic. `-L 6379:127.0.0.1:6379` makes port 6379 on your laptop lead to port 6379 on the VM's localhost. That way you can use a private server without ever opening its port.

## 6. Linux for servers: users, permissions, folders, packages

### Users and permissions
Every process runs **as some user**, and can only do what that user is allowed to do.
- **root** can do anything. Running a network service as root means a single bug could hand an attacker the whole machine.
- **`sudo`** lets your normal user run one command as root, when you need it.
- A **system user** is an account for a program, not a person. We create `miniredis`:
  ```bash
  sudo useradd --system --no-create-home --shell /usr/sbin/nologin miniredis
  ```
  `--system`: a service account. `--no-create-home`: it needs no home folder. `--shell /usr/sbin/nologin`: nobody can log in as it.

Both `mini-redis-server` and `ttyd` run as `miniredis`. They can write to `/var/lib/mini-redis` and nowhere else (systemd's sandboxing enforces that, section 8). This is the **principle of least privilege**: give each program only the access it needs.

### Where things go
Linux has conventions (the *Filesystem Hierarchy Standard*) so everyone knows where to look:

| Path | Holds | Ours |
|---|---|---|
| `/usr/local/bin` | programs you installed yourself (not from apt) | `mini-redis-server`, `mini-redis-cli` |
| `/usr/bin` | programs from the package manager | `ttyd`, `caddy` |
| `/etc` | configuration | `/etc/caddy/Caddyfile` |
| `/etc/systemd/system` | service definitions you add | `mini-redis.service`, `mini-redis-web.service` |
| `/var/lib/<name>` | a service's persistent data | `/var/lib/mini-redis/appendonly.aof` |
| `/var/log` and the journal | logs | read with `journalctl` (section 8) |

### Packages and repositories
`apt install ttyd` downloads a ready-made program from Ubuntu's **package repository**, checks its signature, and installs it with its dependencies. `apt upgrade` installs security fixes.

For Caddy we add **Caddy's own repository**, because Ubuntu's copy is older. That's what these lines do:
```bash
curl ... gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg   # trust Caddy's signing key
curl ... debian.deb.txt | sudo tee /etc/apt/sources.list.d/caddy-stable.list                    # add the repository
```
The **GPG key** lets apt verify that every package really comes from the Caddy team and wasn't tampered with.

## 7. Building and installing on the server

There are two ways to get a program onto a server:
1. **Build it there** from source (what we do): `git clone`, `make`.
2. **Copy a ready binary** built elsewhere (what Docker images and package repositories do).

We build on the VM because a compiled program only runs on the CPU **architecture** it was built for. A binary from your x86 laptop won't run on an ARM VM. Building on the target removes that problem.

We also run `make test` on the VM. The end-to-end tests start a real server and talk to it over real TCP, so passing them proves *this* machine's compiler, kernel and network stack all work with the code.

Then:
```bash
sudo cp bin/mini-redis-server bin/mini-redis-cli /usr/local/bin/
```
This installs the binaries. The service runs the copies in `/usr/local/bin`, not the ones in your git folder, so you can rebuild without disturbing the running server until you choose to restart it.

---

# Part B: Keeping it running

## 8. Processes, services and systemd

### The problem
If you SSH in and run `./mini-redis-server`, it runs **inside your SSH session**. When you disconnect, the session ends, the kernel sends the process **SIGHUP** ("hang up"), and it dies. It also won't start again after a reboot or a crash.

A program that runs in the background, independent of any login, is a **service** (or **daemon**).

### systemd
**systemd** is the first program Linux starts at boot (it's **PID 1**, process number 1), and it manages every service on the machine. You describe a service in a **unit file**, and systemd starts it, watches it, restarts it, and collects its logs.

Here is `deploy/mini-redis.service`, line by line:

```ini
[Unit]
Description=mini-redis in-memory data store
After=network.target
```
- `[Unit]`: general information.
- `After=network.target`: start only once networking is up, because we need to bind a socket.

```ini
StartLimitIntervalSec=120
StartLimitBurst=5
```
A circuit breaker for `Restart=` (below): if the service has to be started more than 5 times within 120 seconds, systemd gives up and marks it `failed` instead of retrying forever. Section 9 shows the case that needs it.

```ini
[Service]
Type=simple
```
`simple`: the program itself is the service. It stays in the foreground and systemd watches that process. Our server never "forks into the background", so this fits.

```ini
User=miniredis
WorkingDirectory=/var/lib/mini-redis
```
Run as the unprivileged user (section 6), starting in the data folder.

```ini
ExecStart=/usr/local/bin/mini-redis-server --bind 127.0.0.1 --port 6379 --aof-file /var/lib/mini-redis/appendonly.aof
```
The exact command to run. systemd needs full paths.

```ini
KillSignal=SIGTERM
```
`systemctl stop` sends SIGTERM. Our server catches it (`install_signal_handlers` in `server/server.cpp`): the handler sets a flag, the event loop exits, and `shutdown()` flushes and fsyncs the AOF. **Stopping the service never loses acknowledged writes.** If the process doesn't exit within 90 seconds, systemd escalates to SIGKILL, which can't be caught.

```ini
MemoryMax=256M
```
A memory cap (section 9).

```ini
Restart=on-failure
RestartSec=2
```
If the process exits with an error or is killed by a signal, start it again after 2 seconds. On restart the server replays the AOF, so the data comes back.

```ini
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/mini-redis
ProtectHome=yes
PrivateTmp=yes
...
```
**Sandboxing.** Running as `miniredis` already limits what the process may do. These lines make systemd build a tighter box around it, using the same kernel features containers use:
- `NoNewPrivileges`: the process can never gain more rights (no `setuid` tricks).
- `ProtectSystem=strict` + `ReadWritePaths=`: the whole filesystem is **read-only** for it, except its data folder.
- `ProtectHome`: `/home` and `/root` are invisible.
- `PrivateTmp`, `PrivateDevices`: its own empty `/tmp`, and no access to hardware devices.
- `ProtectKernel*`, `ProtectClock`, `RestrictNamespaces`, `SystemCallFilter=@system-service`, `CapabilityBoundingSet=` ...: it can't change kernel settings, load modules, set the clock, create containers or use unusual system calls, and holds no special capabilities.
- `RestrictAddressFamilies=`: it may only open the kinds of sockets it needs.

`systemd-analyze security mini-redis` scores this: the unit without these lines scored **9.2 UNSAFE**, with them **1.5 OK** (measured). Tested: inside this sandbox, writing to `/usr/local/bin` fails with "Read-only file system", `/home` can't be read, and the data folder works normally (including `REWRITEAOF`).

```ini
[Install]
WantedBy=multi-user.target
```
`multi-user.target` is the normal running state of a server. "Wanted by" it means `systemctl enable` starts this service at every boot.

### The commands
```bash
sudo systemctl daemon-reload              # re-read unit files after adding or editing one
sudo systemctl enable --now mini-redis    # start at boot (enable) and right now (--now)
systemctl status mini-redis               # running? since when? last log lines
sudo systemctl restart mini-redis         # stop (SIGTERM) + start
sudo systemctl stop mini-redis
```

### Logs: journald
Whatever a service prints to stdout or stderr, systemd stores in its **journal**:
```bash
journalctl -u mini-redis          # all logs of this unit
journalctl -u mini-redis -f       # follow live, like tail -f
journalctl -u mini-redis --since "10 min ago"
```

**A detail from our code that matters here.** When stdout is a terminal, C prints each line immediately. When stdout is a pipe (which it is under systemd), C collects output in 4 KB blocks, so log lines could appear minutes late, or be lost in a crash. That's why `server/main.cpp` starts with:
```cpp
setvbuf(stdout, nullptr, _IOLBF, 0);   // flush at every newline
```

### The second service
`deploy/mini-redis-web.service` runs ttyd the same way. It adds:
```ini
After=network.target mini-redis.service
Wants=mini-redis.service
```
These lines start the web terminal after the server, and pull the server in when the terminal starts. Why `Wants` and not `Requires`? With `Requires`, stopping the server (or the server ending up `failed`) also stopped the terminal, and starting the server again did **not** bring the terminal back (tested), so visitors got "502 Bad Gateway" until someone noticed. With `Wants`, the terminal stays up: while the server is down, visitors see the CLI's "Could not connect" message, and it works again the moment the server is back.

It also gets the same sandbox as the server, plus ttyd's `-O` flag (section 17). One sandbox detail was learned the hard way: ttyd needs the `AF_NETLINK` socket type to turn `-i 127.0.0.1` into a network address. Without it, ttyd doesn't fail: it silently listens on **every** interface instead (tested), which is why that family is allowed for this service.

## 9. Resource limits (cgroups)

Real Redis has `maxmemory`: past a limit, it evicts keys or refuses writes. **mini-redis doesn't have this yet**, so with a public demo, one visitor could run `RPUSH` in a loop until the VM runs out of memory. When that happens, Linux's out-of-memory killer picks a process to kill, and it might pick SSH or Caddy.

`MemoryMax=256M` uses a kernel feature called **cgroups** (control groups), which limit the CPU, memory or I/O of a group of processes. If mini-redis exceeds 256 MB, only *it* is killed. `Restart=on-failure` brings it back, and the rest of the machine never notices. It's a safety net that fixes the problem from outside the program.

One catch, found by testing it: if the AOF itself holds more data than the cap, replaying it on restart hits the limit again. Without a limit on restarts, that's an endless loop: killed during replay, restarted, killed again (9 restarts in 40 seconds in the test, never answering a single request, and burning CPU the whole time). `StartLimitBurst=5` (section 8) turns that into a clear `failed` state after 5 attempts. Layer 9's troubleshooting table has the recovery command: delete the AOF (or raise the limit), then `systemctl reset-failed` before starting again, because systemd otherwise keeps refusing to start a unit that hit its start limit.

The real fix is inside the program: a `maxmemory` setting that **refuses writes** before the cap is reached, like Redis's `noeviction` policy. Then the kernel cap would never be hit.

## 10. Where the data lives, and backups

- The AOF is at `/var/lib/mini-redis/appendonly.aof`, owned by `miniredis`.
- It survives service restarts, crashes and reboots (Layer 1 explains fsync and durability).
- It does **not** survive deleting the VM or a disk failure. That's what **backups** are for: copies kept somewhere else.

A simple backup for this project:
```bash
mini-redis-cli REWRITEAOF                                   # compact first: smallest, cleanest file
sudo cp /var/lib/mini-redis/appendonly.aof ~/backup-$(date +%F).aof
scp ubuntu@203.0.113.10:backup-*.aof .                      # from your laptop: copy it off the VM
```
Restore it by stopping the service, copying the file back to `/var/lib/mini-redis/` (owned by `miniredis`), and starting the service. Cloud providers also offer **disk snapshots**, which copy the whole disk at a point in time.

For the public demo, losing data doesn't matter. In a real system, "how do we restore?" is the first question to answer, *before* anything goes wrong.

---

# Part C: Serving it on the web

## 11. HTTP in one page

A browser talks to web servers with **HTTP**. Each exchange is a **request** and a **response**:

```
GET / HTTP/1.1                 ← request: method, path, version
Host: 203-0-113-10.sslip.io    ← which site (one server can host many)
Authorization: Basic ZGVtbzpw… ← credentials (section 15)

HTTP/1.1 200 OK                ← response: status code
Content-Type: text/html
<html>…the ttyd page…</html>
```

A URL breaks down like this:
```
https://203-0-113-10.sslip.io:443/
└─┬─┘   └────────┬────────┘ └┬┘ └ path
scheme       host name      port (443 is the default for https, so it's omitted)
```

HTTP follows a request–response pattern: the browser asks, the server answers. That's a problem for a terminal, where the server must push output whenever the program prints something. Section 16 covers the solution.

## 12. DNS: turning names into IP addresses

People and certificates use **names**, but connections need **IP addresses**. **DNS** (Domain Name System) is the internet's phone book: you ask "what's the IP of `example.com`?" and get an answer like `93.184.215.14`.

If you own a domain, you add an **A record** (name → IPv4 address) in your registrar's DNS settings: `redis-demo.yourname.dev → 203.0.113.10`.

**sslip.io** is a free DNS service with a trick: any name that *contains* an IP resolves to that IP.
```
203-0-113-10.sslip.io  →  203.0.113.10
```
Nobody has to configure anything. The IP is in the name. That gives you a real host name, which HTTPS needs (section 13), without buying a domain.

```bash
dig +short 203-0-113-10.sslip.io     # prints 203.0.113.10
```

## 13. HTTPS, TLS and certificates

Plain HTTP sends everything as readable text. Anyone between the browser and the server (public Wi-Fi, an ISP) could read your password or change the page.

**HTTPS** is HTTP inside **TLS**, which gives you:
1. **Encryption**: nobody in between can read the traffic.
2. **Integrity**: nobody can change it without being detected.
3. **Identity**: the browser can check it's really talking to `203-0-113-10.sslip.io`, not an impostor.

Identity comes from a **certificate**: a file that says "this public key belongs to this host name", signed by a **Certificate Authority (CA)** that browsers trust.

**Let's Encrypt** is a free CA. It issues certificates automatically through a protocol called **ACME**:
1. Caddy asks Let's Encrypt for a certificate for `203-0-113-10.sslip.io`.
2. Let's Encrypt replies: "prove you control that name. Serve this random token at `http://203-0-113-10.sslip.io/.well-known/acme-challenge/…`" (the **HTTP-01 challenge**), or answer a similar challenge on port 443.
3. Let's Encrypt connects to that name, which DNS resolves to your VM, and checks the token.
4. It works, so the certificate is issued. It's valid for 90 days, and Caddy renews it automatically well before it expires.

That's why ports **80 and 443 must be open**: Let's Encrypt has to reach your server to verify it. It's also why you need a **name** and not a bare IP for a normal certificate.

**Caddy does all of this with zero configuration.** Writing a host name in the Caddyfile is enough. With other web servers (nginx, Apache), you'd set up a separate tool like certbot.

## 14. Reverse proxies

A **reverse proxy** is a server that sits in front of your application, receives every request from the internet, and forwards it to the application behind it.

```
                                ┌─ /        → landing page (a file on disk)
internet ──▶ Caddy (:443) ──────┤
                                └─ /try/... → ttyd (127.0.0.1:7681, private)
```

Why not let browsers talk to ttyd directly?
- **One public entry point.** Only Caddy faces the internet. Everything else stays on localhost, which is easy to secure and to change.
- **TLS in one place.** Caddy handles certificates and encryption ("TLS termination"), so ttyd doesn't need to know anything about them.
- **Access control.** Caddy checks the password before a request reaches ttyd.
- **Routing.** One site serves different things at different paths: the public landing page at `/`, the password-protected terminal at `/try/`.

(A *forward* proxy acts for clients, such as a company proxy for its employees' browsing. A *reverse* proxy acts for servers.)

In short, `deploy/Caddyfile` says: for this host name, compress responses; send `/try` to `/try/`; under `/try/`, require the password and forward to ttyd; everything else, serve files from `/var/www/mini-redis`. [Layer 11](11-caddy.md) explains Caddy and walks through that file line by line.

## 15. Passwords: HTTP Basic authentication

**Basic auth** is the simplest login built into HTTP:
1. The browser requests the page without credentials.
2. Caddy answers `401 Unauthorized`, and the browser shows its own login box.
3. The browser sends the request again with the header `Authorization: Basic <base64 of "demo:password">`, and repeats it on every request afterwards.

Base64 is an **encoding, not encryption**. Anyone who sees the header can decode it. So basic auth is only safe over **HTTPS**, where TLS encrypts the header. Here, Caddy only serves HTTPS.

The Caddyfile stores a **bcrypt hash** (`caddy hash-password`), not the password itself. A hash is one-way: Caddy can check a password against it but can't recover the password from it. If someone read your config file, they still wouldn't know the password. bcrypt is deliberately slow, which makes guessing many passwords expensive.

Its limits: everyone shares one password, there's no logout button, and no lockout after wrong guesses. That's fine for a demo. Real applications use login pages and per-user accounts.

## 16. WebSockets: a two-way pipe through HTTP

A terminal needs both directions at any time: keystrokes go up, and output comes down whenever the program prints something. Plain HTTP requests can't do that.

A **WebSocket** starts as a normal HTTP request asking to "upgrade":
```
GET /try/ws HTTP/1.1
Upgrade: websocket
Connection: Upgrade
```
The server replies `101 Switching Protocols`. From then on, the same TCP connection carries **messages in both directions** for as long as it's open. Over HTTPS, it's encrypted like everything else (`wss://`).

Caddy's `reverse_proxy` handles WebSocket upgrades automatically. Some other proxies (older nginx configs) need extra lines for this.

## 17. ttyd and pseudo-terminals

### What a "terminal" is to a program
`mini-redis-cli` reads from **stdin** and writes to **stdout**. When you run it in a terminal window, both are connected to a **TTY**, a terminal device. Programs can ask "am I talking to a terminal?" with `isatty()`.

A **pseudo-terminal (PTY)** is a terminal device made in software. It has two ends: the program sees a normal terminal on one side, and another program (a terminal emulator, SSH, or ttyd) reads and writes the other side.

### What ttyd does
For each browser that connects, ttyd:
1. creates a new PTY,
2. starts **a new `mini-redis-cli` process** attached to it,
3. relays bytes between that PTY and the browser over a WebSocket.

In the browser, a JavaScript terminal emulator (**xterm.js**) draws the characters and sends your keystrokes.

**A tie-in with our code:** `client/main.cpp` only prints the `127.0.0.1:6379>` prompt when `isatty(STDIN_FILENO)` is true. Under ttyd, stdin is a PTY, so it *is* a terminal. The prompt appears in the browser exactly as it does on your laptop. When you pipe commands in (`echo PING | mini-redis-cli`), stdin is a pipe, so no prompt is printed. The same code behaves correctly in both places.

### Why the CLI and not a shell
ttyd can serve any program. If it served `bash`, every visitor could run any command as the `miniredis` user. Serving `mini-redis-cli` means the only thing a visitor can do is send Redis commands. `quit` just ends their CLI process, and ttyd closes that session.

The flags in `deploy/mini-redis-web.service`:

| Flag | Meaning |
|---|---|
| `-i 127.0.0.1` | listen on localhost only, so only Caddy can reach it |
| `-p 7681` | port |
| `-W` | writable: visitors can type (ttyd 1.7+ is read-only by default) |
| `-O` | check origin: refuse WebSocket connections opened by pages from another website (see below) |
| `-m 10` | at most 10 sessions at once, which limits how many CLI processes a crowd can start |
| `-b /try` | base path: the terminal lives at `/try/` on the site, so every URL ttyd serves (page, `/try/token`, `/try/ws`) carries that prefix ([Layer 11](11-caddy.md#5-our-caddyfile-line-by-line) explains why) |
| `-t titleFixed=…`, `-t fontSize=16` | browser tab title and font size |

### Why `-O`: cross-site WebSocket hijacking
After you log in once, your browser remembers the password for that site and **re-sends it automatically**, including on WebSocket connections that *another* website's JavaScript opens to your site. Without a check, a malicious page that a logged-in visitor opens could connect to `wss://your-site/try/ws` and type commands in the visitor's name. This attack is called **cross-site WebSocket hijacking**.

Every browser WebSocket request carries an `Origin` header naming the page that opened it. With `-O`, ttyd compares it with the `Host` header and refuses a mismatch. That works behind Caddy because Caddy passes the original `Host` through ([Layer 11](11-caddy.md)). Tested both ways: a connection claiming `Origin: https://evil.example`, with the right password, could write data without `-O`, and was refused with it, while normal visitors still got in.

### Watch out: Ubuntu's ttyd package has its own service
`apt install ttyd` also installs and starts `ttyd.service`, which serves a **root** `login` prompt on port 7681. It only listens on localhost, but it takes the port our service needs and is a root login nobody asked for. Layer 9, step 4 disables it right after installing (`sudo systemctl disable --now ttyd`).

## 18. The full journey of one keystroke

A visitor opens the landing page (Caddy serves it as a file), clicks **Open the live terminal**, logs in, and the browser opens a WebSocket to `/try/ws`. Now they type `GET name` and press Enter:

```
 Browser                Caddy                 ttyd              mini-redis-cli         mini-redis-server
 (xterm.js)             :443 public           127.0.0.1:7681    (in a PTY)             127.0.0.1:6379
    │  keystrokes          │                     │                  │                       │
    │══ WebSocket/TLS ════▶│ decrypt, check      │                  │                       │
    │                      │ password ──────────▶│ write to PTY ───▶│ getline → split_args  │
    │                      │                     │                  │ encode RESP           │
    │                      │                     │                  │──── TCP ─────────────▶│ epoll → parse →
    │                      │                     │                  │                       │ execute → reply
    │                      │                     │                  │◀─── "$5\r\nAlice\r\n" │
    │                      │                     │◀ "\"Alice\"\n" ──│ ReplyReader →         │
    │◀═ WebSocket/TLS ═════│◀ encrypt ───────────│                  │ format_reply          │
    │ draws "Alice"        │                     │                  │                       │
```

1. **xterm.js** turns each keystroke into a WebSocket message.
2. It travels over **TLS** to the VM's port 443. DNS (sslip.io) turned the name into the IP before the connection was made.
3. **Caddy** decrypts it. The path `/try/ws` matched the terminal route, and the WebSocket was only allowed to open after the password check succeeded. Caddy forwards the bytes to `127.0.0.1:7681`.
4. **ttyd** writes them into that visitor's **PTY**.
5. **mini-redis-cli** reads the line (`std::getline`), splits it (`split_args`), encodes it as RESP (`encode_command`), and sends it over TCP to `127.0.0.1:6379`.
6. **mini-redis-server**'s `epoll_wait` wakes up. The server reads into the connection's input buffer, parses, runs `GET`, and writes `$5\r\nAlice\r\n`.
7. The CLI's `ReplyReader` decodes it, `format_reply` turns it into `"Alice"`, and it prints that to stdout, which is the PTY.
8. ttyd reads the PTY and sends the text back through the WebSocket. Caddy encrypts it, and xterm.js draws it.

The whole round trip is a few milliseconds, most of it network distance. The server's own work is microseconds.

---

# Part D: Containers

## 19. Docker

### The idea
"It works on my machine" happens because machines differ: compiler versions, libraries, settings. A **container** packages a program *with* everything it needs, so it runs the same way everywhere.

- An **image** is a read-only template: a minimal filesystem plus your program. It's built from a **Dockerfile**.
- A **container** is a running instance of an image. You can run many containers from one image.
- Containers aren't VMs. They share the host's Linux kernel, but each gets its own isolated view of files, processes and network (**namespaces**), plus resource limits (**cgroups**, the same feature as `MemoryMax`). This makes them start in milliseconds and use little memory.

### Our Dockerfile, line by line
```dockerfile
FROM ubuntu:24.04 AS build
```
Stage 1 starts from the official Ubuntu image and is named `build`.

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends g++ make \
    && rm -rf /var/lib/apt/lists/*
```
Install the compiler. `--no-install-recommends` skips optional extras, and deleting the apt lists keeps the layer smaller.

```dockerfile
WORKDIR /src
COPY . .
RUN make -j"$(nproc)"
```
Copy the source code in (minus what `.dockerignore` excludes) and build, using every CPU core.

```dockerfile
FROM ubuntu:24.04
COPY --from=build /src/bin/mini-redis-server /src/bin/mini-redis-cli /usr/local/bin/
```
Stage 2 is a **fresh, clean image**, and only the two finished binaries are copied in from stage 1. The compiler, source code and object files are left behind. This is a **multi-stage build**: the final image is much smaller, and it contains no build tools an attacker could use.

```dockerfile
RUN useradd --system --no-create-home --shell /usr/sbin/nologin miniredis \
    && mkdir /data && chown miniredis: /data
USER miniredis
```
Containers run as **root** unless told otherwise. `USER` switches to an unprivileged account for everything after it, including the server, the same principle as the systemd setup (section 6). `/data` is created and handed to that user *before* the `VOLUME` line, because Docker copies the folder's ownership into a new named volume. (A volume created by an older, root-run image keeps root-owned files: Layer 9 has the one-line `chown` fix.)

```dockerfile
WORKDIR /data
VOLUME /data
```
The AOF is written in `/data`, and declaring it a **volume** tells Docker that data there must outlive the container (see below).

```dockerfile
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s \
    CMD ["mini-redis-cli", "PING"]
```
Every 30 seconds Docker runs `mini-redis-cli PING` inside the container. `docker ps` then shows `healthy` or `unhealthy`, and orchestrators (Docker Compose, Swarm) can use that to restart or route around a broken container. A process can be running but stuck; a health check asks "does it actually answer?".

```dockerfile
EXPOSE 6379
ENTRYPOINT ["mini-redis-server", "--bind", "0.0.0.0", "--aof-file", "/data/appendonly.aof"]
```
`EXPOSE` documents the port. `ENTRYPOINT` is the command the container runs.

### Layers and caching
Each instruction creates a **layer**, and Docker reuses unchanged layers on the next build. The `apt-get install` layer is cached, so rebuilding after a code change only re-runs `COPY` and `make`.

### Volumes: keeping data
A container's own filesystem is thrown away when the container is removed. A **volume** is storage managed by Docker that lives outside the container:
```bash
docker run ... -v mini-redis-data:/data mini-redis
```
This mounts the volume `mini-redis-data` at `/data`. Remove the container, start a new one with the same `-v`, and the AOF (and your data) is still there.

### Ports, and why we bind 0.0.0.0 inside
Each container has its **own network namespace**, including its own `127.0.0.1`. If the server bound to `127.0.0.1` inside the container, it would only accept connections from *inside that container*, and Docker's port forwarding couldn't reach it. So the Dockerfile uses `--bind 0.0.0.0`, which here means "every interface *of the container*".

Who can reach it from outside is then decided by **publishing**:
```bash
-p 127.0.0.1:6379:6379    # host's localhost:6379 → container's 6379   (safe)
-p 6379:6379              # every host interface → container           (exposed!)
```

### Why PID 1 matters, and why our server handles it
Inside a container, your program is usually **PID 1**. Linux treats PID 1 specially: **signals it has no handler for are ignored**, instead of using the default action (which for SIGTERM is "terminate"). Many programs therefore ignore `docker stop`, and Docker kills them with SIGKILL after 10 seconds, with no chance to save data.

Our server installs a SIGTERM handler (`install_signal_handlers`), so `docker stop` triggers the same graceful shutdown as Ctrl+C: flush the AOF, fsync, exit. This is the same code that makes `systemctl stop` safe.

## 20. Docker or systemd?

| | systemd on the VM | Docker |
|---|---|---|
| Install | build on the VM, copy binaries | `docker build`, `docker run` |
| Isolation | Linux user + cgroup limits | full namespaces (own files, processes, network) |
| Portability | tied to that machine's setup | the same image runs anywhere |
| Restart on crash / boot | `Restart=`, `enable` | `--restart unless-stopped` |
| Logs | `journalctl -u mini-redis` | `docker logs mini-redis` |
| Best for | one small VM, learning how Linux works | shipping the same build to many places |

They also combine: systemd can run Docker containers, and Kubernetes runs containers across many machines. For this project, the systemd path teaches more about Linux. The Docker path is the quickest way for a reviewer to try it.

---

# Part E: Security

## 21. Who attacks a small server, and how we stop them

You don't need to be famous to be attacked. Automated **bots scan every public IP address** on the internet, all day, for open ports and known weaknesses. A new VM sees SSH login attempts within minutes.

**Redis specifically:** for years, many Redis servers were left open on port 6379 with no password. Attackers used Redis's own `CONFIG SET dir` / `dbfilename` commands to write files onto the server, for example their own SSH key into `authorized_keys`, and took over the machine. That's why Redis 3.2 added **protected mode**: with no password set, it refuses connections from anything other than localhost. (mini-redis has no `CONFIG SET`, so that exact attack doesn't work, but an open port would still let anyone read or delete all the data.)

**Defense in depth** means several independent layers, so one mistake doesn't expose everything:

| Layer | What it does here | Where |
|---|---|---|
| Network | the firewall only allows 22, 80, 443 | provider console, iptables |
| Binding | mini-redis and ttyd listen on localhost only | `--bind 127.0.0.1`, `ttyd -i 127.0.0.1` |
| Transport | everything public is HTTPS (TLS) | Caddy |
| Access | a password (bcrypt hash) before anything reaches ttyd | `basic_auth` |
| Origin | other websites can't open the terminal through a logged-in visitor's browser | `ttyd -O` |
| Application | visitors get the Redis CLI, not a shell | ttyd runs `mini-redis-cli` |
| Capacity | at most 10 sessions | `ttyd -m 10` |
| Resources | the server's memory is capped at 256 MB | `MemoryMax=256M` |
| OS | services run as `miniredis`: no login, no home, minimal rights | `useradd --system ... nologin` |
| Sandbox | read-only filesystem except the data folder, no privileges, restricted system calls | systemd `Protect*=` and friends (1.5 OK) |
| Login | SSH accepts keys only | provider default |

**What's still weak**, and fine to say in an interview:
- One shared password with no lockout after wrong guesses.
- No per-visitor data isolation: everyone shares one database.
- No rate limiting on commands. A visitor can still send a lot of commands, just within the memory cap.

## 22. Keeping it patched

Most break-ins use **known** bugs that already have fixes. Keep the VM updated:
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y unattended-upgrades     # Ubuntu then installs security updates automatically
```
Reboot when a kernel update asks for it (`/var/run/reboot-required` exists). Both services come back by themselves, because they're enabled in systemd.

---

# Part F: Operating it (advanced)

## 23. Releasing a new version, and rolling back

A **deployment** of a new version is: get the new code, build, test, install, restart.
```bash
cd ~/mini-Redis && git pull && make && make test \
  && sudo cp bin/mini-redis-* /usr/local/bin/ \
  && sudo systemctl restart mini-redis mini-redis-web
```
The `&&` chain stops at the first failure, so a version that doesn't build or doesn't pass its tests never gets installed.

**Downtime:** the restart takes a moment. SIGTERM, flush, exit, start, then replay the AOF. Clients connected at that moment are disconnected. For a demo that's fine. Real systems avoid it with replicas: update one copy while another keeps serving.

**Rollback** means going back to the last good version when a new one misbehaves. Use git tags to name releases:
```bash
git tag v1.0 && git push origin v1.0        # when a version is good
git checkout v1.0 && make && make test && sudo cp bin/mini-redis-* /usr/local/bin/ && sudo systemctl restart mini-redis
```
The AOF format hasn't changed between versions, so older and newer servers read the same file.

## 24. Monitoring: knowing it's healthy

| Question | How to answer it |
|---|---|
| Are the services running? | `systemctl status mini-redis mini-redis-web caddy` |
| Is the server answering? | `mini-redis-cli PING` → `PONG` (a **health check**) |
| What happened? | `journalctl -u mini-redis --since today` |
| What's listening, and where? | `sudo ss -ltnp` |
| CPU and memory? | `top` or `htop`; `systemctl status` shows the service's memory against `MemoryMax` |
| Disk space? | `df -h` (a full disk makes AOF writes fail) |
| Is it reachable from outside? | a free uptime monitor (e.g. UptimeRobot) that requests your HTTPS URL every few minutes and emails you if it fails |

A health check could also run on a schedule, restarting the service if it stops answering:
```bash
# sudo crontab -e   (root's schedule, so no sudo prompt is needed; runs every 5 minutes)
*/5 * * * * /usr/local/bin/mini-redis-cli PING > /dev/null || systemctl restart mini-redis
```

## 25. Automation: CI/CD and infrastructure as code

So far, every step is manual. Teams automate both halves:

- **CI (continuous integration)** is what `.github/workflows/ci.yml` already does: every push is built and tested, normally and with sanitizers.
- **CD (continuous deployment)** would add a final job: when CI passes on `main`, connect to the VM over SSH (with a key stored as a GitHub secret) and run the release commands from section 23. Every merged change then goes live automatically, and only if its tests passed.
- **Infrastructure as code** describes the VM itself in files, so it can be re-created exactly:
  - **cloud-init**: a script the provider runs on first boot (install packages, create the user, start the services).
  - **Ansible**: applies the same setup steps to one or many servers over SSH.
  - **Terraform**: creates the VM, firewall rules and DNS records through the provider's API.

For this project, the manual steps in Layer 9 are the right size. Knowing what the next level looks like is what matters in an interview.

## 26. Scaling: what if it had to handle much more?

- **Vertical scaling** means a bigger machine. mini-redis is single-threaded, so more CPU cores don't speed up one instance. More RAM holds more data.
- **Horizontal scaling** means more machines:
  - **Sharding**: split the keys across several instances (e.g. by a hash of the key). Each instance handles a share of the load. Redis Cluster does this with 16,384 hash slots.
  - **Replication**: copies of the data on other machines, for reading and for failover. A natural design here: stream the AOF to replicas, which replay it.
  - **Failover**: if the main instance dies, promote a replica. Redis Sentinel automates this.
- A database holds **state**, so you can't simply put a load balancer in front of several copies the way you can with a stateless web app. Every write has to reach the right place.

## 27. How Redis is deployed in the real world

- **Managed services** (AWS ElastiCache, Google Memorystore, Azure Cache for Redis, Redis Cloud): the provider runs Redis, patches it, backs it up and handles failover. Most companies use these.
- **Self-hosted**: Redis with `requirepass` or ACL users, TLS, `maxmemory` with an eviction policy, protected mode, persistence tuned per use case, and **Sentinel** or **Cluster** for availability.
- It's almost never exposed to the internet. Applications on a private network talk to it.

Comparing this list with mini-redis is a good way to explain what "production-ready" would mean for your project (Layer 9, final table).

---

## Glossary

| Term | Meaning |
|---|---|
| ACME | the protocol Let's Encrypt uses to issue certificates automatically |
| A record | DNS entry mapping a name to an IPv4 address |
| bind | choose the address and port a server listens on |
| cgroup | kernel feature that limits a group of processes' CPU, memory or I/O |
| certificate | signed file tying a public key to a host name, used by TLS |
| container | an isolated process with its own view of files, network and processes, sharing the host kernel |
| daemon / service | a background program not tied to a login session |
| DNS | the system that turns names into IP addresses |
| firewall | rules deciding which network connections may reach a machine |
| image | read-only template a container starts from |
| journald | systemd's log collector (`journalctl`) |
| localhost / 127.0.0.1 | this machine, via the loopback interface |
| PID 1 | the first process: `systemd` on a VM, often your app in a container |
| port | number identifying one listening program on a machine |
| PTY | pseudo-terminal: a terminal device made in software |
| reverse proxy | server that receives public requests and forwards them to private apps |
| SIGHUP / SIGTERM / SIGKILL | "terminal closed" / "please stop" / "stop now" (can't be caught) |
| TLS | the encryption and identity layer under HTTPS |
| unit file | systemd's description of a service |
| volume | Docker storage that outlives containers |
| WebSocket | a two-way message channel that starts as an HTTP request |

## Interview questions on deployment

**"How would you deploy this?"**
"On a small Linux VM as a systemd service, running as an unprivileged user, bound to localhost, with a memory cap and automatic restart. For a public demo, Caddy in front with automatic HTTPS and a password, forwarding to ttyd, which runs my CLI in the browser. Only ports 22, 80 and 443 are open."

**"Why bind to localhost?"**
"There's no authentication, and bots scan for open Redis ports constantly. Real Redis has protected mode for exactly this reason. Everything that needs the server is on the same machine, so nothing else should reach it."

**"What happens when you run `systemctl stop`?"**
"systemd sends SIGTERM. My handler only sets a flag. The epoll loop wakes with EINTR, sees the flag, exits, and shutdown flushes and fsyncs the AOF. If it hung for 90 seconds, systemd would escalate to SIGKILL."

**"Why does your server work correctly with `docker stop` when many programs don't?"**
"In a container the app is PID 1, and the kernel ignores signals PID 1 has no handler for. My server installs a SIGTERM handler, so it shuts down gracefully instead of being killed after the timeout."

**"How does HTTPS get set up?"**
"Caddy uses ACME with Let's Encrypt: it proves control of the host name by answering a challenge on port 80 or 443, gets a 90-day certificate, and renews it automatically. I use sslip.io so the host name resolves to the VM's IP without buying a domain."

**"How would you scale it?"**
"It's single-threaded, so one instance won't use more cores. I'd shard keys across instances and add replicas fed by the AOF for reads and failover. It's stateful, so you can't just load-balance across copies."

---

Previous: [Layer 9: Running and deploying](9-deploy.md) (the commands) · Back to the [docs index](README.md)
