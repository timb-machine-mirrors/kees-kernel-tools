#!/usr/bin/env python3
# Author: Kees Cook <kees@kernel.org>
# License: MIT
# Reworked from the original Perl implementation:
# https://github.com/curl/stats/blob/master/codeage.pl
#
# Extract per-line commit dates for each git tag for graphing code ages.
# Inspired by:
# https://fosstodon.org/@bagder@mastodon.social/113399049650160188
# https://github.com/curl/stats/blob/master/codeage.pl
# https://github.com/curl/stats/blob/master/codeage.plot
#
# cd ~/src/linux && year-annotate.py -d | tee codeage.csv
import glob, sys, os
import json
import pickle
import optparse
import datetime
from subprocess import *
from itertools import repeat
from multiprocessing import Pool, cpu_count
import tqdm
from packaging.version import Version

# Globals (FIXME: turn this into a proper object)
parser = optparse.OptionParser()
parser.add_option("-d", "--debug", help="Report additional debugging while processing USNs", action='store_true')
parser.add_option("-C", "--csv", help="Report as CSV file", action='store_true')
(opt, args) = parser.parse_args()

devnull = open("/dev/null", "w")

gittree = os.path.expanduser("~/src/linux-build/master")

# TODO: split caches by tag -- less to write after each cycle
cachefile = os.path.expanduser("~/.cache/year-blame.pickle")
if os.path.exists(cachefile):
    if opt.debug:
        print("Loading cache...", file=sys.stderr)
    cache = pickle.load(open(cachefile, 'rb'))
    try:
        if 'v2.6.12' in cache:
            print("Refactoring cache...", file=sys.stderr)
            replace = dict()
            replace['tags'] = cache
            replace.setdefault('years', dict())
            cache = replace
    except:
        pass
else:
    cache = dict()
    cache.setdefault('tags', dict())
    cache.setdefault('years', dict())

def run(cmd):
    #if opt.debug:
    #    print(cmd, file=sys.stderr)
    return Popen(cmd, stdout=PIPE, stderr=devnull).communicate()[0].decode("utf-8", "ignore")

def sha_to_date(sha):
    output = run(["git", "show", "--pretty=%at", "-s", "%s^{commit}" % (sha)])
    epoch = output.strip()
    date = datetime.datetime.fromtimestamp(float(epoch))
    return date

def annotate(tag, file):
    dates = []
    ann = run(['git', 'annotate', '-t', '--line-porcelain', file, tag]).splitlines()
    for line in ann:
        if line.startswith('committer-time '):
            tag, value = line.split(' ', 1)
            dates.append(datetime.datetime.fromtimestamp(float(value)).year)
    return dates

def frombefore(before, stamps):
    count = 0
    for stamp in stamps:
        if stamp < before:
            count += 1
    return count

def process(tag, years):
    date = sha_to_date(tag)
    stamps = []
    if not tag in cache['tags']:
        if opt.debug:
            print(date.strftime('Processing files at %Y-%m-%d ...'), file=sys.stderr)
        # FIXME: do we want to exclude Documentation, samples, or tools subdirectories?
        # Or MAINTAINERS, dot files, etc?
        files = run(['git', 'ls-tree', '-r', '--name-only', tag]).splitlines()
        count = len(files)

        with Pool(cpu_count()) as p:
            results = p.starmap(annotate,
                                tqdm.tqdm(zip(repeat(tag), files), total=count))
                                #zip(repeat(tag), files))
            # It seems we get an array of arrays from starmap()
            for result in results:
                stamps += result

        cache['tags'].setdefault(tag, dict())
        cache['tags'][tag] = stamps
        # Save this tag's stamps!
        if opt.debug:
            print("Writing cache...", file=sys.stderr)
        pickle.dump(cache, open(cachefile, 'wb'), -1)
    else:
        stamps = cache['tags'][tag]

    day = date.strftime('%Y-%m-%d')
    report = day
    if not day in cache['years']:
        if opt.debug:
            print('Scanning ages ...                  ', file=sys.stderr)
        for year in years:
            report += ';%u' % (frombefore(year, stamps))
        # Save report
        cache['years'].setdefault(year, dict())
        cache['years'][day] = report
        if opt.debug:
            print("Writing cache...", file=sys.stderr)
        pickle.dump(cache, open(cachefile, 'wb'), -1)
    else:
        report = cache['years'][day]
    print(report)

# Get the list of tags we're going to operate against
output = run(["git", "tag"])
# We want v2.6.Z and vX.Y only. No -rc, no stable versions, also not v2.6.11
# almost: '^v(2\.6|[0-9])\.[0-9]*$'
tags = [x
        for x in output.strip().splitlines()
        if x.startswith('v') and
           '-' not in x and
           x != 'v2.6.11' and
           ((x.startswith('v2.6.') and len(x.split('.')) == 3) or len(x.split('.')) == 2)
       ]
tags.sort(key=Version)
if opt.debug:
    print(tags, file=sys.stderr)

# Find the year bounds of our tags
year_first = sha_to_date(tags[0]).year + 1
year_last  = sha_to_date(tags[-1]).year + 1
if opt.debug:
    print("%d .. %d" % (year_first, year_last), file=sys.stderr)
years = range(year_first, year_last + 1, 1)

# Walk tags
for tag in tags:
    process(tag, years)
