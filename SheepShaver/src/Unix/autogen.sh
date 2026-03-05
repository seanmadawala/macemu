#! /bin/sh
# Run this to generate all the initial makefiles, etc.
# This was lifted from the Gimp, and adapted slightly by
# Christian Bauer.
#
# GTK4 integration: if deps/install/lib/pkgconfig/gtk4.pc exists (built by
# deps/build_deps.sh), PKG_CONFIG_PATH is set automatically so configure
# finds the bundled GTK4 4.18.3.  To build everything from scratch:
#   cd SheepShaver/src && bash build_sheepshaver.sh

DIE=0

PROG="SheepShaver"

# Check how echo works in this /bin/sh
case `echo -n` in
-n) _echo_n=   _echo_c='\c';;
*)  _echo_n=-n _echo_c=;;
esac

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have autoconf installed to compile $PROG."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
        DIE=1
}

(aclocal --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "**Error**: Missing aclocal. The version of automake"
	echo "installed doesn't appear recent enough."
	echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.3.tar.gz"
	echo "(or a newer version if it is available)"
	DIE=1
}

if test "$DIE" -eq 1; then
        exit 1
fi

if test -z "$ACLOCAL_FLAGS"; then
    ACLOCAL_FLAGS="-I `aclocal --print-ac-dir` -I m4"
fi

aclocalinclude="$ACLOCAL_FLAGS"; \
(echo $_echo_n " + Running aclocal: $_echo_c"; \
    aclocal $aclocalinclude; \
 echo "done.") && \
(echo $_echo_n " + Running autoheader: $_echo_c"; \
    autoheader; \
 echo "done.") && \
(echo $_echo_n " + Running autoconf: $_echo_c"; \
    autoconf; \
 echo "done.") 

rm -f config.cache

# Prepend locally built GTK4 to PKG_CONFIG_PATH if available
DEPS_INSTALL="$(cd "$(dirname "$0")/../deps/install" 2>/dev/null && pwd)" || true
if [ -f "${DEPS_INSTALL}/lib/pkgconfig/gtk4.pc" ]; then
    echo " + Found locally built GTK4 in deps/install — prepending to PKG_CONFIG_PATH"
    PKG_CONFIG_PATH="${DEPS_INSTALL}/lib/pkgconfig:${DEPS_INSTALL}/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
    export PKG_CONFIG_PATH
    LDFLAGS="${LDFLAGS:-} -Wl,-rpath,${DEPS_INSTALL}/lib"
    export LDFLAGS
fi

if [ x"$NO_CONFIGURE" = "x" ]; then
    echo " + Running 'configure $@':"
    if [ -z "$*" ]; then
        echo "   ** If you wish to pass arguments to ./configure, please"
        echo "   ** specify them on the command line."
    fi
    ./configure "$@"
fi
