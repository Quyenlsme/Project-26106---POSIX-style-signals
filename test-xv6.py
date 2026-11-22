#!/usr/bin/env python3

#
# python script that tests xv6 without having to boot it and type to its shell
#
# ./test-xv6.py usertests  (runs usertests)
# ./test-xv6.py -q usertests (runs the quick tests of usertests)
# ./test-xv6.py crash  (runs the crash tests)
# ./test-xv6.py log (runs the log crash test)

import argparse, os, inspect, re, signal, subprocess, sys, time
from subprocess import run

sys.stdout.reconfigure(line_buffering=True)

parser = argparse.ArgumentParser()
parser.add_argument('testrex', help="test name or regular expression")
parser.add_argument("-q", action='store_true', help="usertests quick")
args = parser.parse_args()

class QEMU(object):

    def __init__(self, reset=False):
        if reset:
            self.build_xv6()
            self.reset_fs()
        q = ["make", "qemu"]
        self.proc = subprocess.Popen(q, stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.output = ""
        self.outbytes = bytearray()
        self.reported = 0
        time.sleep(1)

    def reset_fs(self):
        try:
            run(["rm", "-f", "fs.img"], check=True)
            run(["make", "fs.img"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")

    def build_xv6(self):
        try:
            run(["make", "kernel/kernel"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")

    def save_output(self):
      try:
        with open("test-xv6.out", "w") as f:
            f.write(self.output)
            f.close()
      except OSError as e:
        print("Provided a bad results path. Error:", e)     
        
    def cmd(self, c):
        if isinstance(c, str):
            c = c.encode('utf-8')
        self.proc.stdin.write(c)
        self.proc.stdin.flush()
        
    def crash(self):
        ps = run(['ps', '-opid', '--no-headers', '--ppid', str(self.proc.pid)], stdout=subprocess.PIPE, encoding='utf8')
        kids = [int(line) for line in ps.stdout.splitlines()]
        if len(kids) == 0:
            print("no qemu")
            sys.exit(1)
        print("kill", kids[0])
        os.kill(kids[0], signal.SIGKILL)

    def stop(self):
        self.proc.terminate()

    def read(self):
        while True:
            try:
                buf = os.read(self.proc.stdout.fileno(), 4096)
            except BlockingIOError:
                break
            if len(buf) == 0:  # qemu exited
                break
            self.outbytes.extend(buf)
        self.output = self.outbytes.decode("utf-8", "replace")

    def lines(self):
        return self.output.splitlines()

    def error(self, *regexps):
        print("FAIL: match failed", regexps)

        self.read()
        output_lines = self.lines()[-40:]

        if output_lines:
            print("Last QEMU output:")
            for line in output_lines:
                print(line)

        self.save_output()
        self.stop()
        sys.exit(1)

    def match(self, *regexps, exit=True):
        found = False
        for line in self.lines():
            if any(re.match(r, line) for r in regexps):
                print(line)
                found = True
        if not found and exit:
            self.error(*regexps)
        return found

    # Print the lines matching regexp that have arrived since the last
    # call.  A trailing partial line is left for the next call, so that
    # each line is printed once, after all of it has been read.
    def progress(self, regexp):
        end = self.output.rfind("\n") + 1
        if end <= self.reported:
            return
        for line in self.output[self.reported:end].splitlines():
            if re.match(regexp, line):
                print(line)
        self.reported = end

    def monitor(self, *regexps, progress="", timeout):
        deadline = time.time() + timeout
        while True:
            time.sleep(1)
            timeleft = deadline - time.time()
            if timeleft < 0:
                self.error(*regexps)
            self.read()
            if progress:
                self.progress(progress)
            if self.match(*regexps, exit=False):
                return

def crash_log(delay=2):
    q = QEMU(True)
    q.cmd("logstress f0 f1 f2 f3 f4 f5\n")
    time.sleep(delay)
    q.crash()
    q.stop()

def recover_log():
    q = QEMU()

    # Cho đến khi shell xuất hiện dấu nhắc lệnh.
    q.monitor(r'^\$ ', timeout=30)

    # Kiểm tra thông báo phục hồi log.
    ok = q.match('^recovering', exit=False)

    if ok:
        q.cmd("ls\n")
        q.monitor('f5', timeout=30)

    q.stop()
    return ok

def forphan():
    q = QEMU(True)
    q.cmd("forphan\n")
    q.monitor('wait', timeout=30)
    q.crash()
    q.stop()

def dorphan():
    q = QEMU(True)
    q.cmd("dorphan\n")
    q.monitor('wait', timeout=30)
    q.crash()
    q.stop()

def recover_orphan():
    q = QEMU()
    q.monitor('^ireclaim', timeout=30)
    q.stop()

def test_log():
    print("Test recovery of log")

    for i in range(20):
        delay = 1.0 + i * 0.15

        print(f"Crash attempt {i + 1}, delay = {delay:.2f}s")

        crash_log(delay)
        ok = recover_log()

        if ok:
            print("OK")
            return

        print("log attempt", i + 1)

    print("FAIL")
    sys.exit(1)
    
def test_forphan():
    print("Test recovery of an orphaned file")
    forphan()
    recover_orphan()
    print("OK")

def test_dorphan():
    print("Test recovery of an orphaned file")
    dorphan()
    recover_orphan()
    print("OK")

def test_crash():
    test_log()
    test_forphan()
    test_dorphan()

def test_usertests(test=""):
    timeout = 600
    opt = ""
    if args.q:
        opt = " -q"
        timeout = 300
    elif test != "":
        opt += " " + test
    q = QEMU(True)
    q.cmd("usertests" + opt + "\n")
    q.monitor('^ALL TESTS PASSED', progress='test', timeout=timeout)
    q.stop()

def test_signals():
    print("=== TEST SIGNALS (AU-01, AU-02, AU-03) ===")
    q = QEMU(True)
    q.monitor(r'.*\$ ', timeout=30)

    # AU-01: Unit tests
    print("--- AU-01: Running unit tests (sigtest, pausetest, killtest, regtest) ---")

    print("Running sigtest...")
    q.cmd("sigtest\n")
    q.monitor(r'.*ALL TESTS PASSED SUCCESSFULLY!', timeout=30)
    time.sleep(0.5)
    print("sigtest OK")

    print("Running pausetest...")
    q.cmd("pausetest\n")
    q.monitor(r'.*pausetest: ALL PASSED', timeout=30)
    time.sleep(0.5)
    print("pausetest OK")

    print("Running killtest...")
    q.cmd("killtest\n")
    q.monitor(r'.*killtest: ALL PASSED', timeout=60)
    time.sleep(0.5)
    print("killtest OK")

    print("Running regtest...")
    q.cmd("regtest\n")
    q.monitor(r'.*TEST COMPLETED', timeout=60)
    time.sleep(0.5)
    print("regtest OK")

    # AU-02: Demos
    print("--- AU-02: Running demos (alarm_demo, mask_demo) ---")

    print("Running alarm_demo...")
    q.cmd("alarm_demo\n")
    q.monitor(r'.*\[DEMO 2\] done: 5 alarms', timeout=30)
    time.sleep(0.5)
    print("alarm_demo OK")

    print("Running mask_demo...")
    q.cmd("mask_demo\n")
    q.monitor(r'.*\[DEMO 3\] after unblock: count=1', timeout=30)
    time.sleep(0.5)
    print("mask_demo OK")

    # AU-03: Ctrl-C automated test (DL-11)
    print("--- AU-03: Automated Ctrl-C testing (DL-11) ---")

    print("Testing Ctrl-C at shell prompt...")
    q.cmd(b'\x03')
    time.sleep(0.5)
    q.cmd("echo OK_SHELL_CTRLC\n")
    q.monitor(r'.*OK_SHELL_CTRLC', timeout=10)
    time.sleep(0.5)
    print("Ctrl-C at shell prompt OK")

    print("Testing ctrlc_demo with handler...")
    q.cmd("ctrlc_demo\n")
    q.monitor(r'.*Running\.\.\.', timeout=15)
    time.sleep(0.2)
    q.cmd(b'\x03')
    q.monitor(r'.*\[DEMO 1\] Caught Ctrl\+C \(SIGINT\)! Cleaning up and exiting\.\.\.', timeout=15)
    time.sleep(0.5)
    print("ctrlc_demo handler OK")

    print("Testing ctrlc_demo with SIG_DFL...")
    q.cmd("ctrlc_demo dfl\n")
    q.monitor(r'.*Running\.\.\.', timeout=15)
    time.sleep(0.2)
    q.cmd(b'\x03')
    time.sleep(0.5)
    q.cmd("echo OK_AFTER_DFL\n")
    q.monitor(r'.*OK_AFTER_DFL', timeout=10)
    time.sleep(0.5)
    print("ctrlc_demo SIG_DFL OK")

    q.stop()
    print("=== ALL SIGNAL TESTS PASSED (AU-01, AU-02, AU-03) ===")

def main():
    print(args)
    rex = r'%s' % args.testrex
    funcs = [(obj,name) for name,obj in inspect.getmembers(sys.modules[__name__]) 
                     if (inspect.isfunction(obj) and 
                         name.startswith('test'))]
    none = True
    for (f,n) in funcs:
        if re.search(rex, n):
            none = False
            f()
    if none:
        test_usertests(test=args.testrex)

if __name__ == '__main__':
    main()
