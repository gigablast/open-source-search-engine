#!/usr/bin/python
# -*- coding: utf-8 -*-

import subprocess
import re
import requests
import json
import sys

def genHostsConf(fname, ips, numHostsPerMachine):
    """ Take an old hosts file and a list of new machine ips
    and generate a new hosts file with the new cluster
    """

    fh = open(fname, 'r')
    ret = {}
    for line in fh.readlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith('#'):
            continue
        try:
            startHostId, startDnsPort, startHttpsPort, startHttpPort, startUdpPort,ip1, ip2, directory, note= re.split('\s+', line, 8)
            break
        except:
            pass

    hostCount = 0
    for ip in ips:
        dnsPort = int(startDnsPort)
        httpsPort = int(startHttpsPort)
        httpPort = int(startHttpPort)
        udpPort = int(startUdpPort)
        directory = 0
        for diskId in xrange(numHostsPerMachine):
            print "{0} {1} {2} {3} {4} {5} {6} /{7:02d}/gigablast/ /{7:02d}/gigablast/".format(hostCount, dnsPort, httpsPort, httpPort, udpPort, ip, ip, directory)
            hostCount, dnsPort, httpsPort, httpPort, udpPort, directory = \
            hostCount+ 1, dnsPort+ 1, httpsPort+ 1, httpPort+ 1, udpPort + 1, directory + 1
            
    print "num-mirrors: 1"
    return
    
def parseHostsConf(fname):
    fh = open(fname, 'r')
    ret = {}
    for line in fh.readlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith('#'):
            continue
        try:
            hostId, dnsPort, httpsPort, httpPort, udbPort,ip1, ip2, directory, note= re.split('\s+', line, 8)
        except:
            continue
            
        print directory, ip1, note
        #try:
        writeSpeed, readSpeed = testDiskSpeed(ip1, directory)
        #except:
        #writeSpeed, readSpeed = 0,0

        note = note[1:]
        group = re.split('\s+', note, 2)


        ret[hostId] = {"writeSpeed":writeSpeed,
                       "readSpeed":readSpeed,
                       "disk":directory,
                       "ip":ip1,
                       "hostId":hostId,
                       "note":note,
                       "group":group[0] + ' ' + group[1]
        }

            # if note.find('novm'):
            #     ret[hostId]['vm'] = 0
            # else:
            #     ret[hostId]['vm'] = 1

            # if note.find('noht'):
            #     ret[hostId]['ht'] = 0
            # else:
            #     ret[hostId]['ht'] = 1
        
    print json.dumps(ret, indent=4)
    return ret
# if __name__ == "__main__":
#     parseHostsConf('../hosts.conf.cluster')

def getSplitTime():
    hostsTable = 'http://207.241.225.222:8000/admin/hosts?c=ait&sort=13'
    qq = requests.get(hostsTable)
    d = pyquery.pyquery.PyQuery(qq.content)
    cells = d('td')
    for cell in cells.items():
        print cell.text(), '***'




def copyToTwins(fname, backToFront=False):
    fh = open(fname, 'r')
    ret = {}
    hosts = []
    for line in fh.readlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith('#'):
            continue
        try:
            hostId, dnsPort, httpsPort, httpPort, udbPort,ip1, ip2, directory, note= re.split('\s+', line, 8)
            hosts.append((int(hostId), int(dnsPort), int(httpsPort), int(httpPort), int(udbPort),ip1, ip2, directory, note))
        except:
            continue
        #print directory, ip1, note
    step = len(hosts)/2
    cmds = []
    for hostId, dnsPort, httpsPort, httpPort, udbPort,ip1, ip2, directory, note in hosts[:step]:
        backHostId, backDnsPort, backHttpsPort, backHttpPort, backUdbPort,backIp1, backIp2, backDirectory, backNote = hosts[hostId + step]

        if note != directory:
            print 'oh looks like you overlooked host %s' % hostId
        if backNote != backDirectory:
            print 'oh looks like you overlooked host %s' % backHostId

        if backToFront:
            cmd = 'scp -r %s:%s* %s:%s. &' % (backIp1, backDirectory, ip1, directory )
        else:
            cmd = 'scp -r %s:%s* %s:%s. &' % (ip1, directory, backIp1, backDirectory)
        cmds.append(cmd)
        #print 'scp -r %s:%s* %s:%s. &' % (ip1, directory, (hosts[hostId + step][5]), (hosts[hostId + step][7]))

    for cmd in cmds:
        print cmd



def testDiskSpeed(host, directory):
    writeSpeedOut = subprocess.Popen('eval `ssh-agent`;ssh-add ~/.ssh/id_ecdsa; ssh {0} "cd {1};dd if=/dev/zero of=test bs=1048576 count=2048"'.format(host,directory),
                                     stdout=subprocess.PIPE,stderr=subprocess.PIPE, shell=True).communicate()[1]
    print 'matching **', writeSpeedOut, '**'

    writeSpeed = re.match('.* (\d+\.*\d+) MB/s.*', writeSpeedOut, flags=re.DOTALL)
    if writeSpeed:
        writeSpeed = float(writeSpeed.group(1))
    else:
        writeSpeed = re.match('.* (\d+\.?\d+) GB/s.*', writeSpeedOut, flags=re.DOTALL)
        if writeSpeed:
            writeSpeed = float(writeSpeed.group(1)) * 1000

    readSpeedOut = subprocess.Popen('eval `ssh-agent`;ssh-add ~/.ssh/id_ecdsa; ssh {0} "cd {1};dd if=test of=/dev/null bs=1048576"'.format(host,directory),
                                     stdout=subprocess.PIPE,stderr=subprocess.PIPE, shell=True).communicate()[1]

    readSpeed = re.match('.* (\d+\.?\d+) MB/s.*', readSpeedOut, flags=re.DOTALL)
    if readSpeed:
        float(readSpeed.group(1))
    else:
        readSpeed = re.match('.* (\d+\.*\d+) GB/s.*', readSpeedOut, flags=re.DOTALL)
        if readSpeed:
            readSpeed = float(readSpeed.group(1)) * 1000


    return writeSpeed, readSpeed


def graphDiskSpeed(data):
    reads = []
    writes = []
    readByGroup = {}
    writeByGroup = {}
    for hostId, stats in data.iteritems():
        if stats['group'] not in readByGroup:
            readByGroup[stats['group']] = []
        readByGroup[stats['group']].append({'y':stats['readSpeed'], 'label':hostId})

        if stats['group'] not in writeByGroup:
            writeByGroup[stats['group']] = []
        writeByGroup[stats['group']].append({'y':stats['writeSpeed'], 'label':hostId})
        
    colors = ['#5996CE', '#385E82', "#CFB959", "#826838"]

    for group, dataPoints in readByGroup.iteritems():
        for xx in dataPoints:
            xx['y'] = float(xx['y']) / 1000.0

        color = colors.pop()
        reads.append({
            'type': "bar",
            'showInLegend': True,
            'name': group,
            'color': color,
            'dataPoints': dataPoints
        })
        writes.append({
            'type': "bar",
            'showInLegend': True,
            'name': group,
            'color': color,
            'dataPoints': writeByGroup[group]
        })
    print json.dumps(reads)
    print json.dumps(writes)


def installCluster():
    genHostsConf('../hosts.conf', sys.argv[2:], 12)


if __name__ == '__main__':
    if len(sys.argv) > 0:
        if sys.argv[1] == 'test':
            parseHostsConf('../hosts.conf.cluster')
        if sys.argv[1] == 'graph':
            graphDiskSpeed(json.loads(open(sys.argv[2], 'r').read()))

        if sys.argv[1] == 'newhosts':
            genHostsConf('../hosts.conf.cluster', sys.argv[2:], 12)

            

            
    #getSplitTime()







