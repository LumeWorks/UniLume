# SPDX-License-Identifier: GPL-2.0-or-later

%global srcname unilume

Name:           unilume
Version:        0.1.0
Release:        0.rc1.1%{?dist}
Summary:        Modern, lightweight Vietnamese input method for Linux

License:        GPL-2.0-or-later AND LGPL-2.0-or-later
URL:            https://packages.dismon.me
Source0:        %{name}-%{version}.tar.zst

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(Fcitx5Core) >= 5.0

Requires:       fcitx5 >= 5.0

%description
UniLume is a Vietnamese input method built on the UniKey engine.
It provides Telex, VNI, and VIQR input through an Fcitx5 addon.

Features:
- Direct commit with SurroundingText support
- Safe client-preedit fallback for browsers
- Deterministic, no-sleep architecture

%prep
%autosetup

%build
%cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNILUME_BUILD_FCITX5_ADDON=ON
%cmake_build

%install
%cmake_install

%check
%ctest

%files
%license LICENSE
%doc README.md AUTHORS.md CONTRIBUTING.md
%doc docs/
%{_libdir}/fcitx5/unilume.so*
%{_datadir}/fcitx5/addon/unilume.conf
%{_datadir}/fcitx5/inputmethod/unilume.conf
%{_datadir}/doc/unilume/
%{_datadir}/licenses/unilume/

%changelog
* Fri Jul 24 2026 Lê Hùng Quang Minh <dismonjames@gmail.com> - 0.1.0-0.rc1.1
- Initial release candidate.
