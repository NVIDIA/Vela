# Split authority between round-tripped structure and optimistic parameters

Studio client-server state authority splits on one line: *identity,
lifecycle, and structure* (object creation and removal, layer structure,
project entities, dataset residency) are server-authoritative — the client
requests, the server confirms, and the client's copy updates only on
confirmation. *Parameter values on user-editable objects* (rig-owned lights,
cameras, renderers, color-map transfer functions) are optimistic and one-way:
the client mutates its mirror immediately and streams the edit with no reply.

Making everything round-trip was rejected because it puts a network round-trip
on every drag of a transfer-function knob or camera orbit — the most
latency-sensitive interactions in scientific visualization — to guard against
failures that cannot really happen (a parameter write on an existing object
does not fail). Making everything optimistic was rejected because structural
operations genuinely fail (`unloadDataset` refuses a dirty dataset,
`renameDataset` collides case-insensitively) and the client cannot predict
the outcome; optimistic structure is how mirrors silently diverge.

Two consequences follow. Layer structure is server-push-only — there is no
client layer-edit message in the protocol at all, and the client's `LayerTree`
is a read-only inspector. And the server suppresses echo by origin: its push
delegate is disabled while it applies a client message, so the client's own
optimistic edits are not reflected back at it. Origin-based suppression is
only correct with one client; per-edit sequence numbers are the upgrade path
if multi-client ever lands.
