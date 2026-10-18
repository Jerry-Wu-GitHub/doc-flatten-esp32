#!/usr/bin/env python3
"""DocFlatten 串口协议解析与文件接收。"""

import base64
import re
import sys
import time
from pathlib import Path
from typing import TextIO

import serial

ANSI_ESCAPE_PATTERN = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
BASE64_LINE_PATTERN = re.compile(r"^[A-Za-z0-9+/=]+$")
FILE_BEGIN_PATTERN = re.compile(r"FILE_BEGIN\s+(\S+)\s+(\d+)\s+([0-9a-fA-F]{8})")
FILE_END_PATTERN = re.compile(r"FILE_END\s+(\S+)")


def remove_terminal_sequences(line: str) -> str:
    """去掉 ANSI 转义序列、回车和首尾空白。

    Args:
        line: 串口读到的一行文本。

    Returns:
        清理后的文本。
    """
    return ANSI_ESCAPE_PATTERN.sub("", line).replace("\r", "").strip()


def calculate_fnv1a_checksum(data: bytes) -> int:
    """计算 FNV-1a 32 位校验值。

    Args:
        data: 输入字节。

    Returns:
        32 位无符号校验值。
    """
    checksum = 2166136261
    for byte in data:
        checksum ^= byte
        checksum = (checksum * 16777619) & 0xFFFFFFFF
    return checksum


def wait_for_marker(
    port: serial.Serial,
    marker: str,
    deadline: float,
    log_file: TextIO,
) -> bool:
    """等待串口输出中出现指定标记。

    Args:
        port: 已打开的串口。
        marker: 需要等待的标记文本。
        deadline: 超时的绝对时间（time.time()）。
        log_file: 日志文件。

    Returns:
        在超时前找到标记返回 True。
    """
    while time.time() < deadline:
        raw_line = port.readline()
        if not raw_line:
            continue
        line = remove_terminal_sequences(raw_line.decode("utf-8", errors="replace"))
        if not line:
            continue
        log_file.write(line + "\n")
        print(line, flush=True)
        if marker in line:
            return True
    return False


def receive_files(
    port: serial.Serial,
    output_directory: Path,
    deadline: float,
    log_file: TextIO,
) -> int:
    """接收 base64 文件流并写入输出目录。

    Args:
        port: 已打开的串口。
        output_directory: 保存图片的本地目录。
        deadline: 超时的绝对时间（time.time()）。
        log_file: 日志文件。

    Returns:
        成功保存的文件数量。
    """
    current_file_name: str | None = None
    expected_size = 0
    expected_checksum = 0
    base64_chunks: list[str] = []
    saved_count = 0

    while time.time() < deadline:
        raw_line = port.readline()
        if not raw_line:
            continue
        line = remove_terminal_sequences(raw_line.decode("utf-8", errors="replace"))
        if not line:
            continue

        begin_match = FILE_BEGIN_PATTERN.search(line)
        if begin_match:
            current_file_name = begin_match.group(1)
            expected_size = int(begin_match.group(2))
            expected_checksum = int(begin_match.group(3), 16)
            base64_chunks = []
            log_file.write(line + "\n")
            print(line, flush=True)
            continue

        end_match = FILE_END_PATTERN.search(line)
        if end_match and current_file_name is not None:
            base64_text = "".join(base64_chunks)
            decoded_data = base64.b64decode(base64_text)
            if len(decoded_data) != expected_size:
                print(
                    f"[warning] {current_file_name}: expected {expected_size}, "
                    f"got {len(decoded_data)}",
                    file=sys.stderr,
                )
            if calculate_fnv1a_checksum(decoded_data) != expected_checksum:
                print(
                    f"[warning] {current_file_name}: checksum mismatch",
                    file=sys.stderr,
                )
            target_path = output_directory / current_file_name
            target_path.write_bytes(decoded_data)
            print(f"[saved] {target_path} ({len(decoded_data)} bytes)", flush=True)
            log_file.write(f"[saved] {target_path} ({len(decoded_data)} bytes)\n")
            saved_count += 1
            current_file_name = None
            base64_chunks = []
            continue

        if current_file_name is not None and BASE64_LINE_PATTERN.match(line):
            base64_chunks.append(line)
            continue

        log_file.write(line + "\n")
        print(line, flush=True)
        if "PULL_DONE" in line:
            return saved_count
    return saved_count
