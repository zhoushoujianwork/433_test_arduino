# Web 固件烧录工具

这是一个基于 Web Serial API 的在线固件烧录工具，让用户无需安装任何开发环境即可烧录固件。

## 使用方法

### 方式一：单文件版本（推荐，最简单）

**适合没有开发环境的用户**

1. 构建固件并生成合并的 bin 文件：
```bash
pio run
python scripts/merge_bin.py
```

2. 将 `flasher-standalone.html` 和 `firmware.bin` 打包发布

3. 用户下载后：
   - 直接双击打开 `flasher-standalone.html`（无需启动服务器）
   - 点击"选择 firmware.bin 文件"按钮
   - 选择下载的 `firmware.bin` 文件
   - 连接 ESP32-S3 设备
   - 点击"连接并烧录固件"按钮

### 方式二：在线版本（需要 HTTP 服务器）

1. 构建固件并生成合并的 bin 文件：
```bash
pio run
python scripts/merge_bin.py
```

2. 启动本地 HTTP 服务器：
```bash
cd web-flasher
python -m http.server 8000
```

3. 在浏览器中打开 `http://localhost:8000`

### 方式三：GitHub Pages 部署

1. 将 `web-flasher` 目录推送到 GitHub 仓库

2. 在仓库设置中启用 GitHub Pages，选择 `main` 分支的 `/web-flasher` 目录

3. 访问 `https://yourusername.github.io/433_test_arduino/`

### 方式四：Release 附件

在 GitHub Release 中上传 `firmware-package.zip`（包含 `flasher-standalone.html` 和 `firmware.bin`），用户下载解压后直接打开 HTML 文件即可。

## 浏览器要求

- Chrome 89+
- Edge 89+
- Opera 75+

不支持 Firefox 和 Safari（它们不支持 Web Serial API）

## 文件说明

- `index.html` - 烧录界面
- `manifest.json` - 固件清单文件
- `firmware.bin` - 合并后的固件文件（需要生成）

## 固件生成

使用 PlatformIO 构建后，需要将 bootloader、partition table 和 app 合并成一个文件。

运行脚本：
```bash
python scripts/merge_bin.py
```

这会在 `web-flasher` 目录生成 `firmware.bin` 文件。

## 自动化构建

项目包含 GitHub Actions 工作流，会自动：
1. 构建固件
2. 合并 bin 文件
3. 上传到 Release

每次创建 Release 时会自动触发。
