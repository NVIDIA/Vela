# Give Studio its own message set

The Studio client-server protocol is its own message-type enum served by its
own server. Nothing is folded into the remote-viewer demo's enum
(`RenderSession.hpp`), which stays untouched. Studio reuses the
`vsr::network` transport as-is — `Message` framing, `NetworkChannel`
dispatch, `StructuredMessage`/`DataTree` encoding per ADRs 0026/0027 — and
reuses the payload classes in `src/vsr/network/messages/` under Studio's own
type values wherever the semantics match. Studio-specific payloads start life
app-local and graduate into `vsr::network` proper only when something else
needs them. A message type outside Studio's set is rejected with an error,
never silently ignored.

Extending the demo's enum was the path of least resistance and was rejected
for what it would couple: the demo protocol is a raw scene-edit surface
(client-side object creation, array uploads, bare-float time) that Studio's
design forbids — the project layer is the only door to structure. Sharing an
enum would leave every forbidden message reachable and make the demo's
single-writer identity scheme (bare pool indices kept in lockstep by
allocation order) part of Studio's contract. A separate set makes "not in the
protocol" mean *absent* rather than *policed*, which is the difference
between a constraint the compiler enforces and one a review must catch.

Two details ride along. Studio's enum avoids the value 255, because the
demo's `ERROR = 255` collides with `MESSAGE_TYPE_INVALID = 255` in
`Message.hpp` — an existing latent bug this ADR records so it is not copied.
And a single integer `PROTOCOL_VERSION`, exchanged in an exact-match Hello
handshake, versions the whole set; both ends live in this repo and are built
together, so capability negotiation was rejected as speculative generality.
