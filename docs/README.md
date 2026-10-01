# mini-redis documentation

Everything you need to understand, explain and defend this project, from "what is a socket?" to "why did every key once cost 5 KB?".

## Start here

**New to the project?** Open the [learning path](0-learning-path.md). It tells you what to read, in what order, what to run, what to break on purpose, and how to check you've understood it.

## The layers

| # | Document | What's in it | Read it when |
|---|---|---|---|
| 0 | [Learning path](0-learning-path.md) | study plan, experiments, checkpoint questions with answers, exercises, mastery checklist | first, and to guide everything else |
| 1 | [Basics](1-basics.md) | the concepts from zero: sockets, TCP framing, RESP, event loops, pipelining, TTLs, AOF, skip lists, signals | before reading any code |
| 2 | [Architecture](2-architecture.md) | file map, every flow step by step, design decisions and trade-offs, old vs new | to see how the pieces fit |
| 3 | [Code walkthrough](3-walkthrough/README.md) | every file, line by line | with the source open side by side |
| 4 | [Presenting](4-presenting.md) | resume bullets, pitch, standard interview Q&A, demo script | before an interview |
| 5 | [Diagrams](5-diagrams.md) | 24 Mermaid diagrams: layers, classes, sequences, state machines, skip list, AOF, build, tests | any time a picture helps |
| 6 | [C++ concepts](6-cpp-concepts.md) | every C++/POSIX feature used, in plain language, with `file:line` | when the code shows you something unfamiliar |
| 7 | [Defense guide](7-defense-guide.md) | measured facts, claim → evidence map, decision dossiers, complexity table, hard questions, **known issues** | to make every claim defensible |
| 8 | [Reference](8-reference.md) | all 53 commands, AOF translation rules, options, limits, error messages, glossary | to look something up |
| 9 | [Running and deploying](9-deploy.md) | the commands: local, Docker, systemd on a VM, SSH tunnel, browser demo with ttyd + Caddy | to run it anywhere but your laptop |
| 10 | [Deployment concepts](10-deployment-concepts.md) | the why behind layer 9, from zero: servers, IPs, ports, firewalls, SSH, systemd, logs, cgroups, DNS, HTTPS, reverse proxies, WebSockets, PTYs, Docker, security, operations, scaling | before deploying, and before questions like "how would you deploy this?" |
| 11 | [Caddy and the landing page](11-caddy.md) | what Caddy is, why it was chosen, how it runs, the Caddyfile line by line, where each URL goes, automatic HTTPS, the landing page, testing and debugging | when setting up or explaining the public demo |
| ▶ | [Interview presentation](interview/index.html) | an interactive talk track: what to show, what to say, the hooks that invite deep dives, and the deep-dive answers | the night before and the hour before an interview |

## Paths by goal

| Your goal | Path |
|---|---|
| "I have an interview tomorrow" | [2](2-architecture.md) → [5](5-diagrams.md) (diagrams 2, 6, 8, 19) → [4](4-presenting.md) → [7](7-defense-guide.md) sections 1, 3, 6, 7 |
| "I want to understand it properly" | follow the [1-week plan](0-learning-path.md#2-pick-a-schedule) |
| "I know C++, show me the design" | [2](2-architecture.md) → [5](5-diagrams.md) → [7 §4](7-defense-guide.md#4-decision-dossiers) |
| "I'm weak on C++" | [6](6-cpp-concepts.md) Parts A-D first, then the learning path |
| "I want to extend it" | [0 Phase 4](0-learning-path.md#phase-4-extend-it) → [7 §7](7-defense-guide.md#7-known-issues-and-limitations) |

## How these docs were checked

- Built and ran everything: 32/32 unit tests, 35/35 end-to-end checks (with the official `redis-cli` installed), all again under ASan + UBSan.
- Every "break it" experiment in the learning path was applied and its failure observed.
- All Mermaid diagrams render with mermaid-cli.
- Measurements (memory per key, micro benchmarks, `redis-benchmark` vs Redis 7.0.15) were taken on a 4-core x86-64 VM. Re-measure on your machine before quoting numbers.

If the code and a document ever disagree, the code is right. Fix the document.
