#!/bin/bash
# Create test archives without macOS metadata
set -e

cd "$(dirname "$0")"

# Disable macOS extended attributes and resource forks
export COPYFILE_DISABLE=1

echo "Creating test archives from source/..."

# Create tar archive
tar -cf test.tar -C source root

# Create compressed variants
gzip -c < test.tar > test.tar.gz
bzip2 -c < test.tar > test.tar.bz2
xz -c < test.tar > test.tar.xz
zstd -c < test.tar > test.tar.zst

# Create zip archive
rm -f test.zip
(cd source && zip -q -r ../test.zip root)

echo "Done! Created:"
ls -lh test.tar* test.zip
