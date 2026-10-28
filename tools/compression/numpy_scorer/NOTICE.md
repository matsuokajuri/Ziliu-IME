# Attribution and license boundaries

Original NumPy implementation and synthetic fixtures: Ziliu-IME contributors, 2026.
These first-party project additions follow the receiving Ziliu-IME repository's
root GPL-3.0 license; this notice does not change that license.

Architecture/format reference: chinese-ime-lm, Copyright 2026 chinese-ime-lm
contributors. The reference implementation is Rust, not this Python/NumPy code.
Historical architecture reference revision:
https://github.com/metasequoiaime/chinese-ime-lm/tree/f4a3fc007fba051695ae8300de917bb824d458ba/reference

Upstream license scopes must not be conflated:
- reference/ has its own Apache License 2.0:
  https://github.com/metasequoiaime/chinese-ime-lm/blob/f4a3fc007fba051695ae8300de917bb824d458ba/reference/LICENSE
- root scripts are GPL-3.0, not blanket Apache-2.0:
  https://github.com/metasequoiaime/chinese-ime-lm/blob/main/LICENSE
- upstream explains the separate reference license:
  https://github.com/metasequoiaime/chinese-ime-lm/blob/main/README.md

The exact reference/LICENSE at the historical revision above was retrieved
through the GitHub connector on 2026-10-28. Its 11,358 bytes match Git blob
`d645695673349e3947e8e5ae42332d0ac3164cd7`; that revision's reference/Cargo.toml
also declares Apache-2.0. The unmodified reference license is included as
LICENSE.reference-Apache-2.0.txt. No upstream GPL scripts or Rust source files
are copied into this package. The new implementation changes execution engine,
array layout operations, FP32 reduction details and GELU numerical evaluation;
it is not an unchanged upstream implementation or a bitwise equivalence claim.

NumPy and its BLAS runtime are external dependencies, not bundled. Their own
licenses and notices remain applicable to separately distributed binaries.
No trained weights, tokenizer/vocabulary, corpora, evaluation inputs, or their
redistribution rights are conveyed by this code-only package.
