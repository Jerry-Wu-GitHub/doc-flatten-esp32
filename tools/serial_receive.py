#!/usr/bin/env python3
"""通过串口驱动开发板上的 DocFlatten 程序：RUN -> PULL，并保存图片到本地。"""

import argparse
import sys
import time
from pathlib import Path

import serial

PROJECT_DIRECTORY = Path(__file__).resolve().parent
if str(PROJECT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(PROJECT_DIRECTORY))

from serial_protocol import receive_files, wait_for_marker


def parse_arguments() -> argparse.Namespace:
    """解析命令行参数。

    Returns:
        解析后的参数对象。
    """
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("--port", default="COM3")
    argument_parser.add_argument("--baud", type=int, default=921600)
    argument_parser.add_argument(
        "--output",
        default=r"D:\网络服务\AI驱动的绘本阅读机器人\doc-flatten-esp32\output",
    )
    argument_parser.add_argument("--run-timeout", type=float, default=900.0)
    argument_parser.add_argument("--pull-timeout", type=float, default=900.0)
    argument_parser.add_argument("--skip-run", action="store_true")
    argument_parser.add_argument("--skip-pull", action="store_true")
    argument_parser.add_argument(
        "--set224",
        action="store_true",
        help="使用 /spiffs/sample224 输入，命令改为 RUN224/PULL224",
    )
    return argument_parser.parse_args()


def main() -> int:
    """运行串口接收流程。

    Returns:
        进程退出码；0 表示成功。
    """
    arguments = parse_arguments()
    output_directory = Path(arguments.output)
    output_directory.mkdir(parents=True, exist_ok=True)
    log_file_path = output_directory / "serial.log"

    with log_file_path.open("w", encoding="utf-8") as log_file:
        port = serial.Serial(arguments.port, arguments.baud, timeout=0.5)
        time.sleep(0.5)
        port.reset_input_buffer()

        is_ready = False
        for _ in range(10):
            port.write(b"PING\n")
            port.flush()
            if wait_for_marker(port, "READY", time.time() + 2.0, log_file):
                is_ready = True
                break
        if not is_ready:
            print("[host] board did not answer READY", file=sys.stderr)
            port.close()
            return 2

        if not arguments.skip_run:
            run_command = "RUN224" if arguments.set224 else "RUN"
            print(f"[host] sending {run_command}", flush=True)
            port.write((run_command + "\n").encode())
            port.flush()
            if not wait_for_marker(
                port, "RUN_DONE", time.time() + arguments.run_timeout, log_file
            ):
                print("[host] timeout waiting for RUN_DONE", file=sys.stderr)
                port.close()
                return 2

        if not arguments.skip_pull:
            pull_command = "PULL224" if arguments.set224 else "PULL"
            print(f"[host] sending {pull_command}", flush=True)
            port.write((pull_command + "\n").encode())
            port.flush()
            saved_count = receive_files(
                port, output_directory, time.time() + arguments.pull_timeout, log_file
            )
            print(
                f"[host] saved {saved_count} file(s) to {output_directory}",
                flush=True,
            )

        port.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
