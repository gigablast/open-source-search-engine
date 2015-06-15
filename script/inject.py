# -*- coding: utf-8 -*-


import requests
import json
import re
import subprocess
import multiprocessing




def getItems():
    r = requests.get('https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&rows=200&page=1&output=json&callback=callback&save=yes')
    if r.status_code != 200:
        return []
    # jsonp reply is callback(.*), so strip that
    contents = r.content[9:-1]
    print contents
    jsonContents = json.loads(contents)
    items = [x['identifier'] for x in jsonContents['response']['docs']]
    return items


def injectItem(item):
    metadata = subprocess.Popen(['./ia','metadata', item], stdout=subprocess.PIPE).communicate()[0]
    print 'item metadata is ', metadata, 'item is ', item
    md = json.loads(metadata)
    for ff in md['files']:
        print ff['name'], 'is the name'
        if not ff['name'].endswith('arc.gz'): continue
        itemMetadata = {'mtime':ff['mtime']}
        itemMetadata.update(md['metadata'])
        postVars = {'url':'http://archive.org/download/%s/%s' %(item,ff['name']),
                    'metadata':json.dumps(itemMetadata)}
        print "sending", postVars,' to gb'
        rp = requests.post("http://localhost:8000/admin/inject", postVars)
        print rp.content



def getPage(page):
    print 'https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&rows=1000&page={0}&output=json&callback=callback&save=yes'.format(page)
    r = requests.get('https://archive.org/advancedsearch.php?q=collection%3Aarchiveitdigitalcollection&fl%5B%5D=identifier&rows=1000&page={0}&output=json&callback=callback&save=yes'.format(page))

    if r.status_code != 200:
        return []
    # jsonp reply is callback(.*), so strip that
    contents = r.content[9:-1]
    jsonContents = json.loads(contents)
    items = [x['identifier'] for x in jsonContents['response']['docs']]
    if len(items) == 0:
        return
    print 'loading %s items, %s - %s' % (len(items), items[0], items[-1])
    for item in items:
        injectItem(item)

    return len(items)

def main():
    getPage(4)
    # pool = multiprocessing.Pool(processes=5)
    # print pool.map(getPage, xrange(1,120000))

    # items = getItems()
    # for item in items:
    #     injectItem(item)
            
    # print items
    
if __name__ == '__main__':
    main()
# payload = dict(key1='value1', key2='value2')
# requests.post("http://httpbin.org/post", data=payload))


