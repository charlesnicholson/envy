import sys

START_OUT = 10_000_000_000
START_ERR = 20_000_000_000
COUNT = 180_000  # ~2MB per stream

for i in range(COUNT):
    sys.stdout.write(f"{START_OUT + i}\n")
for i in range(COUNT):
    sys.stderr.write(f"{START_ERR + i}\n")
