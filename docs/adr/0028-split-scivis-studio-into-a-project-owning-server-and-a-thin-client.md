# Split SciVis Studio into a project-owning server and a thin client

SciVis Studio gains a client-server mode in which a headless server owns the
`ProjectContext` — Project, Dataset, Shot, and Rig CRUD, dataset residency,
and persistence — along with the `vsr::scene::Scene`, the `AnimationManager`,
all file I/O, and all ANARI rendering. The client is a UI frontend holding
read-oriented copies of server state and no project logic of its own. The full
design is in [`docs/scivis-studio-client-server.md`](../scivis-studio-client-server.md).

The alternative was the shape the existing network demo suggests: keep the
project model on the client and treat the server as a remote renderer that
mirrors scene edits. That shape was rejected because everything the project
model does — importing datasets, checking availability, loading and unloading
residency, saving archives — is filesystem- and memory-bound work that must
happen where the data and the GPU live. A client-owned project would have to
ship file contents and residency consequences across the wire continuously;
a server-owned project ships only structure and metadata. Placing the
authoritative model next to the data is the premise that makes every other
decision in the split (no wire file transfer, descriptor-only arrays, opaque
dataset subtrees) coherent.

The consequence is that the client is never a source of truth: identity,
lifecycle, and structure round-trip through the server, and a client that
disconnects loses nothing because it owned nothing. This is also what makes
the future modes cheap — an MPI server keeps the project on rank 0, and a
local-loopback frontend is the same client pointed at 127.0.0.1.
