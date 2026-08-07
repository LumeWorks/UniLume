# UniLume Installation Guide

UniLume is distributed through the [UniLume package repository](https://packages.dismon.me).

## Table of contents

- [Debian / Ubuntu](#debian--ubuntu)
- [Fedora / RHEL](#fedora--rhel)
- [Arch Linux](#arch-linux)
- [Generic Linux (tar.zst)](#generic-linux-tarzst)
- [Post-install](#post-install)
- [Verification](#verification)
- [Updating](#updating)
- [Uninstalling](#uninstalling)

## Debian / Ubuntu

### Add the repository

```sh
curl -fsSL https://packages.dismon.me/keys/unilume-archive-keyring.gpg \
  | sudo tee /usr/share/keyrings/unilume-archive-keyring.gpg >/dev/null

echo "deb [signed-by=/usr/share/keyrings/unilume-archive-keyring.gpg] \
https://packages.dismon.me/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/unilume.list
```

### Install

```sh
sudo apt update
sudo apt install unilume
```

## Fedora / RHEL

### Add the repository

```sh
sudo dnf config-manager addrepo \
  --from-repofile=https://packages.dismon.me/rpm/unilume.repo
```

### Install

```sh
sudo dnf install unilume
```

## Arch Linux

### Manual download

```sh
curl -fsSL https://packages.dismon.me/latest/stable/$(uname -m) \
  -o unilume-latest.pkg.tar.zst

sudo pacman -U unilume-latest.pkg.tar.zst
```

*Repository support is planned for a future release.*

## Generic Linux (tar.zst)

For any Linux distribution without a native package format:

```sh
# Download the latest release
curl -fsSL https://packages.dismon.me/latest/stable/$(uname -m) \
  -o unilume-latest.tar.zst

# Extract
tar --zstd -xf unilume-latest.tar.zst
cd unilume-*/

# Install (default prefix: /usr/local)
sudo ./install.sh

# Or with custom prefix
sudo ./install.sh --prefix=/usr
```

## Post-install

1. Native packages install the session-access rule for the zero-preedit
   backend. After a first install from source, reload it once:
   ```sh
   sudo udevadm control --reload-rules
   sudo udevadm trigger --name-match=uinput
   ```

2. Restart Fcitx5:
   ```sh
   fcitx5 -rd
   ```

3. Open Fcitx5 configuration:
   ```sh
   fcitx5-configtool
   ```

4. Add **UniLume** as an input method in the *Add Input Method* dialog.

5. If the package includes the optional configuration application, open it
   from the UniLume entry in Fcitx configuration or run:
   ```sh
   unilume-config
   ```

If `/dev/uinput` is unavailable, UniLume remains usable and falls back to
client preedit. Set `VerifiedDirectEnabled=False` in the input-method config to
force that rollback explicitly.

4. If the package includes the optional configuration application, open it
   from the UniLume entry in Fcitx configuration or run:
   ```sh
   unilume-config
   ```

## Verification

Verify package integrity and authenticity:

```sh
# Import the release key
curl -fsSL https://packages.dismon.me/keys/unilume-archive-key.asc \
  | gpg --import

# Verify the checksum file
gpg --verify SHA256SUMS.asc SHA256SUMS

# Verify the downloaded package
sha256sum -c SHA256SUMS
```

The release key fingerprint is available on the [package repository](https://packages.dismon.me).

## Updating

### Package manager updates

- **Debian**: `sudo apt update && sudo apt upgrade`
- **Fedora**: `sudo dnf update unilume`
- **Arch**: `sudo pacman -Syu unilume`
- **Generic**: Download and re-run the install script

### Manual update

Download the latest package from [GitHub Releases](https://github.com/dismonjames/UniLume/releases)
and follow the installation instructions above.

## Uninstalling

### Debian / Ubuntu

```sh
sudo apt remove unilume
```

### Fedora / RHEL

```sh
sudo dnf remove unilume
sudo dnf config-manager delrepo unilume
```

### Arch Linux

```sh
sudo pacman -R unilume
```

### Generic tar.zst

```sh
sudo ./uninstall.sh
```

The uninstall script removes only files that were installed by the installer.
User configuration in `~/.config/fcitx5/` is not modified.
