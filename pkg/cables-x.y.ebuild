# Copyright 1999-2012 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI="4"

# no mime types, so no need to inherit fdo-mime
inherit eutils

DESCRIPTION="Secure and anonymous serverless email-like communication."
HOMEPAGE="http://dee.su/cables"
LICENSE="GPL-3"

# I2P version is not critical, and need not be updated
MY_P_PF=mkdesu-cables
I2P_PV=0.8.8
I2P_MY_P=i2pupdate_${I2P_PV}

# GitHub URI can refer to a tagged download or the master branch
SRC_URI="https://github.com/mkdesu/cables/tarball/v${PV} -> ${P}.tar.gz
         http://mirror.i2p2.de/${I2P_MY_P}.zip
         http://launchpad.net/i2p/trunk/${I2P_PV}/+download/${I2P_MY_P}.zip"

SLOT="0"
KEYWORDS="x86 amd64"

IUSE=""
DEPEND="app-arch/unzip
	>=virtual/jdk-1.5"
RDEPEND="www-servers/nginx[http,pcre,nginx_modules_http_access,nginx_modules_http_fastcgi,nginx_modules_http_gzip,nginx_modules_http_rewrite]
	www-servers/spawn-fcgi
	www-misc/fcgiwrap
	mail-filter/procmail
	net-misc/curl
	dev-libs/openssl
	>=virtual/jre-1.5
	gnome-extra/zenity"

pkg_setup() {
	enewgroup cable
	enewuser  cable -1 -1 -1 cable
}

src_unpack() {
	unpack ${P}.tar.gz
	mv ${MY_P_PF}-* ${P}              || die "failed to recognize archive top directory"

	unzip -j -d ${P}/lib ${DISTDIR}/${I2P_MY_P}.zip lib/i2p.jar || die "failed to extract i2p.jar"
}

src_install() {
	default

	doinitd  "${D}"/etc/cable/cabled
	doconfd  "${D}"/etc/cable/spawn-fcgi.cable
	rm       "${D}"/etc/cable/{cabled,spawn-fcgi.cable} || die
	dosym    spawn-fcgi /etc/init.d/spawn-fcgi.cable
	fperms   600   /etc/cable/nginx.conf

	# /srv/www(/cable)        drwx--x--x root  root
	# /srv/www/cable/certs    d-wx--s--T root  nginx
	# /srv/www/cable/(r)queue d-wx--s--T cable nginx
	keepdir       /srv/www/cable/{certs,{,r}queue}
	fperms  3310  /srv/www/cable/{certs,{,r}queue}
	fperms   711  /srv/www{,/cable}
	fowners      :nginx /srv/www/cable/certs
	fowners cable:nginx /srv/www/cable/{,r}queue
}

pkg_postinst() {
	elog "Remember to add cabled, nginx, and spawn-fcgi.cable to the default runlevel."
	elog "You need to adjust the user-specific paths in /etc/cable/profile and set the"
	elog "nginx configuration: ln -sf /etc/cable/nginx.conf /etc/nginx/nginx.conf"
	elog "Generate cables certificates and Tor/I2P keypairs for the user:"
	elog "    gen-cable-username"
	elog "        copy CABLE_CERTS/certs/*.pem  to CABLE_PUB/cable/certs (group-readable)"
	elog "    gen-tor-hostname"
	elog "        copy CABLE_TOR/hidden_service to /var/lib/tor (readable only by 'tor')"
	elog "    gen-i2p-hostname"
	elog "        copy CABLE_I2P/eepsite        to /var/lib/i2p (readable only by 'i2p')"
	elog "Once a cables username has been generated for the user:"
	elog "    rename CABLE_PUB/cable to CABLE_PUB/<username>"
	elog "        <username> is located in CABLE_CERTS/certs/username"
	elog "    /etc/cable/nginx.conf"
	elog "        replace each occurrence of CABLE with <username>"
	elog "        uncomment the 'allow' line"
	elog "Configure Tor and I2P to forward HTTP connections to nginx:"
	elog "    /etc/tor/torrc"
	elog "        HiddenServiceDir  /var/lib/tor/hidden_service/"
	elog "        HiddenServicePort 80 127.0.0.1:80"
	elog "    /var/lib/i2p/i2ptunnel.config"
	elog "        tunnel.X.privKeyFile=eepsite/eepPriv.dat"
	elog "        tunnel.X.targetHost=127.0.0.1"
	elog "        tunnel.X.targetPort=80"
	elog "Finally, the user should configure the email client to run cable-send"
	elog "as a pipe for sending messages from addresses shown by cable-info."
	elog "See comments in /usr/bin/cable-send for suggested /etc/sudoers entry."
}
