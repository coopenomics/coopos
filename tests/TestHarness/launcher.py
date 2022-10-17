#!/usr/bin/env python3

import argparse
from dataclasses import dataclass, field
import os
import pathlib
import math
import string
import sys
from typing import Dict, List

from testUtils import Utils

block_dir = 'blocks'

@dataclass
class KeyStrings(object):
    pubkey: str
    privkey: str

@dataclass
class nodeDefinition:
    name: str
    keys: List[str] = field(init=False)
    peers: List[str] = field(init=False)
    producers: List[str] = field(init=False)
    #gelf_endpoint: str
    dont_start: bool = field(init=False, default=False)
    config_dir_name: str = field(init=False)
    data_dir_name: str = field(init=False)
    p2p_port: int = field(init=False)
    http_port: int = field(init=False)
    #file_size: int
    instance_name: str = field(init=False)
    #host: str
    p2p_endpoint: str = field(init=False)
    host_name: str = 'localhost'
    public_name: str = field(init=False)
    listen_addr: str = field(init=False)
    _dot_label: str = ''
    
    @property
    def dot_label(self):
        if self._dot_label.empty():
            self.mk_host_dot_label()
        return self._dot_label
    
    def mk_dot_label(self):
        self._dot_label = self.name + '\nprod='
        if len(self.producers) > 0:
            self._dot_label += ','.join(self.producers)
        else:
            self._dot_label += '<none>'

    def mk_host_dot_label(self):
        if self.public_name.empty():
            self._dot_label = self.host_name
        elif self.public_name == self.host_name:
            self._dot_label = self.public_name
        else:
            self._dot_label = self.public_name + '/' + self.host_name

@dataclass
class testnetDefinition:
    name: str
    nodes: Dict[str, nodeDefinition] = field(init=False, default_factory=dict)


def producer_name(producer_number: int, shared_producer: bool = False):
    '''Currently only supports 26 producer names; original supports approx. 26^6'''
    return ('shr' if shared_producer else 'def') + 'producer' + string.ascii_lowercase[producer_number]

class launcher(object):
    def __init__(self, args):
        self.args = self.parseArgs(args)
        self.network = testnetDefinition(self.args.network_name)
        self.aliases: List[str] = []
        self.next_node = 0

        self.define_network()
        self.generate()

    def parseArgs(self, args):
        '''Configure argument parser and use it on the passed in list of strings.
        
        arguments:
        args -- list of arguments (may be sys.argv[1:] or synthetic list)
        
        returns -- argparse.Namespace object with parsed results
        '''
        def comma_separated(string):
            return string.split(',')
        parser = argparse.ArgumentParser(description='launcher command line options',
                                         formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument('-i', '--timestamp', help='set the timestamp for the first block.  Use "now" to indicate the current time')
        parser.add_argument('-l', '--launch', choices=['all', 'none', 'local'], help='select a subset of nodes to launch.  If not set, the default is to launch all unless an output file is named, in which case none are started.', default='all')
        parser.add_argument('-o', '--output', help='save a copy of the generated topology in this file', dest='topology_filename')
        parser.add_argument('-k', '--kill', help='retrieve the list of previously started process ids and issue a kill to each')
        parser.add_argument('--down', type=comma_separated, help='comma-separated list of node numbers that will be shut down')
        parser.add_argument('--bounce', type=comma_separated, help='comma-separated list of node numbers that will be restarted')
        parser.add_argument('--roll', type=comma_separated, help='comma-separated list of host names where the nodes will be rolled to a new version')
        parser.add_argument('--config-dir', help='directory containing configuration files such as config.ini', default=os.getcwd())
        parser.add_argument('-c', '--config', type=pathlib.Path, help='configuration file name relative to config-dir', default='config.ini')
        parser.add_argument('-v', '--version', action='version', version='%(prog)s 1.0')
        
        cfg = parser.add_argument_group(title='optional and config file arguments')
        cfg.add_argument('-f', '--force', action='store_true', help='force overwrite of existing configuration files and erase blockchain', default=False)
        cfg.add_argument('-n', '--nodes', dest='total_nodes', type=int, help='total number of nodes to configure and launch', default=2)
        cfg.add_argument('--unstarted-nodes', type=int, help='total number of nodes to configure, but not launch', default=0)
        cfg.add_argument('-p', '--pnodes', type=int, help='number of nodes that contain one or more producers', default=2)
        cfg.add_argument('--producers', type=int, help='total number of non-bios and non-shared producer instances in this network', default=21)
        cfg.add_argument('--shared-producers', type=int, help='total number of shared producers on each non-bios node', default=0)
        cfg.add_argument('-m', '--mode', choices=['any', 'producers', 'specified', 'none'], help='connection mode', default='any')
        cfg.add_argument('-s', '--shape', help='network topology, use "star", "mesh", "ring", "line" or give a filename for custom', default='star')
        cfg.add_argument('-g', '--genesis', help='set the path to genesis.json', default='./genesis.json')
        cfg.add_argument('--skip-signature', action='store_true', help='do not require transaction signatures', default=False)
        cfg.add_argument('--' + Utils.EosServerName, help=f'forward {Utils.EosServerName} command line argument(s) to each instance of {Utils.EosServerName}; enclose all arg(s) in quotes')
        cfg.add_argument('--specific-num', type=int, action='append', dest='specific_nums', default=[], help=f'forward {Utils.EosServerName} command line argument(s) (using "--specific-{Utils.EosServerName}" flag to this specific instance of {Utils.EosServerName}.  This parameter can be entered multiple times and requires a paired "--specific-{Utils.EosServerName}" flag each time it is used.')
        cfg.add_argument('--specific-' + Utils.EosServerName, action='append', dest=f'specific_{Utils.EosServerName}es', default=[], help=f'forward {Utils.EosServerName} command line argument(s) to its paired specific instance of {Utils.EosServerName} (using "--specific-num"); enclose arg(s) in quotes')
        cfg.add_argument('--spcfc-inst-num', type=int, action='append', dest='spcfc_inst_nums', default=[], help=f'specify a path to a binary (using "--spcfc-inst-{Utils.EosServerName}" flag) for launching this numbered instance of {Utils.EosServerName}.  This parameter can be entered multiple times and requires a paired "--spcfc-inst-{Utils.EosServerName}" flag each time it is used.')
        cfg.add_argument('--spcfc-inst-' + Utils.EosServerName, action='append', dest=f'spcfc_inst_{Utils.EosServerName}es', default=[], help=f'path to a binary to launch for the {Utils.EosServerName} instance number specified by the corresponding "--spcfc-inst-num" flag')
        cfg.add_argument('-d', '--delay', type=int, help='seconds delay before starting each node after the first', default=0)
        cfg.add_argument('--boot', action='store_true', help='after deploying the nodes, boot the network', default=False)
        cfg.add_argument('--nogen', action='store_true', help='launch nodes without writing new config files', default=False)
        cfg.add_argument('--host-map', help='name of file containing mapping specific nodes to hosts, used to enhance the custom shape argument')
        cfg.add_argument('--servers', help='name of a file containing ip addresses and names of individual servers to deploy as producers or non-producers')
        cfg.add_argument('--per-host', type=int, help='specifies how many instances will run on a single host.  Use 0 to indicate all on one', default=0)
        cfg.add_argument('--network-name', help='network name prefix used in GELF logging source', default='testnet_')
        cfg.add_argument('--enable-gelf-logging', action='store_true', help='enable gelf logging appender in logging configuration file', default=False)
        cfg.add_argument('--gelf-endpoint', help='hostname:port or ip:port of GELF endpoint', default='128.0.0.1:12201')
        cfg.add_argument('--template', help='the startup script template')
        cfg.add_argument('--script', help='the optionally generated startup script name', default='bios_boot.sh')
        cfg.add_argument('--max-block-cpu-usage', type=int, help='the "max-block-cpu-usage" value to use in the genesis.json file')
        cfg.add_argument('--max-transaction-cpu-usage', type=int, help='the "max-transaction-cpu-usage" value to use in the genesis.json file')
        r = parser.parse_args(args)
        if r.shape not in ['star', 'mesh', 'ring', 'line'] and not pathlib.Path(r.shape).is_file():
            parser.error('-s, --shape must be one of "star", "mesh", "ring", "line", or a file')
        if len(r.specific_nums) != len(getattr(r, f'specific_{Utils.EosServerName}es')):
            parser.error(f'Count of uses of --specific-num and --specific-{Utils.EosServerName} must match')
        if len(r.spcfc_inst_nums) != len(getattr(r, f'spcfc_inst_{Utils.EosServerName}es')):
            parser.error(f'Count of uses of --spcfc-inst-num and --spcfc-inst-{Utils.EosServerName} must match')
        print(r)
        return r

    def assign_name(self, is_bios):
        if is_bios:
            return 'bios'
        else:
            index = str(self.next_node)
            self.next_node +=1
            return self.network.name + index.zfill(2)

    def define_network(self):
        if self.args.per_host == 0:
            for i in range(self.args.total_nodes):
                node = nodeDefinition(self.assign_name(i == 0))
                self.aliases.append(node.name)
        else:
            ph_count = 0
            host_ndx = 0
            num_prod_addr = 1
            num_nonprod_addr = 1
            for i in range(self.args.total_nodes, 0, -1):
                do_bios = False
                lhost = nodeDefinition()
                if ph_count == 0:
                    if host_ndx < num_prod_addr:
                        do_bios = True # servers.producer[host_ndx].has_bios
                        lhost.host_name = '127.0.0.1' # servers.producer[host_ndx].ipaddr
                        lhost.public_name = 'localhost' # servers.producer[host_ndx].name
                        ph_count = 1 # servers.producer[host_ndx].instances
                    elif host_ndx - num_prod_addr < num_nonprod_addr:
                        ondx = host_ndx - num_prod_addr
                        do_bios = True # servers.nonprod[ondx].has_bios
                        lhost.host_name = '127.0.0.1' # servers.nonprod[ondx].ipaddr
                        lhost.public_name = 'localhost' # servers.nonprod[ondx].name
                        ph_count = 1 # servers.nonprod[ondx].instances
                    else:
                        lhost.host_name = f'pseudo_{host_ndx.zfil(2)}'
                        lhost.public_name = lhost.host_name
                        ph_count = 1
                    host_ndx += 1
                lhost.name = self.assign_name(do_bios)
                self.aliases.push_back(lhost.name)
                do_bios = False
                ph_count -= 1

    def bind_nodes(self):
        if self.args.pnodes < 2:
            raise RuntimeError(f'Unable to allocate producers due to insufficient pnodes = {self.args.pnodes}')
        non_bios = self.args.pnodes - 1
        per_node = self.args.producers / non_bios
        extra = self.args.producers % non_bios
        i = 0
        producer_number = 0
        to_not_start_node = self.args.total_nodes - self.args.unstarted_nodes - 1
        for node in self.network.nodes:
            is_bios = n.name == 'bios'
            node.keys.append(KeyStrings('EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV',
                                         '5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3'))
            if is_bios:
                node.producers.push_back('eosio')
            else:
                if i < non_bios:
                    count = per_node
                    if extra:
                        count += 1
                        extra -= 1
                    while count:
                        prodname = producer_name(producer_number)
                        node.producers.append(prodname)
                        producer_number += 1
                        count -= 1
                    for j in range(0, self.args.shared_producers):
                        prodname = producer_name(j, True)
                        node.producers.append(prodname)
                node.dont_start = i >= to_not_start_node
            if not is_bios:
                i += 1


    def generate(self):
        {
            'ring': self.make_line,
            'line': lambda: self.make_line(False),
            'star': self.make_star,
            'mesh': self.make_mesh,
        }.get(self.args.shape, self.make_custom)()

        if not self.args.nogen:
            for node in self.network.nodes:
                self.write_config_file(node)
                self.write_logging_config_file(node)
                self.write_genesis_file(node)
        
        self.write_dot_file()

    def write_config_file(self, node):
        is_bios = node.name == 'bios'
        peers = '\n'.join([f'p2p-peer-address = {self.network.nodes[p].p2p_endpoint}' for p in node.peers])
        if len(node.producers) > 0:
            producer_keys = 'private-key = ["EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV","5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"\n'
            producer_names = '\n'.join([f'producer-name = {p}' for p in node.producers])
            producer_plugin = 'plugin = eosio::producer_plugin\n'
        else:
            producer_keys = ''
            producer_names = ''
            producer_plugin = ''
        config = f'''blocks-dir = {block_dir}
http-server-address = {node.host_name}:{node.http_port}
http-validate-host = false
p2p-listen-endpoint = {node.listen_addr}:{node.p2p_port}
p2p-server-address = {node.public_name}:{node.p2p_port}
{"enable-stale-production = true" if is_bios else ""}
{"p2p-peer-address = %s" % self.network.nodes['bios'].p2p_endpoint if not is_bios else ""}
{peers}
{producer_keys}
{producer_names}
{producer_plugin}
plugin = eosio::net_plugin
plugin = eosio::chain_api_plugin
'''
        with open('config.ini', 'w') as cfg:
            cfg.write(config)

    def write_logging_config_file(self, node):
        pass

    def write_genesis_file(self, node):
        pass

    def is_bios_ndx(self, ndx):
        return self.aliases[ndx] == 'bios'

    def start_ndx(self):
        return 1 if self.is_bios_ndx(0) else 0

    def next_ndx(self, ndx):
        ndx += 1
        loop = ndx == self.args.total_nodes
        if loop:
            ndx = self.start_ndx()
        else:
            if self.is_bios_ndx(ndx):
                loop = self.next_ndx(ndx)
        return ndx, loop

    def skip_ndx(self, begin, offset):
        ndx = (begin + offset) % self.args.total_nodes
        if self.args.total_nodes > 2:
            attempts = self.args.total_nodes - 2
            while attempts and (self.is_bios_ndx(ndx) or ndx == begin):
                ndx, _ = self.next_ndx(ndx)
                attempts -= 1
        return ndx

    def make_line(self, make_ring: bool = True):
        print(f"making {'ring' if make_ring else 'line'}")
        self.bind_nodes()
        non_bios = self.args.total_nodes - 1
        if non_bios > 2:
            end_of_loop = False
            i = self.start_ndx()
            while not end_of_loop:
                front = i
                front, end_of_loop = self.next_ndx(front)
                if end_of_loop and not make_ring:
                    break
                self.network.nodes[self.aliases[i]].peers.append(self.aliases[front])
                i, _ = self.next_ndx(i)
        elif non_bios == 2:
            n0 = self.start_ndx()
            n1 = n0
            n1 = self.next_ndx(n1)[0]
            self.network.nodes[self.aliases[n1]].peers.append(self.aliases[n0])

    def make_star(self):
        print('making star')
        non_bios = self.args.total_nodes - 1
        if non_bios < 4:
            self.make_line()
            return
        self.bind_nodes()

        links = 3
        if non_bios > 12:
            links = int(math.sqrt(non_bios)) + 2
        gap = 3 if non_bios > 6 else (non_bios - links)/2 + 1
        while non_bios % gap == 0:
            gap += 1

        loop = False
        i = self.start_ndx()
        while not loop:
            current = self.network.nodes[self.aliases[i]]
            ndx = i
            for l in range(1, links):
                ndx = self.skip_ndx(ndx, l * gap)
                peer = self.aliases[ndx]
                found = True
                while found:
                    found = False
                    for p in current.peers:
                        if p == peer:
                            ndx = self.next_ndx(ndx)
                            if ndx == i:
                                ndx = self.next_ndx(ndx)
                            peer = self.aliases[ndx]
                            found = True
                            break
                current.peers.append(peer)

    def make_mesh(self):
        print('making mesh')
        non_bios = self.args.total_nodes - 1
        self.bind_nodes()
        loop = False
        i = self.start_ndx()
        while not loop:
            current = self.network.nodes[self.aliases[i]]
            for j in range(1, non_bios):
                ndx = self.skip_ndx(i,j)
                peer = self.aliases[ndx]
                current.peers.append(peer)

    def make_custom(self):
        print('making custom')
    
    def write_dot_file(self):
        with open('testnet.dot', 'w') as f:
            f.write('digraph G\n{\nlayout="circo";\n')
            for node in self.network.nodes:
                for p in node.peers:
                    pname = ''
                    f.write('"{node.instance.dot_label()}"->"{pname}" [dir="forward"];\n')
            f.write('}')

if __name__ == '__main__':
    l = launcher(sys.argv[1:])