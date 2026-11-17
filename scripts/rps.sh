#!/bin/sh
exec python3 scripts/runall.py --mode full --extended --timing-breakdown "$@"
