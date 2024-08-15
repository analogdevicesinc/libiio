# -*- coding: utf-8 -*-

import os
import pathlib
import signal
import socket
import subprocess
import time
from shutil import which


class iio_emu_manager:
    def __init__(
        self, xml_path: str, auto: bool = True, rx_dev: str = None, tx_dev: str = None,
    ):
        self.xml_path = xml_path
        self.rx_dev = rx_dev
        self.tx_dev = tx_dev
        self.current_device = None
        self.auto = auto
        self.data_devices = None

        iio_emu = which("iio-emu") is None
        if iio_emu:
            raise Exception("iio-emu not found on path")

        hostname = socket.gethostname()
        self.local_ip = socket.gethostbyname(hostname)
        self.uri = f"ip:{self.local_ip}"
        self.p = None
        if os.getenv("IIO_EMU_URI"):
            self.uri = os.getenv("IIO_EMU_URI")
        print('IIO_EMU_PREFIX', os.getenv("IIO_EMU_PREFIX"))
        if os.getenv("IIO_EMU_PREFIX"):
            self.prefix = os.getenv("IIO_EMU_PREFIX")
            self.prefix = self.prefix.replace("'","")
            print(f"Using IIO_EMU_PREFIX: {self.prefix}")
        else:
            self.prefix = None


    def __del__(self):
        if self.p:
            self.stop()

    def start(self):
        with open("data.bin", "w"):
            pass
        cmd = ["iio-emu", "generic", self.xml_path]
        if self.prefix:
            cmd = [self.prefix] + cmd
        if self.data_devices:
            for dev in self.data_devices:
                cmd.append(f"{dev}@data.bin")
        print(cmd)
        self.p = subprocess.Popen(' '.join(cmd), shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=os.setsid)
        time.sleep(3)  # wait for server to boot
        # With shell stopping or checking if the process is stopped is hard
        # It should have a process group if its running.
        try:
            pidg = os.getpgid(self.p.pid)
            failed_to_start = False
        except ProcessLookupError:
            failed_to_start = True # stop multiple exceptions appearing

        if failed_to_start:
            self.p = None
            raise Exception("iio-emu failed to start... exiting")

    def stop(self):
        if self.p:
            os.killpg(os.getpgid(self.p.pid), signal.SIGTERM)
        self.p = None
