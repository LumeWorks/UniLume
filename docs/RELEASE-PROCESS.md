# Release Process

## Overview

UniLume publishes packages to:
- **Cloudflare Pages** — website at `packages.dismon.me`
- **Cloudflare R2** — package binaries and repo metadata
- **GitHub Releases** — source archives and binary builds

## Prerequisites

- GPG signing key configured (see [REPO-SIGNING.md](REPO-SIGNING.md))
- GitHub Actions secrets set up:
  - `UNILUME_GPG_PRIVATE_KEY` — base64-encoded private key
  - `UNILUME_GPG_PASSPHRASE` — key passphrase
  - `RCLONE_CONFIG` — rclone config for Cloudflare R2
- `wrangler` CLI configured for Cloudflare Worker deployment

## Release checklist

### 1. Prepare the release

```sh
# Ensure working tree is clean
git status
git diff

# Update version in cmake/UniLumeVersion.cmake
vim cmake/UniLumeVersion.cmake

# Update changelogs
# Debian: dch -v <version> -D unstable
# RPM: update %changelog in packaging/rpm/unilume.spec

# Commit
git add -A
git commit -m "chore: bump version to <version>"
```

### 2. Create the tag

```sh
git tag -s v<version> -m "UniLume v<version>"
git push origin v<version>
```

The tag must follow `vMAJOR.MINOR.PATCH[-PRERELEASE]` format.

### 3. Monitor the release workflow

The [Release workflow](.github/workflows/release.yml) automatically:

1. Builds `.deb` packages on Ubuntu
2. Builds `.rpm` packages on Fedora
3. Builds `.pkg.tar.zst` on Arch Linux
4. Signs all packages (if signing key available)
5. Uploads to GitHub Releases
6. (Optional) Uploads to Cloudflare R2

Monitor at: https://github.com/dismonjames/UniLume/actions

### 4. Verify the release

```sh
# Check GitHub Release
gh release view v<version>

# Check package repository
curl -fsSL https://packages.dismon.me/status.json

# Download and install on a test system
# Follow docs/INSTALL.md
```

### 5. Update repository metadata (manual)

After packages are on R2, update repo metadata:

```sh
# Debian repo
scripts/release/update-apt-repo.sh stable

# RPM repo
scripts/release/update-rpm-repo.sh stable

# Arch repo
scripts/release/update-arch-repo.sh stable
```

### 6. Deploy Cloudflare Worker

```sh
cd infra/cloudflare/worker
npx wrangler deploy
```

### 7. Post-release

```sh
# Create a GitHub Release discussion
gh release view v<version> --json body --jq .body
```

## Version numbering

| Format | Example | Use |
|--------|---------|-----|
| `MAJOR.MINOR.PATCH` | `0.1.0` | CMake version |
| `MAJOR.MINOR.PATCH~rcN` | `0.1.0~rc1` | Debian prerelease |
| `MAJOR.MINOR.PATCH-0.rcN.1` | `0.1.0-0.rc1.1` | RPM prerelease |
| `MAJOR.MINOR.PATCHrcN` | `0.1.0rc1` | Arch prerelease |

Source of truth: `cmake/UniLumeVersion.cmake`

## Hotfix release

For critical fixes:

1. Branch from the release tag: `git checkout -b hotfix/v<version>-hotfix v<old-version>`
2. Apply fixes
3. Bump patch version
4. Tag and push

## Rollback

If a release is broken:

1. Remove the tag: `gh release delete v<bad-version>`
2. Push a fixed version
3. Update `dist/status.json` to point to the previous known-good version
