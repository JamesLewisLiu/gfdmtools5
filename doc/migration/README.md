# 2026-10-25 Standalone repository

Source: the bemanitools gfdm branch, from 859af5d77c6d91a7c7ff28a43e98ccaee67fbefb through 5914d4a641e067d2d6f65df016ebe2d564fa9508, 64 linear commits inclusive.

The starting commit is now a true root; the previous 621 ancestors, other branches, old tags, remote-tracking refs, and replace/graft data are absent. The 64 commits retain their trees, authors, committers, dates, and messages; their SHAs were recomputed because the parent graph changed. See commit-map.json for the mapping.
The historical trees still contain other-game code because those commit snapshots are preserved. The latest standalone commit removes unrelated code from the current tree, and that code is not built.

All retained src and GFDM dist files match the source tip byte-for-byte. Functions, module names, and original module Module.mk files are unchanged. Only the top-level build graph and workflow were rebuilt, limited to x86/AVS 1002, producing the five GFDM packages and six existing tests by default. Original paths remain suitable for moving feature patches back to bemanitools.

G:/GFDM_Revive/bemanitools was not modified. The pre-migration target repository is backed up in target-before-migration. No game directory was deployed. Online Actions status is tracked separately.
