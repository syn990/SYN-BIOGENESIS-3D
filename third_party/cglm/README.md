# third_party/cglm/

Vendored headers from [cglm](https://github.com/recp/cglm) 0.9.6 (MIT
licensed, see `LICENSE`), used header-only per upstream's own supported
usage mode -- no system package, no linking, just `#include <cglm/cglm.h>`.

Not available in this system's package repos at the time this was vendored
(checked `pacman`), so it's committed directly rather than documented as a
build dependency.

## Regenerating

```
git clone --depth 1 https://github.com/recp/cglm.git /tmp/cglm-src
cp -r /tmp/cglm-src/include third_party/cglm/include
cp /tmp/cglm-src/LICENSE third_party/cglm/LICENSE
```
