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
import time

#Generate environment with:
#pex -r requests -r multiprocessing -e inject:main -o warc-inject -s '.' --no-wheel
#pex -r requests -r multiprocessing -o warc-inject
# import argparse
# parser = argparse.ArgumentParser()
# parser.add_argument('--foo', help='foo help')
# args = parser.parse_args()

def injectItem(item, c):
    while True:
        try:
            metadata = subprocess.Popen(['./ia','metadata', item],
                                        stdout=subprocess.PIPE).communicate()[0]
            #print 'item metadata is ', metadata, 'item is ', item
            md = json.loads(metadata)
            break
        except Exception, e:
            print 'error: metadata feed went down (%s) for: %s' % (e, item)
            time.sleep(10)
            
    if 'files' not in md:
        return

    res = None
    while True:
        try:
            res = c.execute("select * from items where item = ?", (item,))
            c.commit() # Getting database is locked errors, will this help?
            break
        except sqlite3.OperationalError:
            time.sleep(1)
            print 'got locked database on select, retrying'
            continue


    lastUpdate = {}
    for item, fileName, updated, status, took in res:
        
        if status == -1: # Auto retry if we couldn't reach gb last time
            continue
        lastUpdate[fileName] = updated

    dbUpdates = []
    for ff in md['files']:
        if not ff['name'].endswith('arc.gz'): continue
        itemMetadata = {'mtime':ff['mtime']}
        updateTime = datetime.datetime.fromtimestamp(float(ff['mtime']))
        if ff['name'] in lastUpdate and updateTime <= lastUpdate[ff['name']]:
            print "skip {0} because it is up to date".format(ff['name'])
            continue
        
        itemMetadata.update(md['metadata'])
        postVars = {'url':'http://archive.org/download/%s/%s' %
                    (item,ff['name']),
                    'metadata':json.dumps(itemMetadata),
                    'c':'ait'}
        start = time.time()
        if True:
            try:
                rp = requests.post("http://localhost:8000/admin/inject", postVars)
                statusCode = rp.status_code

            except requests.exceptions.ConnectionError, e:
                print 'error: gb inject', postVars['url'], e
                statusCode = -1
            #print postVars['url'], rp.status_code
        else:
            statusCode = 999
        took = time.time() - start

        print "sent", ff['name'],'to gb, took', took
        sys.stdout.flush()

        dbUpdates.append((item, ff['name'], updateTime, statusCode, took))


    while True:
        try:
            c.executemany("INSERT INTO items VALUES (?,?,?,?,?)",
                          dbUpdates)
            c.commit()
            break
        except sqlite3.OperationalError:
            time.sleep(10)
            print 'got locked database on insert, retrying'
            continue
    
    print 'completed %s with %s items injected' % (item, len(dbUpdates))

def getPage(page):

    #r = requests.get('https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&rows=1&page={0}&output=json&save=yes'.format(page))
    r = requests.get('https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&sort[]=date+desc&rows=100&page={0}&output=json&save=yes'.format(page))
    if r.status_code != 200:
        return 0

    contents = r.content
    jsonContents = json.loads(contents)
    items = [x['identifier'] for x in jsonContents['response']['docs']]
    if len(items) == 0:
        print 'got 0 items for search page', page
        return 0
    print 'loading %s items, %s - %s' % (len(items), items[0], items[-1])

    db = sqlite3.connect('items.db',
                         isolation_level=None,
                         detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES,
                         timeout=30.0)

    for item in items:
        injectItem(item, db)

    db.close()
    return len(items)


def dumpDb():
    db = sqlite3.connect('items.db', detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES, timeout=30.0)
    c = db.cursor()
    res = c.execute("select * from items")    
    for item, fileName, updated, status, took in res:
        print item, fileName, updated, status
    db.close()

def showItems():
    db = sqlite3.connect('items.db', detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES, timeout=30.0)
    c = db.cursor()
    res = c.execute("select distinct item from items")    
    for item, in res:
        print item
    db.close()



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

        if sys.argv[1] == 'items':
            showItems()

        if sys.argv[1] == 'kill':
            subprocess.Popen("""kill `ps auxx |grep warc-inject|awk -e '{print $2}'`""",
                             shell=True,stdout=subprocess.PIPE).communicate()[0]


    else:
        #getPage(3)
        from multiprocessing.pool import ThreadPool
        pool = ThreadPool(processes=150)
        pool.map(getPage, xrange(1,1300))
        
    

def init():
    db = sqlite3.connect('items.db', detect_types=sqlite3.PARSE_DECLTYPES|sqlite3.PARSE_COLNAMES, timeout=30.0 )
    c = db.cursor()
    c.execute('''CREATE TABLE items
             (item text, file text, updated timestamp, status integer, took float)''')
    db.commit()
    db.close()

if __name__ == '__main__':
    main()
