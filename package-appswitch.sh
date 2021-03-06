#!/bin/zsh -ef

set -x -v

PACKAGEDIR="$PWD"
PRODUCT="appswitch"

# gather information
cd $PACKAGEDIR/$PRODUCT
VERSION=$(agvtool mvers -terse1)
TARBALL="$PRODUCT-$VERSION.tar.gz"
DISTDIR="$PRODUCT-$VERSION"
EXCLUSIONS=("${(ps:\000:)$(git ls-files -zo --directory -x $PRODUCT/$PRODUCT)}")
EXCLUSIONS=($EXCLUSIONS) # remove empty items

# clean and build
find . -name \*~ -exec rm '{}' \;
rm -rf build/
xcodebuild -configuration Deployment DSTROOT=$PWD DEPLOYMENT_LOCATION=YES install
SetFile -c 'ttxt' -t 'TEXT' README $PRODUCT.1
chmod 755 $PRODUCT
chmod 644 $PRODUCT.1

# install locally
sudo -s <<EOF
umask 022
/bin/mkdir -p /usr/local/bin /usr/local/share/man/man1
/usr/bin/install $PRODUCT /usr/local/bin
/usr/bin/install -m 644 $PRODUCT.1 /usr/local/share/man/man1
EOF

# create tarball
cd ..
rm -f $DISTDIR $TARBALL
ln -s $PRODUCT $DISTDIR
/usr/bin/tar -zcLf $TARBALL --exclude=${^EXCLUSIONS} $DISTDIR
rm -f $DISTDIR

# update Web presence
scp $TARBALL osric:web/nriley/software/
