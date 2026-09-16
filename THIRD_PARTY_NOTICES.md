# Third-party notices

This product bundles the following third-party software. Each component
remains under its own license, reproduced in full below.

---

## gzip-hpp

Vendored in `src/gzip/` (`compress.hpp`, `config.hpp`, `decompress.hpp`,
`utils.hpp`, `version.hpp`), reformatted to this project's brace style but
otherwise functionally unmodified.

- Upstream: https://github.com/mapbox/gzip-hpp
- License: BSD-2-Clause

```
Copyright (c) 2017, Mapbox Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## Build- and link-time dependencies

Fetched by vcpkg at configure time, not vendored in this repository. Listed
for completeness; each is distributed under its own terms.

| Dependency | License |
|---|---|
| zlib | zlib |
| bzip2 | bzip2 (BSD-like) |
| liblzma (xz-utils) | 0BSD / public domain |
| sqlite3 | Public domain |
| libcurl | curl (MIT/X-derivate) |
| nlohmann/json | MIT |
