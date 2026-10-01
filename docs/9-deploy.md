# Layer 9: Running and deploying

This layer is the recipe. For what each piece is and why it's there (ports, systemd, DNS, HTTPS, reverse proxies, ttyd, Docker), read [Layer 10: Deployment concepts](10-deployment-concepts.md).

This guide goes from simplest to most complete:

| Section | What you get |
|---|---|
| 1. Local | run it on your own Linux/WSL machine |
| 2. Docker | run it anywhere Docker runs, one command |
| 3. systemd | a server on a VM that starts at boot and restarts after crashes |
| 4. SSH tunnel | use that remote server safely from your laptop |
| 5. Demo checklist | showing it live in an interview |
| 6. Browser demo | a public HTTPS link where anyone with the password can use your CLI |

For reviewers of your GitHub, 1 and 2 matter most. For a link on your resume, follow 3 and then 6.

> **Security first.** mini-redis has **no password and no encryption**, like Redis without `requirepass`/TLS. Bots constantly scan the internet for open port 6379 and abuse what they find. So by default it only listens on `127.0.0.1` (this machine only). Never run it with `--bind 0.0.0.0` on a machine with a public IP unless a firewall blocks port 6379. To use a remote server, connect through an **SSH tunnel** (section 4).

---

## 1. Locally on Linux / WSL

```bash
git clone https://github.com/X-Rachit-X/mini-Redis.git
cd mini-Redis
make                                   # needs g++ (C++17) and make
./bin/mini-redis-server                # terminal 1: listens on 127.0.0.1:6379
./bin/mini-redis-cli                   # terminal 2
```

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `--port N` | 6379 | TCP port |
| `--bind IP` | 127.0.0.1 | address to listen on. `0.0.0.0` means every interface |
| `--aof-file PATH` | appendonly.aof | where data is persisted |
| `--appendfsync always\|everysec\|no` | everysec | durability vs speed |
| `--no-aof` | AOF on | memory only |

Stop the server with **Ctrl+C**: it flushes the AOF and exits cleanly. Start it again and your data is back.

The official `redis-cli` works too: `sudo apt install redis-tools`, then `redis-cli -p 6379`.

---

## 2. Docker (works on Windows, macOS and Linux)

The repo contains a two-stage `Dockerfile`. The first stage compiles with g++. The second copies only the two binaries into a clean Ubuntu image.

```bash
docker build -t mini-redis .
docker run -d --name mini-redis -p 127.0.0.1:6379:6379 -v mini-redis-data:/data mini-redis
docker exec -it mini-redis mini-redis-cli     # use the client inside the container
```

What each part means:
- `-d`: run in the background.
- `-p 127.0.0.1:6379:6379`: publish the container's port 6379 **on your machine's localhost only**. (Plain `-p 6379:6379` would expose it on every network interface.)
- `-v mini-redis-data:/data`: the AOF is stored in a Docker volume, so data survives `docker rm` and re-creating the container.
- Inside the container the server listens on `0.0.0.0`. It has to, or Docker's port forwarding couldn't reach it. Who can reach it from outside is decided by `-p`.

Stop it gracefully with `docker stop mini-redis`. Docker sends SIGTERM, our handler flushes the AOF, then it exits.

On Windows with Docker Desktop: run the same commands in PowerShell, or enable *WSL integration* in Docker Desktop settings to use `docker` inside Ubuntu.

---

## 3. As a service on a Linux server (systemd)

This is how you'd run it on a cloud VM (AWS EC2, Google Cloud, Oracle Cloud free tier, DigitalOcean, ...) so it starts at boot and restarts if it crashes.

```bash
# on the server
sudo apt install -y g++ make git
git clone https://github.com/X-Rachit-X/mini-Redis.git && cd mini-Redis && make
sudo cp bin/mini-redis-server bin/mini-redis-cli /usr/local/bin/

# a dedicated user with no login shell, and a data directory it owns
sudo useradd --system --no-create-home --shell /usr/sbin/nologin miniredis
sudo mkdir -p /var/lib/mini-redis && sudo chown miniredis: /var/lib/mini-redis

sudo cp deploy/mini-redis.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mini-redis       # start now and at every boot

systemctl status mini-redis                  # is it running?
journalctl -u mini-redis -f                  # its log output (our printf lines)
mini-redis-cli PING                          # PONG
```

Why each step:
- **Dedicated user**: if someone ever exploited a bug in the server, they'd only get the rights of `miniredis`, not root or your account.
- **`/var/lib/mini-redis`**: the conventional place for a service's data on Linux.
- **The unit file** (`deploy/mini-redis.service`): `Restart=on-failure` restarts it after a crash. `KillSignal=SIGTERM` makes `systemctl stop` trigger our graceful shutdown. It binds to `127.0.0.1`. `MemoryMax=256M` caps its memory, because mini-redis has no `maxmemory` setting of its own: past the cap the kernel stops the service instead of the whole VM running out of memory.

---

## 4. Reaching a remote server safely: SSH tunnel

Keep the server bound to `127.0.0.1` on the VM, and let SSH carry your connection:

```bash
# on your laptop
ssh -N -L 6379:127.0.0.1:6379 user@your-vm-ip    # leave this running
./bin/mini-redis-cli -p 6379                      # in another terminal: talks to the VM
```

`-L 6379:127.0.0.1:6379` means "forward my local port 6379, through the encrypted SSH connection, to 127.0.0.1:6379 *as seen from the VM*". Only someone with your SSH key can connect. This is also how people commonly reach real Redis servers.

---

## 5. Showing it to someone (demo checklist)

In an interview, a live terminal demo is the strongest option: an interviewer learns more from watching `redis-benchmark` hit your server than from any web page. For reviewers who want to try it themselves, set up the browser demo in section 6.

1. `make && ./bin/mini-redis-server` in one terminal, and keep it visible so they see the log lines.
2. In another terminal, use the official `redis-cli`. Using the official tool is the point: it proves compatibility.
3. Show persistence: write data, Ctrl+C the server, restart, read the data back.
4. Run `redis-benchmark -p 6379 -t set,get -n 200000 -c 50 -P 16 -q` live.
5. Optional: record the session with `asciinema rec` and link the recording in the README, so reviewers can watch it without installing anything.

The interview presentation (`docs/interview/index.html`) has the full script for this.

---

## 6. A public browser demo: the CLI in a web page

The goal: you send someone a link. They land on a page that explains the project and lists commands to try. One click opens a terminal (after a password) where they type into **your real `mini-redis-cli`**, running against **your real server**.

```
                         ┌─ /        → landing page (public)       /var/www/mini-redis/index.html
browser ──HTTPS──▶ Caddy ┤
                         └─ /try/... → password → ttyd 127.0.0.1:7681 → mini-redis-cli → mini-redis-server 127.0.0.1:6379
```

- **Caddy** is the web server facing the internet. It gets a free HTTPS certificate automatically, serves the landing page, and puts a password in front of the terminal. [Layer 11](11-caddy.md) explains it in full.
- **ttyd** runs a terminal program and shows it in a web page. We give it `mini-redis-cli`, **not a shell**, so visitors can only send Redis commands. They can't run anything else on your machine.
- **The landing page** (`deploy/site/index.html`) is a single static HTML file: what the project is, commands to try with copy buttons, and how it works.
- Only ports 22 (SSH), 80 and 443 are open to the internet. The Redis port (6379) and ttyd (7681) stay on localhost.

> These steps haven't been run end to end yet. Do them once on your VM, and use step 7's checks and the troubleshooting table if something doesn't match.

What you need: a cloud VM (free tiers work), about 45 minutes, and no domain name (we use `sslip.io`, explained in step 6).

### Step 1. Create the VM

Any provider works. Free options:

| Provider | Free option | Notes |
|---|---|---|
| Oracle Cloud | "Always Free" VM | Free forever. Pick the Ampere (ARM) or AMD shape. mini-redis builds on both. |
| Google Cloud | `e2-micro` in us-west1, us-central1 or us-east1 | Free tier, 1 GB RAM is plenty |
| AWS | `t2.micro` / `t3.micro` | Free for new accounts, for a limited time |

When creating it:
1. **Image:** Ubuntu 24.04 LTS.
2. **Size:** the smallest. mini-redis needs very little.
3. **SSH key:** upload your public key or let the provider generate one, and download the private key.
4. **Firewall / security group:** allow inbound **TCP 22, 80 and 443** only. Don't open 6379 or 7681.
5. Note the VM's **public IP address**. The examples below use `203.0.113.10`; replace it with yours.

### Step 2. Connect and update

From your laptop (in WSL or PowerShell):

```bash
ssh -i path/to/your-key ubuntu@203.0.113.10    # the user may be "ubuntu", "opc" or your name, depending on the provider
sudo apt update && sudo apt upgrade -y
```

**Oracle Cloud only:** its Ubuntu image has its own firewall rules that block ports 80 and 443, even after you open them in the web console. Allow them on the VM too:

```bash
sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 80 -j ACCEPT
sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 443 -j ACCEPT
sudo netfilter-persistent save
```

### Step 3. Install mini-redis as a service

This is section 3, repeated so this guide is complete:

```bash
sudo apt install -y g++ make git
git clone https://github.com/X-Rachit-X/mini-Redis.git && cd mini-Redis
make && make test                                 # build, and check it works on this machine
sudo cp bin/mini-redis-server bin/mini-redis-cli /usr/local/bin/

sudo useradd --system --no-create-home --shell /usr/sbin/nologin miniredis
sudo mkdir -p /var/lib/mini-redis && sudo chown miniredis: /var/lib/mini-redis

sudo cp deploy/mini-redis.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mini-redis
mini-redis-cli PING                               # → PONG
```

### Step 4. Put the CLI in a web page with ttyd

```bash
sudo apt install -y ttyd
ttyd --help | grep -- "-W"                        # should list "-W, --writable"
```

If `-W` isn't listed, your ttyd is older and writable by default: delete ` -W` from `deploy/mini-redis-web.service` before the next step.

```bash
sudo cp deploy/mini-redis-web.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mini-redis-web
curl -s http://127.0.0.1:7681/try/ | head -c 80; echo   # some HTML → ttyd is serving
```

What the service runs (see the comments in `deploy/mini-redis-web.service`):
`ttyd -i 127.0.0.1 -p 7681 -W -m 10 -b /try ... /usr/local/bin/mini-redis-cli`
- `-i 127.0.0.1`: only programs on the VM (Caddy) can reach it.
- `-W`: visitors can type.
- `-m 10`: at most 10 people at once.
- `-b /try`: the terminal lives at `/try/` on the website, so ttyd must expect that prefix on every URL (its page, its script, its WebSocket).
- It runs as the `miniredis` user, which has no login shell and no rights outside its data folder.

### Step 5. Install the landing page

The page is one HTML file. Caddy will serve it from `/var/www/mini-redis`, the conventional place for website files:

```bash
sudo mkdir -p /var/www/mini-redis
sudo cp deploy/site/index.html /var/www/mini-redis/
```

Optional: edit `deploy/site/index.html` first. The line *"User: demo. Password: ask me for it."* is where you decide whether to publish the password or share it on request.

### Step 6. HTTPS, the password and routing with Caddy

Install Caddy from its official repository (Ubuntu's own package is old):

```bash
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt install -y caddy
```

Pick the web address. HTTPS certificates need a host name, not a bare IP. **sslip.io** is a free service where `203-0-113-10.sslip.io` automatically points to `203.0.113.10`, so you don't need to buy a domain. (If you own one, point an `A` record at your IP and use that name instead.)

Create the password hash (it asks for the password twice and prints a hash starting with `$2a$`):

```bash
caddy hash-password
```

Edit the Caddyfile from the repo, then install it:

```bash
nano deploy/Caddyfile
#  - replace 203-0-113-10.sslip.io with YOUR IP, dots replaced by dashes
#  - replace $2a$14$REPLACE_WITH_... with the hash you just printed
sudo cp deploy/Caddyfile /etc/caddy/Caddyfile
sudo caddy validate --config /etc/caddy/Caddyfile   # "Valid configuration"
sudo systemctl reload caddy
```

Caddy now requests a certificate from Let's Encrypt. That takes a few seconds and needs ports 80 and 443 open (step 1).

What the Caddyfile does, in short: `/` serves the landing page to anyone; `/try` redirects to `/try/`; anything under `/try/` asks for the password, then goes to ttyd. [Layer 11](11-caddy.md) goes through it line by line.

### Step 7. Check every piece

Run these **on the VM** (replace the host name and password). Each line says what you should see:

```bash
H=https://203-0-113-10.sslip.io
curl -s -o /dev/null -w '%{http_code}\n' $H/                              # 200  landing page
curl -s $H/ | grep -o '<title>[^<]*'                                      # <title>mini-redis
curl -s -o /dev/null -w '%{http_code} %{redirect_url}\n' $H/try           # 308 .../try/
curl -s -o /dev/null -w '%{http_code}\n' $H/try/                          # 401  password required
curl -s -o /dev/null -w '%{http_code}\n' -u demo:WRONG $H/try/            # 401  wrong password refused
curl -s -o /dev/null -w '%{http_code}\n' -u demo:YOURPASS $H/try/         # 200  terminal page
curl -s -u demo:YOURPASS $H/try/token                                     # {"token": ""}  ttyd answers under /try
curl -s -o /dev/null -w '%{http_code}\n' http://203-0-113-10.sslip.io/    # 308  plain HTTP is redirected to HTTPS
sudo ss -ltnp | grep -E ':(6379|7681|80|443) '                            # 6379 and 7681 on 127.0.0.1 only
```

Then in a browser: open `https://203-0-113-10.sslip.io`, click **Open the live terminal**, log in as **demo**, and type:

```
127.0.0.1:6379> PING
PONG
127.0.0.1:6379> ZADD board 300 carol 100 alice 200 bob
(integer) 3
```

Try a copy button on the landing page and paste into the terminal. Type `help` for examples. `quit` ends the session; reload the page to start a new one.

### Step 8. Share it

- **Who gets the password:** you choose. Put the link and login on your resume or in the README for anyone to try, or send them only to interviewers. Visitors share one database and can see and delete each other's keys. That's fine for a demo, but don't store anything real.
- **Add it to the README**, e.g. "Live demo: https://203-0-113-10.sslip.io (user `demo`, password on request)".

### Keeping it running

| Task | Command |
|---|---|
| See if everything is up | `systemctl status mini-redis mini-redis-web caddy` |
| Server logs | `journalctl -u mini-redis -f` |
| Wipe all demo data | `mini-redis-cli FLUSHALL` |
| Compact the log file | `mini-redis-cli REWRITEAOF` |
| Deploy a new version | `cd ~/mini-Redis && git pull && make && make test && sudo cp bin/mini-redis-* /usr/local/bin/ && sudo systemctl restart mini-redis mini-redis-web` |
| Update the landing page | edit `deploy/site/index.html`, then `sudo cp deploy/site/index.html /var/www/mini-redis/` (no restart needed) |
| Change the password | `caddy hash-password`, edit `/etc/caddy/Caddyfile`, `sudo systemctl reload caddy` |
| Caddy's logs | `journalctl -u caddy -f` |
| Take the terminal offline | `sudo systemctl stop mini-redis-web`. The landing page stays up; its button then shows an error. |
| Take everything offline | stop the VM in the provider's console |

### Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Browser can't connect at all | Port 80/443 closed. Check the provider's firewall (step 1), and on Oracle the iptables step. |
| Certificate / "not secure" error | Caddy couldn't get a certificate. Check `journalctl -u caddy`. The host name must match your IP exactly, and port 80 must be open. |
| Landing page shows "404" or is empty | The file isn't where Caddy looks: `ls -l /var/www/mini-redis/index.html` (step 5). |
| Login works, then "502 Bad Gateway" | ttyd isn't running: `systemctl status mini-redis-web`. Check the `-W` note in step 4. |
| Login works, then a blank or broken page | ttyd isn't using the `/try` prefix. Check that `-b /try` is in `/etc/systemd/system/mini-redis-web.service`, then `sudo systemctl daemon-reload && sudo systemctl restart mini-redis-web`. |
| Terminal draws, but nothing you type appears | ttyd is read-only: add `-W` (step 4). |
| `caddy validate` fails | Usually a typo in the host name or a hash pasted incompletely. The error names the line. `caddy fmt --overwrite /etc/caddy/Caddyfile` also fixes indentation. |
| Terminal opens, says "Could not connect to 127.0.0.1:6379" | The server is down: `systemctl status mini-redis`, then `journalctl -u mini-redis`. |
| Server keeps restarting | It hit `MemoryMax`, or the AOF is corrupt. Check `journalctl -u mini-redis`. To start clean: `sudo systemctl stop mini-redis && sudo rm /var/lib/mini-redis/appendonly.aof && sudo systemctl start mini-redis`. |

### Is a public demo safe?

Reasonably, for a demo, because of the layers above: visitors get a Redis prompt and nothing else; HTTPS and a password are in front; the session count is capped; the server's memory is capped and it runs as an unprivileged user. Keep the VM updated (`sudo apt upgrade`), and stop the demo when you no longer need it. There's no backup here: anything a visitor deletes is gone.

---

## What "production-ready" would still need

Be upfront about these if asked. Each is a good "what would you add next" answer:

| Missing | Why it matters | How Redis does it |
|---|---|---|
| Authentication | Anyone who can reach the port can read or delete everything | `requirepass` / ACL users |
| TLS encryption | Traffic is readable on the network | `tls-port` with certificates |
| Output buffer limits | A client that never reads can grow server memory without bound | `client-output-buffer-limit` |
| `maxmemory` + eviction | Memory grows until the OS kills the process | LRU/LFU eviction policies |
| Background AOF rewrite | `REWRITEAOF` blocks all clients while it runs | `fork()` + copy-on-write |
| Replication / failover | One machine is a single point of failure | replicas + Sentinel / Cluster |
