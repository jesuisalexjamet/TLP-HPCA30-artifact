#!/usr/bin/python
import os
import csv
for root, dirs, files in os.walk("."):
    
    if root == ".":
        continue

    os.chdir(root)

    fp_sim = open(files[1])
    fp_wgt = open(files[0])

    dict_local = {}
    line = fp_sim.readline()
    linen = fp_wgt.readline()
    while line:
        dict_local[line.strip()] = float(linen.strip())
        line = fp_sim.readline()
        linen = fp_wgt.readline()
    
    print dict_local
    with open("concat.txt", "w") as write_file: 
        # trace_identifier ; weight <--- this is the format that I store them
        for index, item in dict_local.items():
            write_file.write(index + ' ; ' + str(item) + '\n')
    os.chdir('../')
