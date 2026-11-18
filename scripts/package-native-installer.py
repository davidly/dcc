#!/usr/bin/env python3
try:from .native_installer import main
except ImportError:from native_installer import main
if __name__=='__main__':raise SystemExit(main())
