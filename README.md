# JamesOS v25 (3.0.2.113d)

Custom firmware for Anki Vector, built on [WireOS](https://github.com/os-vector).
This is the complete source of the public JamesOS v25 OTA (`jamesos-3.0.2.113d.ota`), published under the
GPL-3.0 like WireOS itself.

JamesOS is a set of changes on top of WireOS. This repository holds every file JamesOS changed or added,
exactly as built into the OTA:

| Folder | Goes on top of |
|---|---|
| `victor/` | [os-vector/wire-os-victor](https://github.com/os-vector/wire-os-victor) at commit `8fc055ac5c7538601a319149fb62275207719140` |
| `builder/` | [os-vector/wire-os](https://github.com/os-vector/wire-os) at commit `3afd148fc4d6ba5b4b8bbb4c52e8081841274f30` |

Files listed in `victor-deleted.txt` (if any) were removed.

## Build it yourself

1. `git clone --recurse-submodules https://github.com/os-vector/wire-os && cd wire-os && git checkout 3afd148fc4d6ba5b4b8bbb4c52e8081841274f30 && git submodule update --init --recursive`
2. `cd anki/victor && git checkout 8fc055ac5c7538601a319149fb62275207719140 && cd ../..`
3. Copy `builder/` over `wire-os/`, and `victor/` over `wire-os/anki/victor/`.
4. Build the OTA the WireOS way (`./build/build.sh`, see the WireOS README).

## License

GPL-3.0 (see LICENSE). Vector, Anki and Digital Dream Labs are trademarks of their owners.
