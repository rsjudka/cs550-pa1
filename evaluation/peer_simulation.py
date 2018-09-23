from subprocess import call, Popen, PIPE
from sys import argv
from multiprocessing import Process
import os
import sys

os.chdir("../src/")

def run_peer(peer_dir, peer_port):
    cmd = ['./peer', peer_dir, peer_port]
    peer = Popen(cmd, stdin=PIPE, stdout=open(os.devnull, 'w'))

    for _ in range(500):
        peer.stdin.write('s\n')
        peer.stdin.flush()
        peer.stdin.write('a.txt\n')
        peer.stdin.flush()
    peer.stdin.write('q\n')

peers = [('peers/p{}/'.format(i), '55{:03d}'.format(i)) for i in range(1, int(sys.argv[1])+1)]

for peer in peers:
    p = Process(target=run_peer, args=peer)
    p.start()
