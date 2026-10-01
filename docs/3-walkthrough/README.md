# Layer 3: Code walkthrough (line by line)

Each file below quotes the code in small pieces and explains every line: what it does and why it's written that way.
Suggested reading order (bottom of the stack first):

1. [Protocol: resp_parser, resp_writer](01-protocol.md)
2. [Storage: database, clock](02-database.md)
3. [Skip list and sorted set](03-skiplist.md)
4. [Commands: commands, helpers, glob, cmd_*.cpp](04-commands.md)
5. [Persistence: aof](05-aof.md)
6. [Networking: net, connection, server, main, config](06-server.md)
7. [The client](07-client.md)
8. [Tests, benchmarks, Makefile, CI](08-tests-and-build.md)

How to use this: open the source file side by side with its walkthrough and step through it.
