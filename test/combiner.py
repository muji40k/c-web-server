#! /bin/python

import csv

sizes = [1, 5, 10, 50, 100, 500, 1000, 5000]

def nextn(iter, n: int) -> list[str]:
    for _ in range(n - 1):
        next(iter)

    return next(iter)

def combine(filea : str, fileb : str, filer : str):
    fa = open(filea, "r")
    fb = open(fileb, "r")
    fr = open(filer, "w")

    ra = csv.reader(fa)
    rb = csv.reader(fb)
    rr = csv.writer(fr)
    rr.writerow(["req", "timeapp", "timenginx", "fraction"])

    run = True
    next(ra)
    next(rb)
    rowa = next(ra)
    rowb = next(rb)

    while (run):
        try:
            rr.writerow([rowa[0], rowa[4], rowb[4],
                         "{:0.6f}".format(float(rowb[4]) / float(rowa[4]))])
            rowa = nextn(ra, 10)
            rowb = nextn(rb, 10)
        except StopIteration:
            run = False

    fa.close()
    fb.close()
    fr.close()

def main() -> int:
    for i in sizes:
        combine(f"converted/app/{i}", f"converted/nginx/{i}", f"combined/{i}")

    return 0

if __name__ == "__main__":
    exit(main())

