#!/usr/bin/env python3
# test-signals.py — Runner kiểm thử tự động cho hệ thống POSIX signals trên xv6
# Phụ trách: Nguyễn Trung Kiên (Tuần 5)
# Ca kiểm thử bao phủ: AU-01 (Unit tests), AU-02 (Demos), AU-03 (Ctrl-C / DL-11)

import subprocess
import sys

def main():
    print("=== Chạy kiểm thử tự động hệ thống POSIX Signals (Project 26106) ===")
    cmd = ["./test-xv6.py", "signals"]
    ret = subprocess.run(cmd)
    sys.exit(ret.returncode)

if __name__ == "__main__":
    main()
