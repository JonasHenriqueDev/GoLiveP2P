Name:           golive-p2p
Version:        0.6.0
Release:        0.1.qt1%{?dist}
Summary:        GoLive P2P Qt receive-only client (experimental)
License:        LicenseRef-GoLiveP2P-with-bundled-libraries
URL:            https://github.com/JonasHenriqueDev/GoLiveP2P
Source0:        GoLive-P2P-0.6.0-qt.1-linux-x64.tar.gz
Source1:        golive-p2p.desktop
Source2:        golive-p2p.svg
ExclusiveArch:  x86_64
# Private runtime below /opt: dependencies are verified by ldd and startup
# tests. RPM must not advertise these private libraries as system providers.
AutoReqProv:    no
Requires:       bash
Requires:       glibc >= 2.39
Requires:       libGL.so.1()(64bit)
Requires:       libEGL.so.1()(64bit)
Requires:       alsa-lib
# Retain the exact binaries already tested and published in the Qt tarball.
%global debug_package %{nil}
%global __os_install_post %{nil}

%description
Native Qt Widgets and C++ client for receiving GoLive P2P video and audio
over a Tailscale tailnet. Linux hosting/capture is not enabled in this build.
Qt and multimedia libraries are bundled in a private application directory.
Tailscale must be installed and connected separately.
This is a third-party experimental installer, not a Fedora repository package.
Licenses and dependency notices are included; the complete dependency license
audit remains pending, as documented in the upstream experimental release.

%prep
%setup -q -n qt-linux

%build
# Uses the checksum-verified, previously built native Linux release payload.

%install
mkdir -p %{buildroot}/opt/golive-p2p
cp -a . %{buildroot}/opt/golive-p2p/
mkdir -p %{buildroot}%{_bindir}
cat > %{buildroot}%{_bindir}/golive-p2p <<'SH'
#!/bin/bash
exec /opt/golive-p2p/golive "$@"
SH
chmod 755 %{buildroot}%{_bindir}/golive-p2p
install -Dm644 %{SOURCE1} %{buildroot}%{_datadir}/applications/golive-p2p.desktop
install -Dm644 %{SOURCE2} %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/golive-p2p.svg
desktop-file-validate %{buildroot}%{_datadir}/applications/golive-p2p.desktop

%files
/opt/golive-p2p
%{_bindir}/golive-p2p
%{_datadir}/applications/golive-p2p.desktop
%{_datadir}/icons/hicolor/scalable/apps/golive-p2p.svg

%changelog
* Fri Oct 02 2026 GoLive P2P project - 0.6.0-0.1.qt1
- Package existing Qt-only receive client with menu launcher and icon.
