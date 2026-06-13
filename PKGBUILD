# Maintainer: Tung <your-email@example.com>
pkgname=mudfish-vpn-bin
_pkgver=6.5.3
pkgver=${_pkgver}
pkgrel=1
pkgdesc="Mudfish Cloud VPN - a VPN service optimized for reducing game latency"
arch=('x86_64')
url="https://mudfish.net"
license=('custom')
depends=('polkit')
makedepends=('perl')
backup=()  # no config files to preserve
provides=('mudfish-vpn')
conflicts=('mudfish-vpn')
options=('!strip')
source=("mudfish-${_pkgver}-linux-x86_64.sh::https://mudfish.net/releases/mudfish-${_pkgver}-linux-x86_64.sh"
        "mudfish-helper.c"
        "mudfish-wrapper")
sha256sums=('58969885d1c22a84579c114449a9c59ec168f1f3a73c483a3c6f0dfb4ff3b818'
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

    find "${_htdocs_dir}" -name 'dashboard*.html' -exec sed -i \
        -e 's/chart_ping_refreshcnt < 36/true/' \
        -e 's/chart_traffic_line_refreshcnt < 36/true/' \
        -e 's/"handleChartPing()", 5000/"handleChartPing()", 15000/' \
        {} +

    (cd "${_htdocs_dir}" && tar cf "../${_htdocs##*/}" *)
    mv "${_htdocs##*/}" "${_htdocs}"
    rm -rf "${_htdocs_dir}"
}

package() {
    cd "${srcdir}/mudfish-${_pkgver}"

    local _optdir="${pkgdir}/opt/mudfish/${_pkgver}"

    # Compile and install mudfish Netlink helper shim
    gcc -O3 -fPIC -shared -o "${srcdir}/mudfish-helper.so" "${srcdir}/mudfish-helper.c" -ldl
    install -Dm755 "${srcdir}/mudfish-helper.so" "${_optdir}/bin/mudfish-helper.so"

    # bin/
    install -Dm755 bin/mudadm        "${_optdir}/bin/mudadm"
    install -Dm755 bin/muddiag       "${_optdir}/bin/muddiag"
    install -Dm755 bin/mudfish       "${_optdir}/bin/mudfish-real"
    install -Dm755 "${srcdir}/mudfish-wrapper" "${_optdir}/bin/mudfish"
    install -Dm755 bin/mudflow       "${_optdir}/bin/mudflow"
    install -Dm755 bin/mudmtr        "${_optdir}/bin/mudmtr"
    install -Dm755 bin/mudnetmon     "${_optdir}/bin/mudnetmon"
    install -Dm755 bin/mudrun        "${_optdir}/bin/mudrun"
    install -Dm755 bin/mudrun-headless "${_optdir}/bin/mudrun-headless"
    install -Dm755 bin/mudstat       "${_optdir}/bin/mudstat"
    install -Dm755 bin/mudwfp_proxy  "${_optdir}/bin/mudwfp_proxy"

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
}

