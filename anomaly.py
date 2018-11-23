import random
import math

def generate_anomaly():
    t = '23456789abcdefghijklmnopqrs\ntuvwxyz'

    fib_cache = {0: 0, 1: 1}

    def fib(n):
        if n in fib_cache:
            return fib_cache[n]
        else:
            ret = fib(n - 1) + fib(n - 2)

        fib_cache[n] = ret
        return ret

    for i in range(1, 64):
        print "%6s %12s %s" % (i, fib(i), math.log(fib(i), 2))

    s = []
    for (n, a) in enumerate(t):
        print n
        s.extend([a] * fib(n + 1))

    random.shuffle(s)

    s = ''.join(s)

    with open('anomaly.txt', 'w') as f:
        f.write(s)

generate_anomaly()
