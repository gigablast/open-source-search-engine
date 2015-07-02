#!/usr/bin/python
# -*- coding: utf-8 -*-


import requests
import json
import re
import subprocess
import multiprocessing

#Generate environment with:
#pex -r requests -r multiprocessing -e inject:main -o warc-inject -s '.' --no-wheel
#pex -r requests -r multiprocessing -o warc-inject


def injectItem(item):
    metadata = subprocess.Popen(['./ia','metadata', item], stdout=subprocess.PIPE).communicate()[0]
    #print 'item metadata is ', metadata, 'item is ', item
    md = json.loads(metadata)
    for ff in md['files']:
        if not ff['name'].endswith('arc.gz'): continue
        itemMetadata = {'mtime':ff['mtime']}
        itemMetadata.update(md['metadata'])
        postVars = {'url':'http://archive.org/download/%s/%s' %(item,ff['name']),
                    'metadata':json.dumps(itemMetadata),
                    'c':'ait'}
        print "sending", postVars,' to gb'
        rp = requests.post("http://localhost:8000/admin/inject", postVars)
        print postVars['url'], rp.status_code



def getPage(page):
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
        injectItem(item)

    return len(items)



def main():
    #getPage(4)
    from multiprocessing.pool import ThreadPool
    pool = ThreadPool(processes=5)
    print pool.map(getPage, xrange(1,1200))
    

if __name__ == '__main__':
    main()


