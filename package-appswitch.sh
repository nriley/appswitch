#!/bin/sh

set -x -v

cd appswitch && \
find . -name \*~ -exec rm '{}' \; && \
xcodebuild -configuration Deployment clean && \
xcodebuild -configuration Deployment DSTROOT=/ "INSTALL_PATH=$PWD" install && \
SetFile -c 'ttxt' -t 'TEXT' README VERSION appswitch.1 && \
sudo /usr/bin/install -d /usr/local/bin /usr/local/man/man1 && \
sudo /usr/bin/install appswitch /usr/local/bin && \
sudo /usr/bin/install -m 644 appswitch.1 /usr/local/man/man1 && \
chmod 755 appswitch && \
chmod 644 appswitch.1 && \
VERSION=`cat VERSION` TARBALL="appswitch-$VERSION.tar.gz" && \
cd .. && \
rm -f appswitch-$VERSION $TARBALL && \
ln -s appswitch appswitch-$VERSION && \
tar --owner=root --group=wheel --exclude=.DS_Store --exclude=.svn --exclude=.gdb_history --exclude=build --exclude=\*.mode* --exclude=\*.pbxuser --exclude=\*.perspective -zchf appswitch-$VERSION.tar.gz appswitch-$VERSION && \
# scp $TARBALL ainaz:web/nriley/software/
: