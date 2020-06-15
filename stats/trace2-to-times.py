#!/usr/bin/python

import json;
import sys;

gitdir=sys.argv[1]

filename='trace2-event-' + gitdir + '.txt'

eventFile = open(filename, 'r')
decoder = json.JSONDecoder()

while True:
    line = eventFile.readline()

    if not line:
        break

    if line.find("atexit") < 0:
        continue

    o = decoder.decode(line)
    print o["t_abs"]

eventFile.close()

