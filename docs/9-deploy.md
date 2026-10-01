# Layer 9: Running and deploying

There are four ways to run mini-redis, from simplest to most "production-like".
For a portfolio project, **1 and 2 are what matter**: anyone reviewing your GitHub can build and run it in a minute.

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
- **The unit file** (`deploy/mini-redis.service`): `Restart=on-failure` restarts it after a crash. `KillSignal=SIGTERM` makes `systemctl stop` trigger our graceful shutdown. It binds to `127.0.0.1`.

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

A live terminal demo beats a hosted URL for a project like this: there's no web page to "visit", and an interviewer learns more from watching `redis-benchmark` hit your server.

1. `make && ./bin/mini-redis-server` in one terminal, and keep it visible so they see the log lines.
2. In another terminal, use the official `redis-cli`. Using the official tool is the point: it proves compatibility.
3. Show persistence: write data, Ctrl+C the server, restart, read the data back.
4. Run `redis-benchmark -p 6379 -t set,get -n 200000 -c 50 -P 16 -q` live.
5. Optional: record the session with `asciinema rec` and link the recording in the README, so reviewers can watch it without installing anything.

The interview presentation (`docs/interview/index.html`) has the full script for this.

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
