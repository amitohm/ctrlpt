#!/bin/bash

if [ "$1" = "clean" ]
then
    echo "Cleaning and removing autogenerated files."
    make clean
elif [ "$1" = "cleanall" ]
then
    make clean
    rm -rf Makefile.in Makefile aclocal.m4 autoconfig.h autoconfig.h.in config.log config.status configure device web libupnp.pc libtool stamp-h1 autom4te.cache/ upnp/Makefile upnp/Makefile.in sample/Makefile sample/Makefile.in json/Makefile json/Makefile.in threadutil/Makefile threadutil/Makefile.in
    rm -rf sample/.deps sample/common/.deps upnp/src/api/.deps upnp/src/gena/.deps upnp/src/genlib/miniserver/.deps upnp/src/genlib/net/.deps upnp/src/genlib/util/.deps upnp/src/ssdp/.deps upnp/src/genlib/net/http/.deps upnp/src/urlconfig/.deps upnp/src/soap/.deps upnp/src/genlib/client_table/.deps upnp/src/genlib/service_table/.deps upnp/src/genlib/net/uri/.deps
    rm -f ctrlpt
    echo "Cleaning done!!"
else
    if [ -e "./configure" ]
    then
	echo "Configuration exists!!!"
    else
	autoreconf
	automake
	./configure --disable-optssdp --disable-device --disable-shared --enable-debug
    fi
    make
    echo "Build done!!"
    rm -f device web
    ln -s sample/ctrlpt ctrlpt
fi

