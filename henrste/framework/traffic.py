"""
This file contains different functions for generating traffic for a test
"""

import os
from plumbum.cmd import ssh
from .test_framework import Logger, get_log_cmd, add_known_pid, kill_pid

def tcp_netcat(dry_run, testbed, hint_fn, run_fn, node='a', tag=None):
    """
    Run TCP traffic with netcat (nc)
    """
    server_port = testbed.get_next_traffic_port()

    node = 'A' if node == 'a' else 'B'

    hint_fn('traffic=tcp type=netcat node=%s%s server=%d tag=%s' % (node, node, server_port, 'No-tag' if tag is None else tag))

    cmd1 = ssh['-tt', os.environ['IP_SERVER%s_MGMT' % node], 'cat /dev/zero | nc -l %d >/dev/null' % server_port]
    cmd2 = ssh['-tt', os.environ['IP_CLIENT%s_MGMT' % node], 'sleep 0.2; nc -d %s %d >/dev/null' % (os.environ['IP_SERVER%s' % node], server_port)]

    if dry_run:
        Logger.debug(get_log_cmd(cmd1))
        Logger.debug(get_log_cmd(cmd2))

        def stopTest():
            pass

    else:
        pid1 = run_fn(cmd1, bg=True)
        pid2 = run_fn(cmd2, bg=True)
        add_known_pid(pid1)
        add_known_pid(pid2)

        def stopTest():
            kill_pid(pid1)
            kill_pid(pid2)

    return stopTest

def tcp_iperf(dry_run, testbed, hint_fn, run_fn, node='a', tag=None):
    """
    Run TCP traffic with iperf2
    """
    server_port = testbed.get_next_traffic_port()

    node = 'A' if node == 'a' else 'B'

    hint_fn('traffic=tcp type=iperf2 node=%s%s client=%d tag=%s' % (node, node, server_port, 'No-tag' if tag is None else tag))

    cmd1 = ssh['-tt', os.environ['IP_CLIENT%s_MGMT' % node], 'iperf -s -p %d' % server_port]
    cmd2 = ssh['-tt', os.environ['IP_SERVER%s_MGMT' % node], 'sleep 0.2; iperf -c %s -p %d -t 86400' % (os.environ['IP_CLIENT%s' % node], server_port)]

    Logger.debug(get_log_cmd(cmd1))
    Logger.debug(get_log_cmd(cmd2))
    if dry_run:
        def stopTest():
            pass

    else:
        pid1 = run_fn(cmd1, bg=True)
        pid2 = run_fn(cmd2, bg=True)
        add_known_pid(pid1)
        add_known_pid(pid2)

        def stopTest():
            kill_pid(pid1)
            kill_pid(pid2)

    return stopTest

def scp(dry_run, testbed, hint_fn, run_fn, node='a', tag=None):
    """
    Run TCP traffic with SCP (SFTP)

    Note there are some issues with the window size inside
    SSH as it uses its own sliding window. This test is therefore
    not reliable with a high BDP

    See:
    - http://www.slideshare.net/datacenters/enabling-high-performance-bulk-data-transfers-with-ssh
    - http://stackoverflow.com/questions/8849240/why-when-i-transfer-a-file-through-sftp-it-takes-longer-than-ftp

    All traffic goes over port 22 as of now. Tagging is
    not really possible because of this.
    """
    server_port = -1

    node = 'A' if node == 'a' else 'B'

    hint_fn('traffic=tcp type=scp node=%s%s server=%s tag=%s' % (node, node, server_port, 'No-tag' if tag is None else tag))

    cmd = ssh['-tt', os.environ['IP_SERVER%s_MGMT' % node], 'scp /opt/testbed/bigfile %s:/tmp/' % (os.environ['IP_CLIENT%s' % node])]

    Logger.debug(get_log_cmd(cmd))
    if dry_run:
        def stopTest():
            pass

    else:
        pid_server = run_fn(cmd, bg=True)
        add_known_pid(pid_server)

        def stopTest():
            kill_pid(pid_server)

    return stopTest

def greedy(dry_run, testbed, hint_fn, run_fn, node='a', tag=None):
    """
    Run greedy TCP traffic

    Greedy = always data to send, full frames

    node: a or b (a is normally classic traffic, b is normally l4s)

    Tagging makes it possible to map similar traffic from multiple tests,
    despite being different ports and setup

    Returns a lambda to stop the traffic
    """
    server_port = testbed.get_next_traffic_port()

    node = 'A' if node == 'a' else 'B'

    hint_fn('traffic=tcp type=greedy node=%s%s server=%s tag=%s' % (node, node, server_port, 'No-tag' if tag is None else tag))

    cmd1 = ssh['-tt', os.environ['IP_SERVER%s_MGMT' % node], '/opt/testbed/greedy_generator/greedy -vv -s %d' % server_port]
    cmd2 = ssh['-tt', os.environ['IP_CLIENT%s_MGMT' % node], 'sleep 0.2; /opt/testbed/greedy_generator/greedy -vv %s %d' % (os.environ['IP_SERVER%s' % node], server_port)]

    Logger.debug(get_log_cmd(cmd1))
    Logger.debug(get_log_cmd(cmd2))
    if dry_run:
        def stopTest():
            pass

    else:
        pid_server = run_fn(cmd1, bg=True)
        pid_client = run_fn(cmd2, bg=True)
        add_known_pid(pid_server)
        add_known_pid(pid_client)

        def stopTest():
            kill_pid(pid_server)
            kill_pid(pid_client)

    return stopTest

def udp(dry_run, testbed, hint_fn, run_fn, bitrate, node='a', ect="nonect", tag=None):
    """
    Run UDP traffic at a constant bitrate

    ect: ect0 = ECT(0), ect1 = ECT(1), all other is Non-ECT

    Tagging makes it possible to map similar traffic from multiple tests,
    despite being different ports and setup

    Returns a lambda to stop the traffic
    """

    tos = ''
    if ect == 'ect1':
        tos = "--tos 0x01" # ECT(1)
    elif ect == 'ect0':
        tos="--tos 0x02" # ECT(0)
    else:
        ect = 'nonect'

    server_port = testbed.get_next_traffic_port()

    node = 'A' if node == 'a' else 'B'

    hint_fn('traffic=udp node=%s%s client=%s rate=%d ect=%s tag=%s' % (node, node, server_port, bitrate, ect, 'No-tag' if tag is None else tag))

    cmd_server = ssh['-tt', os.environ['IP_CLIENT%s_MGMT' % node], 'iperf -s -p %d' % server_port]

    # bitrate to iperf is the udp data bitrate, not the ethernet frame size as we want
    framesize = 1514
    headers = 42
    length = framesize - headers
    bitrate = bitrate * length / framesize

    cmd_client = ssh['-tt', os.environ['IP_SERVER%s_MGMT' % node], 'sleep 0.5; iperf -c %s -p %d %s -u -l %d -R -b %d -i 1 -t 99999' %
                      (os.environ['IP_CLIENT%s' % node], server_port, tos, length, bitrate)]

    Logger.debug(get_log_cmd(cmd_server))
    Logger.debug(get_log_cmd(cmd_client))
    if dry_run:
        def stopTest():
            pass

    else:
        pid_server = run_fn(cmd_server, bg=True)
        pid_client = run_fn(cmd_client, bg=True)

        add_known_pid(pid_server)
        add_known_pid(pid_client)

        def stopTest():
            kill_pid(pid_client)
            kill_pid(pid_server)

    return stopTest