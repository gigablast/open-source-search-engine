import argparse


def gen_hosts_conf(ips, num_hosts_per_ip=1, start_dns_port=5000, start_https_port=7000,
                   start_http_port=8000, start_udp_port=9000,
                   num_mirrors=1, base_dir='/var/gigablast/data'):
    host_count = 0
    for ip in ips:
        dns_port = start_dns_port
        https_port = start_https_port
        http_port = start_http_port
        udp_port = start_udp_port
        directory = 0
        for offset in range(num_hosts_per_ip):
            print("{0} {1} {2} {3} {4} {5} {6} {8}{7:02d}/ {8}{7:02d}/".format(
                host_count,
                dns_port + offset,
                https_port + offset,
                http_port + offset,
                udp_port + offset,
                ip, ip,
                directory + offset,
                base_dir)
            )
            host_count = host_count + 1

    print(f'num-mirrors: {num_mirrors}')


def main():
    parser = argparse.ArgumentParser(description='Generate hosts.conf file for cluster configuration')
    parser.add_argument('ips', metavar='IP', type=str, nargs='+', help='IP addresses of the cluster nodes')
    parser.add_argument('--num-hosts-per-ip', type=int, default=1, help='Number of hosts per IP, default 1')
    parser.add_argument('--start-dns-port', type=int, default=5000, help='Start DNS port, default 5000')
    parser.add_argument('--start-https-port', type=int, default=7000, help='Start HTTPS port, default 7000')
    parser.add_argument('--start-http-port', type=int, default=8000, help='Start HTTP port, default 8000')
    parser.add_argument('--start-udp-port', type=int, default=9000, help='Start UDP port, default 9000')
    parser.add_argument('--num-mirrors', type=int, default=1, help='Number of mirrors, default 1')
    args = parser.parse_args()
    gen_hosts_conf(args.ips, args.num_hosts_per_ip, args.start_dns_port, args.start_https_port, args.start_http_port,
                   args.start_udp_port, args.num_mirrors)


if __name__ == '__main__':
    main()
