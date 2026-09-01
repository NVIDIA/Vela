# Architecture Decision Records

Each ADR records one decision that is expensive to rediscover: what was decided,
and why the alternatives were rejected. ADRs are immutable once written — a
decision that changes gets a new ADR that supersedes, revises, or amends the
old one, and the "Supersedes / revised by" column below records those links.

New ADRs take the next unused number (`00NN-kebab-case-title.md`) regardless of
topic. Numbers are identifiers, not an ordering, so an ADR that continues an
earlier series does not need to sit next to it.

## SciVis Studio projects and datasets

Project layout, dataset ownership, and file-animation persistence for the
[SciVis Studio](../../src/apps/interactive/scivisStudio/CONTEXT.md) application.

| ADR | Decision | Supersedes / revised by |
| --- | --- | --- |
| [0001](0001-anchor-file-animation-paths-to-dataset-file.md) | Anchor file-animation paths to the Dataset Archive | **Superseded by [0007](0007-treat-file-animation-paths-as-opaque.md)** |
| [0002](0002-use-human-readable-dataset-filenames.md) | Use human-readable dataset filenames | |
| [0003](0003-delegate-file-animation-io-to-vsr.md) | Delegate file-animation I/O to VSR | Partly revised by [0013](0013-externalize-file-animation-source-lists.md) |
| [0004](0004-make-dataset-files-authoritative.md) | Make dataset files authoritative | Partly revised by [0012](0012-treat-dataset-residency-as-project-state.md), [0013](0013-externalize-file-animation-source-lists.md) |
| [0005](0005-keep-the-dataset-directory-flat.md) | Keep the dataset directory flat | Partly revised by [0013](0013-externalize-file-animation-source-lists.md) |
| [0006](0006-migrate-embedded-datasets-on-save.md) | Migrate embedded datasets on explicit save | |
| [0007](0007-treat-file-animation-paths-as-opaque.md) | Treat file-animation paths as opaque VSR state | Supersedes [0001](0001-anchor-file-animation-paths-to-dataset-file.md); narrowly revised by [0013](0013-externalize-file-animation-source-lists.md) |
| [0008](0008-give-each-dataset-an-independent-object-closure.md) | Give each dataset an independent object closure | |
| [0009](0009-give-each-dataset-its-bound-animations.md) | Give each dataset its bound animations | |
| [0010](0010-decompose-studio-project-scene-state.md) | Decompose SciVis Studio project scene state into owned archives | |
| [0012](0012-treat-dataset-residency-as-project-state.md) | Treat dataset residency as project state, not shot intent | Revises [0004](0004-make-dataset-files-authoritative.md) |
| [0013](0013-externalize-file-animation-source-lists.md) | Externalize file-animation source lists into sibling Source List Files | Revises [0003](0003-delegate-file-animation-io-to-vsr.md), [0004](0004-make-dataset-files-authoritative.md), [0005](0005-keep-the-dataset-directory-flat.md), [0007](0007-treat-file-animation-paths-as-opaque.md) |
| [0023](0023-allow-declared-file-animation-datasets.md) | Allow declared file-animation datasets | |

## SciVis Studio client-server split

The split of SciVis Studio into a project-owning server and a thin UI client.
Design doc: [`scivis-studio-client-server.md`](../scivis-studio-client-server.md);
wire vocabulary:
[SciVis Studio Remote](../../src/apps/interactive/scivisStudioRemote/CONTEXT.md).

| ADR | Decision | Supersedes / revised by |
| --- | --- | --- |
| [0028](0028-split-scivis-studio-into-a-project-owning-server-and-a-thin-client.md) | Split SciVis Studio into a project-owning server and a thin client | |
| [0029](0029-mint-all-scene-object-identity-on-the-server.md) | Mint all scene-object identity on the server | |
| [0030](0030-split-authority-between-round-tripped-structure-and-optimistic-parameters.md) | Split authority between round-tripped structure and optimistic parameters | |
| [0031](0031-keep-all-files-server-side-with-no-wire-file-transfer.md) | Keep all files server-side with no wire file transfer | |
| [0032](0032-run-every-long-operation-as-a-server-task.md) | Run every long operation as a Server Task | |
| [0033](0033-give-studio-its-own-message-set.md) | Give Studio its own message set | |
| [0034](0034-confirm-mutations-with-a-whole-project-snapshot.md) | Confirm mutations with a whole-project snapshot | |
| [0035](0035-carry-in-motion-time-in-the-frame-header.md) | Carry in-motion time in the frame header | |

## VSR Core data trees

Addressing and change notification for `vsr::core::DataTree`, and the
boundary between its in-memory and serialized forms.

| ADR | Decision | Supersedes / revised by |
| --- | --- | --- |
| [0025](0025-address-anonymous-data-nodes-by-ordinal.md) | Address anonymous Data Nodes by ordinal, not by name | |
| [0026](0026-keep-the-data-tree-file-format-unchanged.md) | Keep the Data Tree file format unchanged when introducing Data Paths | |
| [0027](0027-serialize-a-data-node-as-the-tree-it-roots.md) | Serialize a Data Node as the tree it roots | Extends [0026](0026-keep-the-data-tree-file-format-unchanged.md) |

See [`src/vsr/core/CONTEXT.md`](../../src/vsr/core/CONTEXT.md) for the
resulting vocabulary.

## VSR I/O vocabulary

| ADR | Decision | Supersedes / revised by |
| --- | --- | --- |
| [0011](0011-distinguish-archives-imports-and-dumps.md) | Distinguish Archives, foreign conversion, and application dumps | |
| [0024](0024-read-pre-rename-tsd-file-identifiers.md) | Read pre-rename TSD file identifiers, but never write them | |

See [`src/vsr/io/CONTEXT.md`](../../src/vsr/io/CONTEXT.md) for the resulting
vocabulary and [`vsr-io-archive-refactor.md`](../vsr-io-archive-refactor.md)
for the proposed source reorganization that follows from it.

## Image import

| ADR | Decision | Supersedes / revised by |
| --- | --- | --- |
| [0014](0014-store-images-in-anari-orientation.md) | Store images in ANARI orientation | |

Background and the full audit behind this decision:
[`vsr-io-image-import.md`](../vsr-io-image-import.md).

## USD import

| ADR | Decision | Supersedes / revised by |
| --- | --- | --- |
| [0015](0015-import-usd-through-a-hydra-scene-index.md) | Import USD through a Hydra scene index | Extended by [0021](0021-share-one-usd-stage-session-across-import-and-animation.md) |
| [0016](0016-bake-prototype-internal-transforms.md) | Bake prototype-internal transforms when importing instanced USD content | |
| [0017](0017-deviate-from-usdview-defaults-for-purpose-and-subdivision.md) | Deviate from usdview defaults for purpose and subdivision | |
| [0018](0018-let-imported-scenes-retain-an-open-usd-stage.md) | Let imported scenes retain an open UsdStage | Amended by [0021](0021-share-one-usd-stage-session-across-import-and-animation.md) |
| [0019](0019-report-udim-tile-sets-as-unsupported.md) | Report UDIM tile sets as unsupported rather than approximate them | |
| [0020](0020-bind-mesh-attributes-per-surface.md) | Bind mesh attributes per Surface, not once per mesh | |
| [0021](0021-share-one-usd-stage-session-across-import-and-animation.md) | Share one USD Stage Session across an import and its animations | Extends [0015](0015-import-usd-through-a-hydra-scene-index.md); amends [0018](0018-let-imported-scenes-retain-an-open-usd-stage.md) |
| [0022](0022-refill-captured-arrays-rather-than-re-running-conversion.md) | Re-fill captured Arrays rather than re-running conversion per frame | |

Known limitations of the USD/MaterialX path that are not decisions:
[`usd-materialx-known-gaps.md`](../usd-materialx-known-gaps.md).
