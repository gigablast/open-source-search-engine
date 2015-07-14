#!/usr/bin/python
# -*- coding: utf-8 -*-


import requests
import json
import re
import subprocess
import multiprocessing
import sqlite3
import datetime
import sys

#Generate environment with:
#pex -r requests -r multiprocessing -e inject:main -o warc-inject -s '.' --no-wheel
#pex -r requests -r multiprocessing -o warc-inject
# import argparse
# parser = argparse.ArgumentParser()
# parser.add_argument('--foo', help='foo help')
# args = parser.parse_args()

def injectItem(item, c):
    metadata = subprocess.Popen(['./ia','metadata', item],
                                stdout=subprocess.PIPE).communicate()[0]
    #print 'item metadata is ', metadata, 'item is ', item
    md = json.loads(metadata)

    res = c.execute("select * from items where item = ?", (item,))
    lastUpdate = {}
    for item, fileName, updated, status in res:
        lastUpdate[fileName] = updated
        pass

    for ff in md['files']:
        if not ff['name'].endswith('arc.gz'): continue
        itemMetadata = {'mtime':ff['mtime']}
        updateTime = datetime.datetime.fromtimestamp(float(ff['mtime']))
        if ff['name'] in lastUpdate and updateTime <= lastUpdate[ff['name']]:
            print "skip {0} because it is up to date".format(ff)
            continue
        
        itemMetadata.update(md['metadata'])
        postVars = {'url':'http://archive.org/download/%s/%s' %
                    (item,ff['name']),
                    'metadata':json.dumps(itemMetadata),
                    'c':'ait'}
        print "sending", postVars,' to gb'
        if True:
            rp = requests.post("http://localhost:8000/admin/inject", postVars)
            statusCode = rp.status_code
            print postVars['url'], rp.status_code
        else:
            statusCode = 999

        c.execute("INSERT INTO items VALUES (?,?,?,?)",
                  (item, ff['name'], updateTime, statusCode))
        c.commit()

def getPage(page):
    db = sqlite3.connect('items.db',detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES)

    #r = requests.get('https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&rows=1&page={0}&output=json&save=yes'.format(page))
    r = requests.get('https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&rows=1000&page={0}&output=json&save=yes'.format(page))
    if r.status_code != 200:
        return 0

    contents = r.content
    jsonContents = json.loads(contents)
    items = [x['identifier'] for x in jsonContents['response']['docs']]
    if len(items) == 0:
        return 0
    print 'loading %s items, %s - %s' % (len(items), items[0], items[-1])
    for item in items:
        injectItem(item, db)

    return len(items)


def dumpDb():
    db = sqlite3.connect('items.db', detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES)
    c = db.cursor()
    res = c.execute("select * from items")    
    for item, fileName, updated, status in res:
        print item, fileName, updated, status


def main():
    print 'arguments were', sys.argv
    if len(sys.argv) == 2:
        if sys.argv[1] == 'init':
            init()
            print 'initialized'
            return sys.exit(0)
        if sys.argv[1] == 'reset':
            import os
            os.unlink('items.db')
            init()
            return sys.exit(0)
        if sys.argv[1] == 'dump':
            dumpDb()
    else:
        #getPage(4)
        from multiprocessing.pool import ThreadPool
        pool = ThreadPool(processes=50)
        print pool.map(getPage, xrange(1,1300))
    

def init():
    db = sqlite3.connect('items.db', detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES)
    c = db.cursor()
    c.execute('''CREATE TABLE items
             (item text, file text, updated timestamp, status integer)''')
    db.commit()
    db.close()

if __name__ == '__main__':
    main()
