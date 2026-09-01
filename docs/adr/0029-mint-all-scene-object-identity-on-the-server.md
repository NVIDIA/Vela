# Mint all scene-object identity on the server

In the Studio client-server protocol, the server allocates every scene-object
identity. The client never mints a `(type, pool index)`: it sends a create
*request* and learns the assigned identity from the reply, or from a server
scene push. Wire identity is the server's explicit `(type, pool index)` pair.

The tempting alternative is what the network demo does today: both ends
allocate from their own pools and rely on identical allocation order, so a
bare index names the same object on both sides. That works only under a
single writer. The Studio server is not single-writer with respect to
creation: it creates objects the client never asked for during dataset import
and Dataset Load. Index parity would become a distributed invariant that any
dropped, reordered, or server-originated creation corrupts *quietly* — the
failure mode is parameter edits landing on the wrong object, with no error
anywhere. A round-trip on object creation is rare and already asynchronous,
so it is the cheaper price by far.

Because the client only ever edits rig-owned objects, cameras, renderers, and
color-map objects — and every lifecycle event for those is a round-tripped
operation the client itself initiated — the stale-slot window is essentially
unreachable with a single client. A generation counter on the index is the
documented upgrade path if pool-slot reuse ever bites.
