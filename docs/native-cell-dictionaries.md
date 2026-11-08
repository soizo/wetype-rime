# Bundled native cell dictionaries

WeTypeRime can load the binary cell dictionaries supplied with the local WeType
engine. They are **disabled by default** in all base schemes, including the
inherited mainland Xiaohe scheme. Selection belongs in the scheme's ordinary
`.custom.yaml` file.

These are WeType binary resources, separate from the optional Rime
`*.dict.yaml` vocabulary interface. The adapter calls the engine's exported
`wxime_add_cell_dict` and `wxime_clear_cell_dicts` interfaces. The native engine
reads each binary and handles candidate generation and ranking.

## Enable or disable

The macOS 3.0.1 (680) engine used for validation contains:

| Identifier | Bundled resource |
| --- | --- |
| `cell_dict.sc` | `cell_dict.sc.zh` |
| `cell_dict.ssrc` | `cell_dict.ssrc.zh` |
| `cell_dict.hf` | `cell_dict.hf.zh` |

To enable all three in the unified scheme, merge this into
`wetype.custom.yaml`:

```yaml
patch:
  wetype/cell_dicts:
    - cell_dict.sc
    - cell_dict.ssrc
    - cell_dict.hf
```

For independent Xiaohe, use `wetype_flypy.custom.yaml`; for the mainland variant,
use `wetype_flypy_gov.custom.yaml`. Finish or cancel the current composition,
then deploy in Squirrel. An [enablement example](../examples/rime-extensions/native-cell-dictionaries/wetype.custom.yaml)
is provided in the repository and is not enabled automatically.

Keep only the desired identifiers in the list, or comment out individual list
items. To disable every cell dictionary, replace the list with an empty one and
deploy:

```yaml
patch:
  wetype/cell_dicts: []
```

Changing `translator/dictionary` or the `import_tables` in a Rime dictionary does
not control these binary packs. `custom_phrase.txt` and `custom.dict.yaml` keep
their existing roles and settings.

## Behaviour

Cell resources are attached to the native dictionary manager whilst enabled and
detached when the scheme next queries the engine after disabling them. The
adapter rebuilds affected native sessions to discard old dictionary snapshots.
Unchanged settings do not reload binaries on every key. Different Rime contexts
reactivate their own scheme's selection before querying, including when contexts
share a worker.

The packs can affect candidate availability or ranking according to the native
engine's own rules. Loading a pack does not guarantee a new candidate for every
input. For example, the tested `cell_dict.sc` changes the native ordering of
`wuqiong`; its effect disappears when disabled. The identifiers are retained
from WeType's index rather than assigned unverified domain labels.

Existing native Emoji settings continue to apply. Cells do not force Emoji
recommendations on. Selection feedback still uses original native candidate
identifiers, and changing cell settings does not reset personal learning.
Words already learned by the native engine may therefore remain after a pack
is disabled.

## Installation and diagnostics

Install the updated adapter once with `install-rime`. Subsequent selection
changes require only the scheme patch and Squirrel deployment. Installation
copies the resources into the independent runtime along with the engine. The
official WeType application is not required whilst typing and can be uninstalled.
No binary resources are distributed in this repository.

Installation discovers cell resources from the local dictionary index and writes
an installation-specific `cell-dicts.tsv` catalogue containing identifiers,
local paths and native resource types. The worker accepts identifiers from this
catalogue; it does not accept arbitrary binary paths from Rime. The base
`dicts*.tsv` tables continue to exclude cells, so no cell is accidentally loaded
as a main dictionary. Other engine versions may supply different identifiers or
lack the native interface.

`_wetype_cell_dicts_count` reports the selected pack count after successful
preparation. `_wetype_cell_dicts_error` reports invalid lists, missing resources
or unsupported interfaces without blocking ordinary input during preparation.
A missing identifier or duplicate entry is rejected before active resources are
changed. On a native loading failure, the worker attempts to restore its previous
selection.

Native workers retain the network-denied policy, independent resource paths and
local `.log` diagnostics. No downloads, cloud updates, account access or remote
statistics are involved.
