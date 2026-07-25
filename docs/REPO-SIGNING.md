# Repository Signing

All UniLume packages and repository metadata are signed using a GPG key.

## Key management

### Creating a signing key

```sh
gpg --full-generate-key --key-type ed25519 --expire 2y
```

Use the email `unilume@dismon.me` and set `UNILUME_SIGNING_KEY`.

### Export the public key

```sh
scripts/release/export-public-key.sh
```

Output:
- `dist/keys/unilume-archive-key.asc` — ASCII-armored (manual import)
- `dist/keys/unilume-archive-keyring.gpg` — binary (Debian keyring)

### Import the private key in CI

The private key is stored as a base64-encoded GitHub secret
(`UNILUME_GPG_PRIVATE_KEY`). The CI workflow imports it:

```sh
scripts/release/import-gpg-key.sh
```

Environment variables required:
- `UNILUME_GPG_PRIVATE_KEY` — base64-encoded ASCII-armored private key
- `UNILUME_GPG_PASSPHRASE` — key passphrase

## Signing workflow

### Signing individual packages

```sh
scripts/release/sign-package.sh <file>
```

Produces `<file>.asc` (detached ASCII-armored signature).

### Signing all packages in a directory

```sh
scripts/release/sign-all.sh dist/packages/
```

This signs every `.deb`, `.rpm`, `.pkg.tar.zst`, and `.tar.zst` and
generates `SHA256SUMS.asc` (clearsigned checksum file).

## Repository metadata signing

### APT (reprepro)

The `Release` file is signed automatically by `reprepro` when
`SignWith` is configured in `conf/distributions`.

### RPM (createrepo_c)

`repomd.xml` is signed separately:
```sh
gpg --detach-sign --armor repomd.xml
```

### Arch Linux (repo-add)

The database is signed with `--sign`:
```sh
repo-add --sign --key unilume@dismon.me unilume.db.tar.zst *.pkg.tar.zst
```

## Key rotation

1. Generate a new key
2. Sign the new key with the old key:
   ```sh
   gpg --sign-key <new-key-id>
   ```
3. Export both keys to the repository:
   ```sh
   scripts/release/export-public-key.sh
   ```
4. Re-sign all packages with the new key
5. Update CI secrets
6. Revoke the old key and distribute the revocation certificate

## Users verifying packages

Users should:

1. Download the public key from `https://packages.dismon.me/keys/`
2. Import it: `gpg --import unilume-archive-key.asc`
3. Verify the checksum file: `gpg --verify SHA256SUMS.asc`
4. Verify the package: `sha256sum -c SHA256SUMS`

The key fingerprint is displayed on the [UniLume website](https://packages.dismon.me).
