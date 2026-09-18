# libsamplerate 0.2.2 (vendored)

Secret Rabbit Code / **libsamplerate** tag [`0.2.2`](https://github.com/libsndfile/libsamplerate/releases/tag/0.2.2)
from <https://github.com/libsndfile/libsamplerate>.

**License:** BSD-2-Clause — see [`COPYING`](COPYING). Do not relicense these files.
Keep copyright banners in the `.c`/`.h` sources.

**Host tools only.** Linked into `remix`, `vqatool`, and `remix.wasm`. Not part of `cnc.tos`.

**Compiled sources:** `src/samplerate.c`, `src/src_sinc.c`, `src/src_linear.c`, `src/src_zoh.c`
(plus headers). `config.h` in this directory is project-local, not upstream.

Binary redistributions of remix-web must ship `COPYING` beside `remix.wasm`
(`web/public/licenses/libsamplerate.COPYING`).
