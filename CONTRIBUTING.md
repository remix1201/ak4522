# Contributing

Thank you for your interest in the AK4522VF emulator. Read [AGENTS.md](AGENTS.md) first: it is the project's specification and working rules, and changes are reviewed against it.

## Licence of contributions

This project is licensed under the GNU General Public License, version 3 or (at your option) any later version (`GPL-3.0-or-later`). By submitting a contribution, you agree that it is licensed under the same terms, and you certify that you have the right to submit it under those terms, as set out in the [Developer Certificate of Origin](https://developercertificate.org/). Sign off each commit to confirm this:

```sh
git commit -s
```

New source files start with the licence header (`tools/check_docs.py` checks it):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker
```

If you add substantial original work, you may add your own copyright line under the existing one.

## Making a change

1. Work on a dedicated branch for each feature or fix, and keep changes focused.
2. Update code, tests, and documentation together: [docs/features.md](docs/features.md) records what exists, and the README must stay accurate.
3. Label every hardware fact as documented, inferred, a project policy, or measured, and cite its source (AGENTS.md section 2).
4. Keep the audio thread real-time safe: no allocation, locks, blocking, or file I/O.
5. Run the tests before opening a pull request:

   ```sh
   cmake --preset dev && cmake --build --preset dev
   ctest --preset dev -LE matrix
   ```

   For changes to the signal path, also run the host-rate matrix in a release build and the relevant mutation lists (see [tools/README.md](tools/README.md)).
6. Do not weaken an acceptance criterion to make a test pass; report the deviation instead.

## Documentation style

Use the Oxford comma, and do not use em or en dashes. The `docs` tests check dashes, links, and the cross-references between the engineering records.

## Reference material

Do not commit the datasheet, extracted text or images from it, or local recordings. Keep a local copy of the datasheet at `reference/AK4522.PDF` (see [reference/README.md](reference/README.md)).
