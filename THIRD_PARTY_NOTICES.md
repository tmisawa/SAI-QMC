---
date: 2026-09-23
datetime: 2026-09-23 14:54 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Third-party algorithm attribution and external dependency boundaries.
  Preserves upstream permissions and links the licenses of external libraries.
---

# Third-party notices

The MIT license covers the project's own contributions. It does not
replace the permissions or licenses of other authors or external libraries.

## Random-number routines

`src/rng.c` implements xoshiro256** and uses SplitMix64 for initialization.
`src/replica.c` also uses the SplitMix64 mixing function for replica seeds.
The corresponding reference implementations are:

- [xoshiro256**](https://prng.di.unimi.it/xoshiro256starstar.c),
  David Blackman and Sebastiano Vigna, 2018.
- [SplitMix64](https://prng.di.unimi.it/splitmix64.c),
  Sebastiano Vigna, 2015.

Both upstream files dedicate the software to the public domain to the extent
possible under law and include an explicit permission to use, copy, modify,
and distribute it. Their notices are preserved in [licenses/PRNG.txt](licenses/PRNG.txt).
The project adapts these routines to explicit per-replica state and floating-point
output; it does not include the upstream jump-function implementation.

## External libraries

No BLAS, LAPACK, OpenMP, or MPI implementation is vendored in this source tree.
The build links to locally installed libraries. Consult the licenses of the
actual implementations and versions when distributing linked executables.

| Component | Use and license reference |
| --- | --- |
| Apple Accelerate | macOS BLAS/LAPACK backend; supplied by the operating system under Apple's terms |
| LAPACK | Linux linear algebra; [upstream license](https://www.netlib.org/lapack/LICENSE.txt) |
| OpenBLAS | Possible Linux BLAS backend; [upstream BSD license](https://github.com/OpenMathLib/OpenBLAS/blob/develop/LICENSE) |
| LLVM OpenMP | Optional OpenMP runtime used in the macOS checks; [upstream notices](https://github.com/llvm/llvm-project/blob/main/openmp/LICENSE.TXT) |
| GCC / libgomp | Possible Linux OpenMP toolchain; [GCC Runtime Library Exception](https://www.gnu.org/licenses/gcc-exception-3.1.html) and the installed runtime's license apply |
| Open MPI | Optional MPI implementation used in local checks; [upstream license](https://www.open-mpi.org/community/license.php) |
| NumPy, Matplotlib, mpmath | Optional analysis packages, installed separately; not required by the C executable |

Intel toolchain and MPI names in historical reports describe external computing
environments; those products are not included here. HΦ and the FullDiag
implementation are not bundled. The included numerical reference tables have
their source paths and hashes documented in [PROVENANCE.md](PROVENANCE.md).

Algorithmic references, including Otsuka's thesis, are given in
[REFERENCES.md](REFERENCES.md).
