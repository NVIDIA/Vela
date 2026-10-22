# Share cached images by decoded content

The Image Cache found an image by its source: the resolved path, colour space
and row order. Production assets often ship their own copy of a texture that
another asset ships too. In the Davinci set, `Flyer`, `FlyerBase` and
`FlyerWing` each carry a byte-identical `wood_cross_meta.png`, and dozens of
props carry their own all-black metallic map. Each copy is a different path,
so each was decoded into an Array of its own. On the `rt_010` shot
(`material=renderLow`) that was 629 image Arrays (1.39 GiB) holding 431
distinct images. Because a Sampler names its image Array by identity, it also
kept ADR 0047 from sharing those copies' Samplers and Materials.

When a newly decoded image's texels match an image the cache already holds
(found by hash, confirmed by `memcmp`, with the same size, format and
orientation), the cache now files the new source under that Image instead of
creating an Array. On the shot this gives 431 image Arrays (0.96 GiB), 432
Samplers instead of 630 and 216 Materials instead of 257, and peak import
memory falls from 21.8 to 21.4 GiB. Import time is unchanged.

Content is compared after decoding, not as file bytes, so identical images in
different containers are shared too, and the comparison is the same one every
source already passes through in `store()`. The cost is that each copy is
still decoded once before it is found to be a duplicate. Hashing file bytes
first would skip that decode, but it would also miss identical texels stored
differently.

## Consequences

- Every importer that uses the Image Cache gets this, not only USD.
- Two sources that share an Image share its Array. Editing one copy's texels
  in place edits every copy, as with geometry Arrays (ADR 0046).
- This amends ADR 0047's note that copies with equal images in separate cache
  entries could not share Samplers. They now can.
