#!/usr/bin/env python3
try:from .publish_package import main
except ImportError:from publish_package import main
if __name__=='__main__':raise SystemExit(main())
