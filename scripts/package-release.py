#!/usr/bin/env python3
try:from .release_package import main
except ImportError:from release_package import main
if __name__=='__main__':raise SystemExit(main())
