# Kode Dot 资料

- 官方快速开始:
  https://docs.kode.diy/en/kode-dot/quickstart
- Arduino 示例:
  https://github.com/bitdeckai/kodedot_arduino_basice_examples

## 在 esp-claw 中编译和烧录 Kode Dot

以下步骤适用于 Windows + ESP-IDF v5.5.x。

### 1) 准备环境

1. 安装 ESP-IDF 并使用 ESP-IDF PowerShell 终端。
2. 进入工程目录:

	cd /d e:\0_project\Kode\kodeclaw\esp-claw\application\edge_agent

3. 首次建议安装 board manager 辅助工具:

	pip install esp-bmgr-assist

### 2) 生成 Kode Dot 板级配置

	idf.py gen-bmgr-config -c ./boards -b kode_dot

说明:
- Kode Dot 板级文件位于 application/edge_agent/boards/kodediy/kode_dot
- 如果切换过 ESP-IDF 大版本，建议先执行一次 fullclean:

	idf.py fullclean

### 3) 编译

	idf.py build

### 4) 烧录并打开串口日志

1. 查看设备串口号，例如 COM6。
2. 执行:

    //auto detect COM port
    idf.py flash monitor
    
    //
	idf.py -p COM6 flash monitor

退出串口监控可使用 Ctrl+]。

### 5) 首次运行后 Wi-Fi 配置

设备会启动配网热点，日志中可看到类似:
- Provisioning AP SSID: esp-claw-xxxxxx
- URL: http://192.168.4.1/

配网方式二选一:

1. 网页配网
- 连接设备热点
- 打开 http://192.168.4.1/
- 填写路由器 2.4G SSID 和密码

2. 串口命令配网

	wifi --scan
	wifi --set --ssid 你的WiFi名 --password 你的密码 --apply
	wifi --status

    //example
    wifi --set --ssid HUAWEI-yang --password justinlee --apply

### 6) 运行成功判据

- 出现 Wi-Fi STA ready 和 sta ip
- cap_time 出现 SNTP time synchronization event received
- Time sync succeeded
- app> 命令行可交互

