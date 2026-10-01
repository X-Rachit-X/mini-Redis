# Layer 11: Caddy and the landing page

This layer explains **Caddy**, the web server in front of the public demo, and the **landing page** it serves. It goes from "what is a web server" to every line of `deploy/Caddyfile`, and how to test and debug it.

Background you may want first: [Layer 10](10-deployment-concepts.md), sections 11–17 (HTTP, DNS, HTTPS, reverse proxies, basic auth, WebSockets, ttyd). The commands to install everything are in [Layer 9](9-deploy.md), section 6.

---

## 1. What Caddy is

A **web server** is a program that listens on ports 80/443 and answers HTTP requests. It does two kinds of work:

1. **Serve files.** A browser asks for `/`, and the server sends back `index.html` from a folder on disk.
2. **Act as a reverse proxy.** A browser asks for `/try/`, and the server forwards the request to another program (ttyd) and relays its answer.

**Caddy** is a web server that does both, with one feature that sets it apart: **automatic HTTPS**. Write a host name in its config, and it gets a certificate from Let's Encrypt, renews it before it expires, and redirects `http://` to `https://`. You configure none of that.

Other facts worth knowing:
- It's written in **Go** and ships as a **single binary** with no dependencies.
- Its config file is the **Caddyfile**, a short human-friendly format. Internally Caddy converts it to JSON, which you can also write directly.
- It has an **admin API** on `localhost:2019` (local only), used to load new configs without dropping connections.
- It's open source (Apache 2.0) and widely used in production.

## 2. Why Caddy here, and not nginx?

| | Caddy | nginx | Apache | Traefik |
|---|---|---|---|---|
| HTTPS certificates | automatic, built in | separate tool (certbot) and cron renewal | separate tool | automatic |
| Config for our setup | ~15 lines | ~40 lines plus certbot setup | similar to nginx | aimed at containers and labels |
| WebSocket proxying | automatic | needs extra `Upgrade` headers in config | needs a module | automatic |
| Typical use | small and medium sites, simplicity | the most common, very fast, very flexible | traditional hosting | Docker / Kubernetes |

nginx would work perfectly well and is the most common choice in industry. Caddy was chosen because **HTTPS and WebSockets work without extra steps**. For a one-person demo, fewer moving parts means fewer ways for it to break. Knowing that trade-off is a good interview answer in itself.

## 3. How Caddy runs on the VM

Installing Caddy from its apt repository (Layer 9, step 6) sets up:

| What | Where | Purpose |
|---|---|---|
| The program | `/usr/bin/caddy` | the web server |
| A systemd service | `caddy.service` | starts at boot, restarts on failure, logs to the journal |
| A system user | `caddy` | Caddy runs with only that user's rights |
| The config | `/etc/caddy/Caddyfile` | what you edit |
| Its data | `/var/lib/caddy/.local/share/caddy/` | certificates and keys, managed by Caddy (don't edit) |

Ports 80 and 443 are below 1024, which normally only root may use. The package's systemd service grants Caddy one Linux **capability**, `CAP_NET_BIND_SERVICE`, which allows exactly that. Caddy can listen on 443 without running as root. That's least privilege again.

Everyday commands:

```bash
sudo systemctl reload caddy      # load an edited Caddyfile, without dropping connections
sudo systemctl restart caddy     # full stop and start (rarely needed)
systemctl status caddy           # running?
journalctl -u caddy -f           # logs: certificate requests, errors
```

**Reload vs restart.** `reload` asks the running Caddy to switch to the new config through its admin API. If the new config is invalid, Caddy keeps the old one, so a typo can't take the site down. Always reload.

## 4. The Caddyfile format in five minutes

```
site-address {
	directive argument argument
	directive [matcher] {
		subdirective value
	}
}
```

- A **site block** starts with an **address**. `example.com` means "HTTPS for this name, with automatic certificates". `:8080` means "plain HTTP on port 8080, any name", which is handy for local testing.
- Inside are **directives**, one per line. Each one is a feature: `file_server`, `reverse_proxy`, `redir`, `encode`, `basic_auth`, `handle`...
- Many directives accept a **matcher** right after the name, to apply only to some requests. `/try/*` is a **path matcher**: it matches `/try/` and everything below it. With no matcher, a directive applies to every request.
- `#` starts a comment. Indentation is with tabs. `caddy fmt` tidies a file.

### Order: Caddy sorts directives for you
In most web servers, the order you write rules in is the order they run. **In a Caddyfile, it isn't.** Caddy has a fixed, built-in order. Among the directives we use: `redir` runs before `basic_auth`, which runs before `handle`, `reverse_proxy` and `file_server`. That's why the password check always happens before proxying, wherever you write it. (To force your own order, wrap directives in a `route` block. We don't need to.)

### `handle`: "exactly one of these"
`handle` blocks at the same level are **mutually exclusive**. For each request, Caddy picks the one whose matcher fits best (more specific paths first). A `handle` with no matcher is the fallback, used only when no other `handle` matched. This is how one site serves two completely different things at different paths.

## 5. Our Caddyfile, line by line

```
203-0-113-10.sslip.io {
```
The site address. Requests for this host name are handled by this block. Because it's a name (not `:port`), Caddy will:
1. get a certificate for it from Let's Encrypt,
2. serve HTTPS on 443,
3. answer plain HTTP on port 80 with a `308` redirect to HTTPS.

sslip.io makes this name resolve to `203.0.113.10` (Layer 10, section 12). Replace it with your IP, using dashes.

```
	encode zstd gzip
```
**Compression.** When the browser says it accepts compressed responses (the `Accept-Encoding` header), Caddy compresses text before sending it. It prefers zstd, then gzip. HTML and JavaScript shrink to roughly a quarter of their size, so the landing page and ttyd's terminal script load faster. Images and already-compressed files are skipped automatically.

```
	redir /try /try/ 308
```
**Redirect.** If someone types `/try` without the trailing slash, send them to `/try/`. Why it matters: ttyd's page loads its script and opens its WebSocket using paths under `/try/`, and the browser works out relative paths from the current URL. From `/try` (no slash), relative paths would resolve against `/` and break.

The matcher `/try` is exact: it matches only `/try`, not `/try/` or `/trying`.

Status codes, for reference:

| Code | Meaning | Browser re-sends the request method? |
|---|---|---|
| 301 | moved permanently | may change POST to GET |
| 302 | found (temporary) | may change POST to GET |
| 307 | temporary redirect | keeps the method |
| **308** | **permanent redirect** | **keeps the method** |

308 says "this is always the right address" and keeps the request exactly as it was. It's also what Caddy itself uses for HTTP → HTTPS.

```
	handle /try/* {
```
First route: everything under `/try/` is the live terminal.

```
		basic_auth {
			demo $2a$14$…
		}
```
**Password.** User `demo`, and the password's **bcrypt hash** (from `caddy hash-password`). A request without the right `Authorization` header gets `401 Unauthorized`, and the browser shows its login box. Only requests that pass go further. `$2a$` identifies bcrypt and `14` is its cost factor (2¹⁴ rounds): slow to compute on purpose, which makes guessing expensive. Details are in Layer 10, section 15.

Caddy before version 2.8 spelled this directive `basicauth`. The official repository installs a newer version, where `basic_auth` is correct.

```
		reverse_proxy 127.0.0.1:7681
	}
```
**Forward to ttyd.** Caddy opens its own connection to ttyd on localhost, sends the request (path unchanged, `/try/...`), and relays the response. Along the way it:
- adds `X-Forwarded-For` (the visitor's IP), `X-Forwarded-Proto: https` and `X-Forwarded-Host`, so the app behind it knows about the original request;
- passes the original `Host` header through;
- **upgrades WebSockets automatically**: when the browser asks to upgrade `/try/ws`, Caddy keeps that connection open and relays messages both ways for as long as the terminal is open.

`reverse_proxy` can also load-balance across several backends and health-check them, for example `reverse_proxy app1:8080 app2:8080`. We have one.

**Why ttyd runs with `-b /try`.** Caddy passes the path through unchanged, so ttyd receives `/try/`, `/try/token` and `/try/ws`. ttyd's `-b /try` (base path) tells it to expect that prefix. The alternative would be `handle_path /try/*`, which **strips** the prefix before forwarding. But then ttyd would build its URLs without `/try`, and the browser would ask for `/ws` and `/token`, which land in the landing-page route and fail. Keeping the prefix and telling the backend about it is the reliable pattern.

```
	handle {
		root * /var/www/mini-redis
		file_server
	}
}
```
**Fallback route: the landing page.** Any request that didn't match `/try/*` ends up here.
- `root * /var/www/mini-redis`: the folder to serve files from. The `*` matcher means "for all requests in this block".
- `file_server`: serve the file at that path. For `/` (a folder), it serves `index.html`. It also sets the `Content-Type` from the file extension, adds caching headers (`ETag`, `Last-Modified`) so browsers can skip re-downloading unchanged files, and answers `404` for files that don't exist. It doesn't list folder contents unless you add `browse`.

Notice that `basic_auth` is **only inside the `/try/*` route**. The landing page is public, so anyone can read about the project, and only the terminal is protected.

## 6. Where each request goes

| Request | Matching rules | Result |
|---|---|---|
| `http://…/anything` | Caddy's automatic HTTPS | `308` → same URL with `https://` |
| `GET /` | fallback `handle` | `200`, landing page (compressed) |
| `GET /index.html` | fallback `handle` | `200`, landing page |
| `GET /nothing-here` | fallback `handle` | `404` |
| `GET /try` | `redir` | `308` → `/try/` |
| `GET /try/` without login | `handle /try/*` → `basic_auth` | `401`, browser shows the login box |
| `GET /try/` with login | → `reverse_proxy` | `200`, ttyd's terminal page |
| `GET /try/token` with login | → `reverse_proxy` | `200`, ttyd's session token (JSON) |
| `GET /try/ws` (Upgrade) with login | → `reverse_proxy` | `101 Switching Protocols`, the live terminal connection |

After the first login, the browser remembers the credentials and sends them automatically with `/try/token` and `/try/ws`. That's why a single login box covers the whole terminal.

## 7. Automatic HTTPS, in detail

When Caddy starts (or reloads) with a site address that's a host name:

1. It checks its storage for a valid certificate for that name.
2. If there's none, it runs **ACME** with Let's Encrypt (and falls back to ZeroSSL if that fails). The CA asks it to prove control of the name by answering a challenge on port 80 (HTTP-01) or 443 (TLS-ALPN-01). Caddy answers it itself.
3. It stores the certificate and private key under `/var/lib/caddy/.local/share/caddy/`.
4. It **renews automatically** well before the 90-day expiry, with no cron job. It also staples OCSP responses (proof the certificate wasn't revoked), which speeds up the browser's checks.
5. It redirects HTTP to HTTPS for that name.

**If it fails**, `journalctl -u caddy` shows why. Usually ports 80/443 are blocked, or the name doesn't resolve to this VM.

**Rate limits.** Let's Encrypt limits how many certificates you can request. If you're experimenting and might trigger many requests, use their **staging** environment first. Its certificates aren't trusted by browsers, but it has generous limits. Add a global options block at the very top of the Caddyfile:

```
{
	acme_ca https://acme-staging-v02.api.letsencrypt.org/directory
}
```

Remove it once everything works, and reload.

## 8. The landing page

`deploy/site/index.html` is a **static** page: one HTML file with its CSS and a few lines of JavaScript inside it. There's no backend and no database, and it never talks to your server.

Why static:
- **Nothing to attack.** There's no code running on the server for this page, just a file being read.
- **Nothing to break.** If ttyd or mini-redis are down, the landing page still loads.
- **Fast.** Compressed by `encode`, cached by browsers through `file_server`'s headers.

What's on it, top to bottom:

| Part | Purpose |
|---|---|
| Header | name, links to the commands, how it works, GitHub |
| Hero | one-sentence description, **Open the live terminal** button (→ `/try/`, new tab), login hint, an example session |
| Commands to try | seven small cards: strings, counters, a list as a queue, a hash, a leaderboard, expiry, a type error. Each command has a **Copy** button. |
| What's under the hood | six short points on the engineering (event loop, parser, skip list, persistence, measurement, tests) |
| How this page reaches you | the browser → Caddy → ttyd → CLI → server path, so visitors can see the architecture |
| Footer | author and repository links |

Details:
- **Copy buttons** use the browser's clipboard API. If the browser refuses (some do on plain HTTP or in strict settings), the command text is selected instead, so `Ctrl+C` still works.
- **Light and dark** follow the visitor's system setting (`prefers-color-scheme`).
- **Fonts** come from Google Fonts, with system-font fallbacks if they can't load.
- The terminal opens in a **new tab**, so visitors can keep the command list open beside it.

**Editing it:** change `deploy/site/index.html` in the repo, then copy it to the VM (`sudo cp deploy/site/index.html /var/www/mini-redis/`). The next request gets the new version. Caddy reads files from disk on each request, so no reload is needed.

## 9. Testing and debugging Caddy

| Tool | What it tells you |
|---|---|
| `caddy validate --config /etc/caddy/Caddyfile` | whether the file parses and the config is valid, before you reload |
| `caddy fmt --overwrite /etc/caddy/Caddyfile` | fixes indentation and spacing |
| `caddy adapt --config /etc/caddy/Caddyfile --pretty` | the JSON Caddy turns your Caddyfile into: the exact routes and their order |
| `journalctl -u caddy -f` | live log: certificate activity, config errors, upstream errors |
| `curl -v https://…/try/` | the full HTTP exchange: status, headers, redirects |
| Layer 9, step 7 | the full checklist of requests and expected results |

**Testing on a laptop first (optional).** The same structure works with plain HTTP, which needs no certificate. With Caddy and ttyd installed and mini-redis running locally, save this as `Caddyfile.local`:

```
:8080 {
	encode zstd gzip
	redir /try /try/ 308
	handle /try/* {
		basic_auth {
			demo PASTE_HASH_HERE
		}
		reverse_proxy 127.0.0.1:7681
	}
	handle {
		root * ./deploy/site
		file_server
	}
}
```

```bash
ttyd -i 127.0.0.1 -p 7681 -W -b /try ./bin/mini-redis-cli &   # from the repo folder
caddy run --config Caddyfile.local                            # then open http://localhost:8080
```

`caddy run` stays in the foreground and prints its log, and Ctrl+C stops it. Note that copy buttons may fall back to selecting text on plain HTTP, because browsers restrict the clipboard API to secure pages (but `localhost` usually counts as secure).

## 10. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Host name doesn't match the VM's IP | certificate error, `journalctl` shows a failed challenge | `dig +short your-name` must print your IP |
| Port 80 closed, only 443 open | certificate may fail to issue or renew | open 80 too. Caddy needs it for challenges and redirects. |
| `handle_path /try/*` instead of `handle` | terminal page loads, then stays blank | use `handle`, and give ttyd `-b /try` |
| ttyd started without `-b /try` | `/try/` returns 404 or a broken page | add `-b /try` to the service, `daemon-reload`, restart |
| Password pasted in plain text instead of the hash | `caddy validate` error about the hash | run `caddy hash-password`, paste its output |
| Editing the Caddyfile in the repo but not copying it | changes have no effect | `sudo cp deploy/Caddyfile /etc/caddy/Caddyfile && sudo systemctl reload caddy` |
| `restart` after a bad edit | site goes down | use `reload`: an invalid config is rejected and the old one keeps running |

## 11. Interview questions

**"What does Caddy do in your setup?"**
"It's the only thing facing the internet. It terminates HTTPS with a certificate it gets and renews automatically from Let's Encrypt, serves a static landing page at the root, and under `/try/` checks a password and then reverse-proxies to ttyd, which runs my CLI in the browser over a WebSocket."

**"Why a reverse proxy instead of exposing ttyd directly?"**
"One public entry point. TLS, authentication and routing live in one place. ttyd and my server stay on localhost where nobody can reach them, and I could add more apps behind the same proxy later."

**"Why Caddy over nginx?"**
"Automatic certificates and WebSocket proxying with no extra setup. nginx would need certbot, a renewal job, and explicit upgrade headers. For production at scale nginx is a fine choice too. Here, fewer moving parts meant fewer ways to break."

**"How do you change its config without downtime?"**
"`systemctl reload caddy`. Caddy validates the new config and switches over gracefully through its admin API. If the config is invalid, it keeps running the old one."

**"Why did you need `-b /try` on ttyd?"**
"Caddy forwards the path unchanged, so ttyd sees `/try/...`. Stripping the prefix instead would make ttyd generate URLs without it, and the browser's WebSocket request would go to the wrong route. Telling the backend its base path keeps every URL consistent."

---

Previous: [Layer 10: Deployment concepts](10-deployment-concepts.md) · Commands: [Layer 9](9-deploy.md) · [Docs index](README.md)
