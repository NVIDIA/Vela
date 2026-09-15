# Run every long operation as a Server Task

Every long-running operation in the Studio protocol — dataset import, dataset
load, project open, project save, shot rendering — runs as a **Server Task**:
the request is answered immediately with a server-allocated task id, the
server pushes optional `TaskProgress` events and exactly one of
`TaskCompleted` / `TaskFailed(error)`, and `CancelTask(id)` is cooperative.
Tasks are single-lane in v1: one runs, others queue in order. The dividing
line between a task and a synchronous request/reply is whether the operation
touches dataset bytes or the disk transactionally; metadata edits (rename,
remove, unload) stay synchronous.

The alternative was per-operation bespoke protocols (an import-progress
message, a render-progress message, …), which is how such things accrete; one
generic task family means every future long operation gets progress, failure,
and cancellation for free, and the client builds exactly one progress UI. The
wire task concept is deliberately richer than the in-process one it replaces
(the engine's `TaskQueue` is a `std::future<void>` with no progress channel),
because a remote user cannot see the disk light blink.

Single-lane was chosen over concurrency to match the engine's single-worker
queue, keep the progress UI as simple as today's one modal, and sidestep
import-racing-save hazards. It is not a wire commitment: task ids are unique
and every event carries its id, so parallel lanes are a compatible later
change. Task state survives client disconnects — a reconnecting client
receives a task-status replay in the bootstrap — which is what makes "kick
off a four-hour render, close the laptop" a supported workflow rather than an
accident.
