#!/usr/bin/python

import json;
import sys;

gitdir=sys.argv[1]

filename='trace2-event-' + gitdir + '.txt'

eventFile = open(filename, 'r')
decoder = json.JSONDecoder()

print "maybe, definitely_not, false_positive"

while True:
    line = eventFile.readline()

    if not line:
        break

    if line.find("statistics") < 0:
        continue

    o = decoder.decode(line)
    v = o["value"]

    print v["maybe"], ",", v["definitely_not"], ",", v["false_positive"]

eventFile.close()

