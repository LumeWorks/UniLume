# SPDX-License-Identifier: GPL-2.0-or-later

%global srcname unilume

Name:           unilume
Version:        0.1.0
Release:        1%{?dist}
Summary:        Modern, lightweight Vietnamese input method for Linux

License:        GPL-2.0-or-later AND LGPL-2.0-or-later
URL:            https://packages.dismon.me
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
BuildRequires:  gettext
BuildRequires:  pkgconfig(Fcitx5Core) >= 5.0
BuildRequires:  fcitx5

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
%{_libdir}/fcitx5/unilume.so*
%{_datadir}/fcitx5/addon/unilume.conf
%{_datadir}/fcitx5/inputmethod/unilume.conf
%{_datadir}/applications/org.fcitx.Fcitx5.Addon.UniLume.desktop
%{_datadir}/icons/hicolor/scalable/apps/unilume*.svg
%{_datadir}/locale/vi/LC_MESSAGES/unilume.mo
%{_datadir}/metainfo/org.fcitx.Fcitx5.Addon.UniLume.metainfo.xml
%{_datadir}/doc/unilume/
%{_datadir}/licenses/unilume/
/usr/lib/udev/rules.d/70-unilume-uinput.rules

%changelog
* Sat Aug 01 2026 Lê Hùng Quang Minh <dismonjames@gmail.com> - 0.1.0-1
- Promote the tested rc2 input and packaging fixes to the first stable release.
- Standardize organization production builds on CMake and CTest.

* Sat Aug 01 2026 Lê Hùng Quang Minh <dismonjames@gmail.com> - 0.1.0-0.rc2.1
- Fix direct-input shortcut handling and fast unchanged-text passthrough.
- Fix release packaging, optional signing, and CI qualification gates.

* Fri Jul 24 2026 Lê Hùng Quang Minh <dismonjames@gmail.com> - 0.1.0-0.rc1.1
- Initial release candidate.
