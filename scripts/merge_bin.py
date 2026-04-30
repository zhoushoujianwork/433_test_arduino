#!/usr/bin/env python3
"""
合并 ESP32-S3 固件文件
将 bootloader、partition table 和 app 合并成一个可直接烧录的 bin 文件
"""

import os
import sys
from pathlib import Path

def merge_bin():
    # 项目根目录
    project_dir = Path(__file__).parent.parent

    # PlatformIO 构建输出目录
    build_dir = project_dir / ".pio" / "build" / "esp32-s3-devkitc-1"

    # 输出目录
    output_dir = project_dir / "web-flasher"
    output_dir.mkdir(exist_ok=True)

    # 固件文件路径
    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    firmware = build_dir / "firmware.bin"

    # 检查文件是否存在
    if not bootloader.exists():
        print(f"错误: 找不到 bootloader.bin")
        print(f"请先运行: pio run")
        return False

    if not partitions.exists():
        print(f"错误: 找不到 partitions.bin")
        return False

    if not firmware.exists():
        print(f"错误: 找不到 firmware.bin")
        return False

    # 输出文件
    output_file = output_dir / "firmware.bin"

    print("开始合并固件文件...")
    print(f"  Bootloader: {bootloader}")
    print(f"  Partitions: {partitions}")
    print(f"  Firmware:   {firmware}")

    # ESP32-S3 的偏移地址
    BOOTLOADER_OFFSET = 0x0
    PARTITION_OFFSET = 0x8000
    APP_OFFSET = 0x10000

    # 创建 16MB 的空白固件（填充 0xFF）
    merged = bytearray([0xFF] * (16 * 1024 * 1024))

    # 写入 bootloader
    with open(bootloader, 'rb') as f:
        bootloader_data = f.read()
        merged[BOOTLOADER_OFFSET:BOOTLOADER_OFFSET + len(bootloader_data)] = bootloader_data
        print(f"  ✓ Bootloader 写入到 0x{BOOTLOADER_OFFSET:X} ({len(bootloader_data)} 字节)")

    # 写入 partition table
    with open(partitions, 'rb') as f:
        partition_data = f.read()
        merged[PARTITION_OFFSET:PARTITION_OFFSET + len(partition_data)] = partition_data
        print(f"  ✓ Partition table 写入到 0x{PARTITION_OFFSET:X} ({len(partition_data)} 字节)")

    # 写入 app
    with open(firmware, 'rb') as f:
        app_data = f.read()
        merged[APP_OFFSET:APP_OFFSET + len(app_data)] = app_data
        print(f"  ✓ App 写入到 0x{APP_OFFSET:X} ({len(app_data)} 字节)")

    # 只保留实际使用的部分（截断到最后一个非 0xFF 字节）
    last_byte = APP_OFFSET + len(app_data)
    merged = merged[:last_byte]

    # 写入输出文件
    with open(output_file, 'wb') as f:
        f.write(merged)

    print(f"\n✓ 固件合并完成!")
    print(f"  输出文件: {output_file}")
    print(f"  文件大小: {len(merged) / 1024:.1f} KB")
    print(f"\n可以使用以下方式烧录:")
    print(f"  1. Web 烧录: 将 {output_file} 放到 web-flasher 目录")
    print(f"  2. esptool: esptool.py write_flash 0x0 {output_file}")

    return True

if __name__ == "__main__":
    success = merge_bin()
    sys.exit(0 if success else 1)
