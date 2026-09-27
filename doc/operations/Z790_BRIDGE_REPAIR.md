# Z790 bridge and migration reconciliation

Recorded 2026-09-27.

The former connector passed a Docker service hostname to inet_pton, which only parses numeric IP addresses. TcpEndpointConnector.h resolves IPv4/IPv6 endpoints anew on every attempt, closes failed descriptors, checks socket errors, observes a shared connection deadline after DNS, and leaves successful sockets nonblocking. The existing send retry budget can therefore operate instead of blocking indefinitely. OS resolver time is not included in the socket deadline; it is not advertised as bounded.

Sources: POSIX getaddrinfo/connect/poll and Linux man-pages maintained at https://man7.org/linux/man-pages/man3/getaddrinfo.3.html and https://man7.org/linux/man-pages/man2/connect.2.html. New implementation and test authored 2026-09-27.

Run: python3 modules/mod-ec-realtime-bridge/tests/run_tcp_endpoint_tests.py

Source baseline: live release commit a6028434f, plus complete modules recovered from 09eb9153ce9646e567b9e95f9f650ade95ee19ac. Existing module authorship/license headers are preserved. This is an isolated worktree; active World Events work is untouched. Recovery snapshot does not establish equivalence with unrecovered MSI core 8d8c63926b24.

No authentication credentials, database dumps, live config, or extracted client data are included. Source history reconciliation and gameplay verification must be reported independently of socket tests.
