# Keep all files server-side with no wire file transfer

Every file the Studio client-server system touches lives on the server's
filesystem, and no file bytes cross the wire: no upload for import, no
download of render frames, no project-archive transfer. The client browses
the server's filesystem through two stateless requests (`ListRoots`,
`ListDirectory`) rooted under Data Roots configured at server launch, and
every operation names an absolute server path. Render output is collected
outside the protocol (shared filesystem, scp).

Wire file transfer was ruled out, not merely deferred, for three converging
reasons. The split's premise is that data is big and lives near the compute —
shipping it to the client inverts that. No archive/export format exists
anywhere in the repo to give a transfer well-defined content: a project is a
directory tree, saved datasets are self-contained server-side files, and
source lists resolve server-side by construction (ADR 0013). And the
transport cannot carry it acceptably: one message is one contiguous payload
with a single write in flight, so a multi-gigabyte transfer would
head-of-line-block frame delivery for its whole duration. A real file channel
is a transport feature (chunking, a second stream), not a message type, and
designing it speculatively would be pure weight.

Data Roots are a guardrail against accidents, not a security boundary; the
deployment stance is a trusted network with one user controlling both ends,
with ssh port-forwarding as the documented remote pattern. Server-side
operation validation is the sole authority on what a path may be used for —
the browse dialog's hints are advisory, because the rules already live
server-side and would drift if duplicated.
