# Third-party provenance

Original project copyright notices are retained verbatim in `LICENSE`.
Renaming adapted framework code to `ethernet::core` does not remove attribution.

| Component | Local notice |
| --- | --- |
| Xenomods / Skyline foundations | `LICENSE` |
| Skylaunch runtime (relocated, adapted imports) | `LICENSE`, source-file notices in `vendor/skylaunch` |
| imgui-xeno | `vendor/imgui-xeno/LICENSE` (GPLv2 text) |
| Dear ImGui | `vendor/imgui-xeno/extern/imgui/LICENSE.txt` |
| fmt | `vendor/fmt/LICENSE.rst` |
| GLM | `vendor/glm/copying.txt` |
| tomlplusplus | `vendor/tomlpp/LICENSE` |
| magic_enum | `vendor/magic_enum/LICENSE` |

These are source snapshots from the existing working EtherNet checkout, not
verified pristine upstream revisions. In particular, fmt's assertion callback
is integrated with the project logger; its namespace was migrated with that
logger. imgui-xeno retains local integration changes.

Publication review remains necessary: the inherited top-level MIT license and
imgui-xeno's GPLv2 notice must not be represented as one blanket MIT license.
The bundled JetBrains Mono font files also need their upstream license notice
verified before distribution. Existing dependency notices must be retained.
