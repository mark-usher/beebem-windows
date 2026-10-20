#!/bin/sh -e
# Host tests (Linux, g++) for BeebEm's SCSI drives (Src/BeebSCSI, Src/Scsi.cpp).
#
# 1. Pi1MHz's own BeebSCSI tests (src/tests/beebscsi/test_beebscsi.c), run
#    against BeebEm's port: mkharness.py takes the tests that apply (not the
#    VFS, jukebox, F-code, FAT transfer or storage ones) and puts BeebEm's
#    file layer under them (stubs.inc, main.inc).  The two BSSENSE/BSSELECT
#    checks fail by design: those Pi1MHz commands are not ported.
# 2. hosttest.cpp: whole commands through the host adaptor's registers, as
#    ADFS and the FileStore drive them (hoststubs: BeebEm and Windows fibers
#    on Linux).
#
# Usage: run_tests.sh <Pi1MHz source tree>   (https://github.com/dp111/Pi1MHz)
HERE=$(cd "$(dirname "$0")" && pwd)
PI1MHZ=${1:?Pi1MHz source tree}
SRC=$HERE/../../Src
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT

python3 "$HERE/mkharness.py" "$PI1MHZ/src/tests/beebscsi/test_beebscsi.c" "$HERE" "$B/test_beebem_scsi.cpp"
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fpermissive -w -I"$SRC" -o "$B/t" \
    "$B/test_beebem_scsi.cpp" "$SRC/BeebSCSI/scsi.cpp" "$SRC/BeebSCSI/filesystem.cpp" "$SRC/BeebSCSI/fileparser.cpp"
"$B/t" "$PI1MHZ/firmware/Pi1MHz/defscsi.cfg" "$B/card" || true

mkdir -p "$B/host"
cp "$HERE"/hoststubs/*.h "$SRC/Scsi.cpp" "$SRC/Scsi.h" "$B/host/"
ln -s "$SRC/BeebSCSI" "$B/host/BeebSCSI"
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -w -I"$B/host" -o "$B/ht" \
    -DDEFSCSI="\"$PI1MHZ/firmware/Pi1MHz/defscsi.cfg\"" \
    "$HERE/hosttest.cpp" "$B/host/Scsi.cpp" "$SRC/BeebSCSI/scsi.cpp" "$SRC/BeebSCSI/filesystem.cpp" "$SRC/BeebSCSI/fileparser.cpp"
"$B/ht"
