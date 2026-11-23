<!--
SPDX-License-Identifier: EUPL-1.2
Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
-->

# Update the upstream base

Select an upstream release tag and inspect it before merging. If the
`upstream` remote is missing, add
`https://github.com/wled/WLED.git` under that name.

1. Fetch the selected tag from `upstream`, for example
   `git fetch --no-tags upstream refs/tags/<tag>:refs/tags/<tag>`.
2. Merge the selected tag into the firmware branch with
   `git merge --no-ff <tag>`. Resolve differences while preserving the
   controller behavior documented in [CHANGES.md](../CHANGES.md).
3. Repeat the clean build procedure in [README.md](../README.md) for
   `kite`, `kite_s3`, `kite_c3`, and `esp32dev`. Run the existing UI and native tests.
4. Update [NOTICE.md](../NOTICE.md) with the upstream version and commit.
   Update [CHANGES.md](../CHANGES.md) from the diff against the new base.
   Refresh the dependency pins and terms in [THIRD-PARTY.md](THIRD-PARTY.md).
5. Review the updated tree for credentials and private data before publishing.
