#!/bin/sh

set -x -v

cd appswitch && \
find . -name \*~ -exec rm '{}' \; && \
xcodebuild && \
SetFile -c 'R*ch' -t 'TEXT' README VERSION && \
strip build/appswitch && \
sudo /usr/bin/install -c build/appswitch /usr/local/bin && \
sudo /usr/bin/install -c appswitch.1 /usr/local/man/man1 && \
chmod 755 build/appswitch && \
chmod 644 appswitch.1 && \
rm -rf build/appswitch.build build/intermediates build/.gdb_history && \
VERSION=`cat VERSION` TARBALL="appswitch-$VERSION.tar.gz" && \
DMG="appswitch-$VERSION.dmg" VOL="appswitch $VERSION" MOUNTPOINT="/Volumes/$VOL" && \
cd .. && \
rm -f appswitch-$VERSION $TARBALL $DMG && \
ln -s appswitch appswitch-$VERSION && \
tar --owner=root --group=wheel --exclude=.DS_Store --exclude=.svn --exclude=.gdb_history -zchf appswitch-$VERSION.tar.gz appswitch-$VERSION && \
#hdiutil create $DMG -megabytes 5 -ov -type UDIF && \
#DISK=`hdid $DMG | sed -ne ' /Apple_partition_scheme/ s|^/dev/\([^ ]*\).*$|\1|p'` && \
#newfs_hfs -v "$VOL" /dev/r${DISK}s2 && \
#hdiutil eject $DISK && \
#hdid $DMG && \
#ditto -rsrc launch "$MOUNTPOINT" && \
#ditto -rsrc "InstallAnywhere/launch_Web_Installers/InstData/MacOSX/Install launch $VERSION.sit" "/Volumes/launch $VERSION" && \
#launch "$MOUNTPOINT/Install launch $VERSION.sit" && \
#./openUp "$MOUNTPOINT" && \
#sleep 2 && \
## hdiutil eject $DISK && \ # this doesn't work
#osascript -e "tell application \"Finder\" to eject disk \"$VOL\"" && \
#hdiutil convert $DMG -format UDZO -o z$DMG && \
#mv z$DMG $DMG && \
scp $TARBALL ainaz:web/nriley/software/ #$DMG 
: