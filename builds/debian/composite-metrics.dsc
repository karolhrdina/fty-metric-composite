Format:         1.0
Source:         composite-metrics
Version:        0.1.0-1
Binary:         libcomposite-metrics0, composite-metrics-dev
Architecture:   any all
Maintainer:     John Doe <John.Doe@example.com>
Standards-Version: 3.9.5
Build-Depends: bison, debhelper (>= 8),
    pkg-config,
    automake,
    autoconf,
    libtool,
    libzmq4-dev,
    libczmq-dev,
    libmlm-dev,
    liblua5.1-0-dev,
    libcxxtools-dev,
    libbiosproto-dev,
    dh-autoreconf,
	asciidoc, docbook-xsl-ns, docbook-xsl, docbook-xml, libxml2-utils, xsltproc

Package-List:
 libcomposite-metrics0 deb net optional arch=any
 composite-metrics-dev deb libdevel optional arch=any

