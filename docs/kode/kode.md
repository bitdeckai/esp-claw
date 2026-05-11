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

### 7) Lua 音频自检脚本（喇叭/麦克风）

以下命令在串口 `app>` 提示符下执行。

1. 查看内置音频测试脚本:

	lua --list --keyword audio

2. 写入并运行喇叭蜂鸣脚本:

	lua --write --path audio_beep.lua --content "local a=require('audio');local bm=require('board_manager');local c,r,ch,b=bm.get_audio_codec_output_params('audio_dac');print('out',c,r,ch,b);local o,e=a.new_output(c,r,ch,b,100);if not o then print('new_output fail',e) return end;a.play_tone(o,1000,800,100);a.play_tone(o,1500,800,100);a.play_tone(o,2000,800,100);a.close(o);print('beep done')"
	lua --run --path audio_beep.lua --timeout-ms 10000

3. 写入并运行录音回放脚本:

	lua --write --path audio_loopback.lua --content "local a=require('audio');local bm=require('board_manager');local ic,ir,ich,ib=bm.get_audio_codec_input_params('audio_adc');local oc,orr,och,ob=bm.get_audio_codec_output_params('audio_dac');print('in',ic,ir,ich,ib,'out',oc,orr,och,ob);local i,ie=a.new_input(ic,ir,ich,ib);local o,oe=a.new_output(oc,orr,och,ob,100);if (not i) or (not o) then print('open fail',ie,oe) return end;local p='/fatfs/rec.wav';local info=a.record_wav(i,p,3000);print('rec',info and info.bytes);a.play_wav(o,p);a.close(o);a.close(i);print('loopback done')"
	lua --run --path audio_loopback.lua --timeout-ms 20000

4. 直接运行内置录音回放测试（推荐）:

	lua --run --path builtin/test/audio_record_play.lua --timeout-ms 30000

提示:
- 如果出现 `/fatfs/scripts/xxx.lua:1: ')' expected near <eof>`，通常是串口输入过长被截断，重新执行 `lua --write` 即可。
- 日志出现 `tone sequence done`、`recorded ... bytes=...`、`playback done` 可判定音频链路正常。

