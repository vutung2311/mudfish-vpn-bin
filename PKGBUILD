# Maintainer: Tung <your-email@example.com>
pkgname=mudfish-vpn-bin
# Dynamically fetch the latest release version if network is available; fallback to 6.6.0
_pkgver_latest=$(curl -sL --connect-timeout 3 --max-time 5 https://mudfish.net/releases/ 2>/dev/null | grep -oE 'mudfish-[0-9]+\.[0-9]+\.[0-9]+-linux-x86_64\.sh' | sed -E 's/mudfish-(.*)-linux-x86_64\.sh/\1/' | sort -V | tail -n 1)
_pkgver="${_pkgver_latest:-6.6.0}"
pkgver=${_pkgver}
pkgrel=1
pkgdesc="Mudfish Cloud VPN - a VPN service optimized for reducing game latency"
arch=('x86_64')
url="https://mudfish.net"
license=('custom')
depends=('polkit')
makedepends=()
backup=()  # no config files to preserve
provides=('mudfish-vpn')
conflicts=('mudfish-vpn')
options=('!strip')
source=("mudfish-${_pkgver}-linux-x86_64.sh::https://mudfish.net/releases/mudfish-${_pkgver}-linux-x86_64.sh"
        "mudfish-helper.c"
        "mudfish-wrapper")
sha256sums=('SKIP'
            'SKIP'
            'SKIP')

prepare() {
    cd "${srcdir}"
    sh "mudfish-${_pkgver}-linux-x86_64.sh" --noexec --target "mudfish-${_pkgver}"

    # Patch htdocs: remove 36-cycle auto-refresh limit on Realtime RTT
    # and Realtime Traffic charts so they refresh every 5s indefinitely.
    local _htdocs="mudfish-${_pkgver}/etc/htdocs-www-${_pkgver}.tar"
    local _htdocs_dir="htdocs-patched"

    mkdir -p "${_htdocs_dir}"
    tar xf "${_htdocs}" -C "${_htdocs_dir}"

    sed -i \
        -e 's/chart_ping_refreshcnt < 36/true/' \
        -e 's/chart_traffic_line_refreshcnt < 36/true/' \
        -e 's/"handleChartPing()", 5000/"handleChartPing()", 15000/' \
        "${_htdocs_dir}/www/common"/dashboard*.html

    tar cf "${_htdocs}" -C "${_htdocs_dir}" .
    rm -rf "${_htdocs_dir}"
}

build() {
    cd "${srcdir}"
    # Compile Netlink helper shim using distro CFLAGS/LDFLAGS for maximum performance and hardening
    gcc ${CFLAGS} ${CPPFLAGS} -O3 -fPIC -shared -o "mudfish-helper.so" "mudfish-helper.c" ${LDFLAGS} -ldl
}

package() {
    cd "${srcdir}/mudfish-${_pkgver}"

    local _optdir="${pkgdir}/opt/mudfish/${_pkgver}"

    # Install mudfish Netlink helper shim compiled in build()
    install -Dm755 "${srcdir}/mudfish-helper.so" "${_optdir}/bin/mudfish-helper.so"

    # Install all binaries from upstream bin/, excluding installer helper
    install -dm755 "${_optdir}/bin"
    for _bin in bin/*; do
        if [ "$_bin" != "bin/pkg_linux_setup.sh" ]; then
            install -m755 "$_bin" "${_optdir}/bin/"
        fi
    done

    # Intercept mudfish with wrapper
    mv "${_optdir}/bin/mudfish" "${_optdir}/bin/mudfish-real"
    install -m755 "${srcdir}/mudfish-wrapper" "${_optdir}/bin/mudfish"

    # sbin/
    install -Dm755 sbin/mudovpn "${_optdir}/sbin/mudovpn"

    # etc/
    install -Dm644 "etc/htdocs-www-${_pkgver}.tar" "${_optdir}/etc/htdocs-www-${_pkgver}.tar"
    install -Dm644 etc/muddiag_config.json          "${_optdir}/etc/muddiag_config.json"
    install -Dm644 etc/mudrun_cert.pem              "${_optdir}/etc/mudrun_cert.pem"
    install -Dm644 etc/mudrun_pkey.pem              "${_optdir}/etc/mudrun_pkey.pem"

    # share/ (logo kept alongside binaries)
    install -Dm644 share/mudrun_logo.png "${_optdir}/share/mudrun_logo.png"

    # var/ directory for runtime data
    install -dm755 "${_optdir}/var"

    # systemd service — runs mudrun as a proper system service,
    # fully isolated from the user session (no env var leakage).
    install -Dm644 /dev/stdin "${pkgdir}/usr/lib/systemd/system/mudfish.service" <<EOF
[Unit]
Description=Mudfish Cloud VPN
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
Environment=LD_PRELOAD=/opt/mudfish/${_pkgver}/bin/mudfish-helper.so
ExecStart=/opt/mudfish/${_pkgver}/bin/mudrun-headless -I
Restart=on-failure
RestartSec=5

# Required for mudwfp_proxy eBPF hooks (Web Inspection)
LimitMEMLOCK=infinity
Delegate=yes

# Hardening
ProtectHome=read-only
ProtectSystem=true
ReadWritePaths=/opt/mudfish/${_pkgver}/var

[Install]
WantedBy=multi-user.target
EOF

    # Launcher script — starts the service (brief pkexec) and opens the web UI.
    # The VPN daemon runs isolated; the browser runs as the user.
    install -Dm755 /dev/stdin "${_optdir}/bin/mudfish-launcher" <<'LAUNCHER'
#!/bin/sh
if ! systemctl is-active --quiet mudfish.service; then
    pkexec systemctl start mudfish.service
fi
# Wait briefly for the web UI to come up
sleep 1
xdg-open http://127.0.0.1:8282
LAUNCHER

    # Desktop entry
    install -Dm644 /dev/stdin "${pkgdir}/usr/share/applications/mudrun.desktop" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Mudfish Launcher
Comment=Start Mudfish VPN and open the web dashboard
Exec=/opt/mudfish/${_pkgver}/bin/mudfish-launcher
Icon=/opt/mudfish/${_pkgver}/share/mudrun_logo.png
Categories=Network;Utility
EOF

    # Polkit policy — allows starting/stopping the service
    sed -e "s|/opt/mudfish/[^/]*/|/opt/mudfish/${_pkgver}/|g" \
        share/net.mudfish.mudrun.policy \
        | install -Dm644 /dev/stdin "${pkgdir}/usr/share/polkit-1/actions/net.mudfish.mudrun.policy"

    # Convenience symlinks into /usr/bin for CLI usage
    install -dm755 "${pkgdir}/usr/bin"
    ln -sf "/opt/mudfish/${_pkgver}/bin/mudrun" "${pkgdir}/usr/bin/mudrun"
    ln -sf "/opt/mudfish/${_pkgver}/bin/mudfish-launcher" "${pkgdir}/usr/bin/mudfish-launcher"
    if [ -f "${_optdir}/bin/mudfish-exclude" ]; then
        ln -sf "/opt/mudfish/${_pkgver}/bin/mudfish-exclude" "${pkgdir}/usr/bin/mudfish-exclude"
    fi
}

