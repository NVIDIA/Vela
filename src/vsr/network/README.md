## Network Library (`vsr_network`)

`vsr_network` provides asynchronous messaging primitives and scene-sync message
types built on Boost.Asio.

### High-Level Concepts

- `Message` with typed header + raw byte payload.
- Payload helpers for writing/reading POD data and strings (`payloadWrite`,
  `payloadRead`, `payloadAs`).
- `StructuredMessage` for DataTree-backed message encoding/decoding.
- `NetworkChannel` base with handler registration and async send operations.
- `NetworkServer` and `NetworkClient` wrappers around socket accept/connect
  lifecycle.
- Prebuilt scene-synchronization message types in `messages/`:
  `NewObject`, `ParameterChange`, `ParameterRemove`, `RemoveObject`,
  `TransferArrayData`, `TransferLayer`, `TransferScene`.

### Why Use This Library

- You are building a remote viewer/control workflow and need consistent wire
  formats for VSR scene edits.
- You want asynchronous networking with simple callback registration and future-
  based send completion.
- You need both coarse-grained (full scene transfer) and fine-grained
  (incremental object/parameter updates) synchronization options.

### Build Notes

- This library is enabled with `-DVSR_USE_NETWORKING=ON`.
