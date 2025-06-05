from fabric import Connection, ThreadingGroup as Group
from fabric.exceptions import GroupException
from paramiko import RSAKey
from paramiko.ed25519key import Ed25519Key
from paramiko.ssh_exception import PasswordRequiredException, SSHException
from os.path import basename, splitext
from time import sleep
from math import ceil
from os.path import join
import subprocess
import sys

from benchmark.config import (
    Committee,
    Key,
    NodeParameters,
    BenchParameters,
    ConfigError,
)
from benchmark.utils import BenchError, Print, PathMaker, progress_bar
from benchmark.commands import CommandMaker
from benchmark.logs import LogParser, ParseError
from benchmark.instance import InstanceManager


class FabricError(Exception):
    """Wrapper for Fabric exception with a meaningfull error message."""

    def __init__(self, error):
        assert isinstance(error, GroupException)
        message = list(error.result.values())[-1]
        super().__init__(message)


class ExecutionError(Exception):
    pass


#### TODO configure ###

USERNAME = "root"

NUMBER_VM = 3

#########################

hosts_ssh = [f"127.0.0.1:{port}" for port in range(2222, 2222 + NUMBER_VM)]

hosts_sim = [f"192.168.15.{i}" for i in range(0, NUMBER_VM)]

class BenchSim:
    def __init__(self, ctx):
        self.manager = InstanceManager.make()
        self.settings = self.manager.settings
        try:
            ctx.connect_kwargs.pkey = Ed25519Key.from_private_key_file(
                self.manager.settings.key_path
            )
            self.connect = ctx.connect_kwargs
        except (IOError, PasswordRequiredException, SSHException) as e:
            raise BenchError("Failed to load SSH key", e)

    def _check_stderr(self, output):
        if isinstance(output, dict):
            for x in output.values():
                if x.stderr:
                    raise ExecutionError(x.stderr)
        else:
            if output.stderr:
                raise ExecutionError(output.stderr)

    def install(self):
        Print.info("Installing rust and cloning the repo...")
        cmd = [
            "source $HOME/.cargo/env",
        ]
        hosts = hosts_ssh
        try:
            g = Group(*hosts, user=USERNAME, connect_kwargs=self.connect)
            g.run(" && ".join(cmd), hide=True)
            Print.heading(f"Initialized testbed of {len(hosts)} nodes")
        except (GroupException, ExecutionError) as e:
            e = FabricError(e) if isinstance(e, GroupException) else e
            raise BenchError("Failed to install repo on testbed", e)

    def kill(self, hosts=[], delete_logs=False):
        assert isinstance(hosts, list)
        assert isinstance(delete_logs, bool)
        hosts = hosts if hosts else hosts_ssh
        delete_logs = CommandMaker.clean_logs() if delete_logs else "true"
        cmd = [delete_logs, f"({CommandMaker.kill()} || true)"]
        try:
            g = Group(*hosts, user=USERNAME, connect_kwargs=self.connect)
            g.run(" && ".join(cmd), hide=True)
        except GroupException as e:
            raise BenchError("Failed to kill nodes", FabricError(e))

    def _select_hosts(self, bench_parameters):
        nodes = max(bench_parameters.nodes)

        # # Ensure there are enough hosts.
        print(len(hosts_ssh), len(hosts_sim), nodes)
        if len(hosts_ssh) < nodes or len(hosts_sim) < nodes:
            return ([], [])

        # # Select the hosts in different data centers.
        return (hosts_ssh[:nodes],  hosts_sim[:nodes])


    def _background_run(self, host, command, log_file):
        name = splitext(basename(log_file))[0]
        print(sys.path)
        cmd = f'tmux new -d -s "{name}" "{command} |& tee {log_file}"'
        c = Connection(host, user=USERNAME, connect_kwargs=self.connect)
        output = c.run(cmd, hide=True)
        self._check_stderr(output)

    def _update(self, hosts):
        Print.info(f'Updating {len(hosts)} nodes (branch "{self.settings.branch}")...')
        cmd = [
            "source $HOME/.cargo/env",
            CommandMaker.alias_binaries(f"./{self.settings.repo_name}/target/release/"),
        ]
        g = Group(*hosts, user=USERNAME, connect_kwargs=self.connect)
        g.run(" && ".join(cmd), hide=True)

    def _config(self, hosts_ssh, hosts_sim, node_parameters):
        Print.info("Generating configuration files...")

        # Cleanup all local configuration files.
        cmd = CommandMaker.cleanup()
        subprocess.run([cmd], shell=True, stderr=subprocess.DEVNULL)

        # Recompile the latest code.
        cmd = CommandMaker.compile().split()
        subprocess.run(cmd, check=True, cwd=PathMaker.node_crate_path())

        # Create alias for the client and nodes binary.
        cmd = CommandMaker.alias_binaries(PathMaker.binary_path())
        subprocess.run([cmd], shell=True)

        # Generate configuration files.
        keys = []
        key_files = [PathMaker.key_file(i) for i in range(len(hosts_sim))]
        for filename in key_files:
            cmd = CommandMaker.generate_key(filename).split()
            subprocess.run(cmd, check=True)
            keys += [Key.from_file(filename)]
        
    
        names = [x.name for x in keys]
        consensus_addr = [f"{x}:{self.settings.consensus_port}" for x in hosts_sim]
        front_addr = [f"{x}:{self.settings.front_port}" for x in hosts_sim]
        mempool_addr = [f"{x}:{self.settings.mempool_port}" for x in hosts_sim]
        committee = Committee(names, consensus_addr, front_addr, mempool_addr)
        committee.print(PathMaker.committee_file())

        node_parameters.print(PathMaker.parameters_file())

        # Cleanup all nodes.
        cmd = f"{CommandMaker.cleanup()} || true"
        g = Group(*hosts_ssh, user=USERNAME, connect_kwargs=self.connect)
        g.run(cmd, hide=True)

        # Upload configuration files.
        progress = progress_bar(hosts_ssh, prefix="Uploading config files:")
        for i, host in enumerate(progress):
            c = Connection(host, user=USERNAME, connect_kwargs=self.connect)
            c.put(PathMaker.committee_file(), ".")
            c.put(PathMaker.key_file(i), ".")
            c.put(PathMaker.parameters_file(), ".")
            c.put("sleep_tmux_kill.sh", ".")

        return committee

    def _run_single(self, hosts_ssh, hosts_sim, rate, bench_parameters, node_parameters, debug=False):
        Print.info("Booting testbed...")

        # Kill any potentially unfinished run and delete logs.
        self.kill(hosts=hosts_ssh, delete_logs=True)

        # Run the clients (they will wait for the nodes to be ready).
        # Filter all faulty nodes from the client addresses (or they will wait
        # for the faulty nodes to be online).
        committee = Committee.load(PathMaker.committee_file())
        addresses = [f"{x}:{self.settings.front_port}" for x in hosts_sim]
        rate_share = ceil(rate / committee.size())  # Take faults into account.
        timeout = node_parameters.timeout_delay
        client_logs = [PathMaker.client_log_file(i) for i in range(len(hosts_sim))]
        for host, addr, log_file in zip(hosts_ssh, addresses, client_logs):
            cmd = CommandMaker.run_client(
                addr, bench_parameters.tx_size, rate_share, timeout, nodes=addresses
            )
            self._background_run(host, cmd, log_file)

        # Run the nodes.
        key_files = [PathMaker.key_file(i) for i in range(len(hosts_sim))]
        dbs = [PathMaker.db_path(i) for i in range(len(hosts_sim))]
        node_logs = [PathMaker.node_log_file(i) for i in range(len(hosts_sim))]
        for host, key_file, db, log_file in zip(hosts_ssh, key_files, dbs, node_logs):
            cmd = CommandMaker.run_node(
                key_file,
                PathMaker.committee_file(),
                db,
                PathMaker.parameters_file(),
                debug=debug,
            )
            self._background_run(host, cmd, log_file)

        sync_time = 2 * node_parameters.timeout_delay / 1000
        duration = bench_parameters.duration

        g = Group(*hosts_ssh, user=USERNAME, connect_kwargs=self.connect)
        g.run("chmod +x sleep_tmux_kill.sh", hide=True)
        Print.info("Waiting for nodes to sync and running benchmark...")
        g.run(f"./sleep_tmux_kill.sh {sync_time + duration}", hide=True)
        Print.info("Benchmark has finished")


    def _logs(self, hosts, faults):
        # Delete local logs (if any).
        cmd = CommandMaker.clean_logs()
        subprocess.run([cmd], shell=True, stderr=subprocess.DEVNULL)

        # Download log files.
        progress = progress_bar(hosts, prefix="Downloading logs:")
        for i, host in enumerate(progress):
            c = Connection(host, user=USERNAME, connect_kwargs=self.connect)
            c.get(PathMaker.node_log_file(i), local=PathMaker.node_log_file(i))
            c.get(PathMaker.client_log_file(i), local=PathMaker.client_log_file(i))

        # Parse logs and return the parser.
        Print.info("Parsing logs and computing performance...")
        return LogParser.process(PathMaker.logs_path(), faults=faults)

    def run(self, bench_parameters_dict, node_parameters_dict, debug=False):
        assert isinstance(debug, bool)
        Print.heading("Starting remote benchmark")
        try:
            bench_parameters = BenchParameters(bench_parameters_dict)
            node_parameters = NodeParameters(node_parameters_dict)
        except ConfigError as e:
            raise BenchError("Invalid nodes or bench parameters", e)

        # Select which hosts to use.
        selected_hosts_ssh, selected_hosts_sim = self._select_hosts(bench_parameters)
        if not selected_hosts_ssh:
            Print.warn("There are not enough instances available")
            return

        # Update nodes.
        try:
            self._update(selected_hosts_ssh)
        except (GroupException, ExecutionError) as e:
            e = FabricError(e) if isinstance(e, GroupException) else e
            raise BenchError("Failed to update nodes", e)

        # Run benchmarks.
        for n in bench_parameters.nodes:
            for r in bench_parameters.rate:
                Print.heading(f"\nRunning {n} nodes (input rate: {r:,} tx/s)")
                hosts_ssh = selected_hosts_ssh[:n]
                hosts_sim = selected_hosts_sim[:n]

                # Upload all configuration files.
                try:
                    self._config(hosts_ssh, hosts_sim, node_parameters)
                except (subprocess.SubprocessError, GroupException) as e:
                    e = FabricError(e) if isinstance(e, GroupException) else e
                    Print.error(BenchError("Failed to configure nodes", e))
                    continue

                # Do not boot faulty nodes.
                faults = bench_parameters.faults

                hosts_ssh = hosts_ssh[:n - faults]
                hosts_sim = hosts_sim[:n - faults]

                # Run the benchmark.
                for i in range(bench_parameters.runs):
                    Print.heading(f"Run {i+1}/{bench_parameters.runs}")
                    try:
                        self._run_single(
                            hosts_ssh, hosts_sim, r, bench_parameters, node_parameters, debug
                        )
                        self._logs(hosts_ssh, faults).print(
                            PathMaker.result_file(
                                faults, n, r, bench_parameters.tx_size
                            )
                        )
                    except (
                        subprocess.SubprocessError,
                        GroupException,
                        ParseError,
                    ) as e:
                        self.kill(hosts=hosts_ssh)
                        if isinstance(e, GroupException):
                            e = FabricError(e)
                        Print.error(BenchError("Benchmark failed", e))
                        continue
