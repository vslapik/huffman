import random

def generate_large():
    s = []
    t = '0123456789abcdefghijklmnopqrstuvwxyz\n'
    for i in xrange(209715200):
        s.append(random.choice(t))

    s = ''.join(s)
    with open('large.txt', 'w') as f:
        f.write(s)

generate_large()
