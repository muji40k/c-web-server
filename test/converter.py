#! /bin/python

import csv
import sys

def main() -> int:
    if (1 == len(sys.argv)):
        return 1;

    file_in = open(sys.argv[1], "r")
    file_out = open("converted/" + sys.argv[1], "w")
    reader = csv.reader(file_in);
    writer = csv.writer(file_out);

    funcs = {
        "min_time":   lambda values: min(values),
        "max_time":   lambda values: max(values),
        "time_range": lambda values: max(values) - min(values),
        "avg_time":   lambda values: sum(values) / len(values)
    }

    writer.writerow(["req"] + list(funcs.keys()))
    funcsv = funcs.values()

    for i, row in enumerate(reader):
        if (0 == i):
            continue

        values = [float(j) for j in row[1:]]
        resi = map(lambda x: x(values), funcsv)
        ress = [row[0]] + list(map(lambda x: "{:0.6f}".format(x), resi))
        writer.writerow(ress);

    file_in.close()
    file_out.close()

    return 0;

if __name__ == "__main__":
    exit(main())

