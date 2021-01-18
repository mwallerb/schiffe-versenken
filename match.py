#!/usr/bin/env python
import subprocess
import sys
import os
import time

def cls():
    os.system('cls' if os.name == 'nt' else 'clear')

if __name__ == '__main__':
    subargs = sys.argv[1:]
    target = 80
    rate = 0.5

    win_a = 0
    win_b = 0
    draws = 0
    prevtime = time.time()

    for i in range(3*target):
        cls()
        print ("A:", ("A" * win_a).ljust(target, "."))
        print ("B:", ("B" * win_b).ljust(target, "."))
        print ("X:", "X" * draws)
        print ()

        rc = subprocess.call(subargs)
        if rc == 0:
            draws += 1
        elif rc == 1:
            win_a += 1
        elif rc == 2:
            win_b += 1
        else:
            raise RuntimeError("something went wrong...")

        elapsed = time.time() - prevtime
        if elapsed < rate:
            time.sleep(rate - elapsed)
        prevtime = time.time()
        i += 1

        if win_a >= target or win_b >= target:
            break
