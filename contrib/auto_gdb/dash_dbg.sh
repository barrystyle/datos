#!/usr/bin/env bash
# use testnet settings,  if you need mainnet,  use ~/.dashcore/pacprotocol.pid file instead
export LC_ALL=C

dash_pid=$(<~/.dashcore/testnet3/pacprotocol.pid)
sudo gdb -batch -ex "source debug.gdb" pacprotocold ${dash_pid}
