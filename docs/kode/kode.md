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

### 8) 微信接入（ClawBot）

以下步骤适用于已经完成联网并能进入 `app>` 的设备。

1. 在 Web 配置页完成微信登录
- 打开 `http://esp-claw.local/` 或 `http://设备IP/`。
- 在微信配置页面扫码登录 ClawBot，并确认状态为 connected/confirmed。

2. 串口设置并启动微信网关

	wechat --set-config --token <token> --base-url https://ilinkai.weixin.qq.com
	wechat --start

说明:
- `token` 来自微信接入页面或二维码登录流程，不是微信开放平台的 AppSecret。
- 如果看到 `WeChat gateway started`，表示网关已启动。

3. 从串口日志获取真实 chat_id（推荐）

给机器人发送一条消息后，在串口中查找如下日志:

	cap_im_wechat: wechat parsed message from_user=... group_id=... chat_id=...

或:

	claw_event_router: processing event=... source=wechat_gateway channel=wechat chat=...

其中 `chat_id`（或 `chat=` 后的值）就是发送命令要使用的目标 ID。

示例:
- `chat_id=o9cq801Mt6qr4aRdcozKl1BjPPhU@im.wechat`

4. 主动发送消息

	wechat --send-text <chat_id> --text nihao

例如:

	wechat --send-text o9cq801Mt6qr4aRdcozKl1BjPPhU@im.wechat --text nihao

提示:
- 文本不含空格时，建议不加引号，避免串口后台日志插入造成命令解析异常。
- 文本含空格时可使用引号，并尽量整行一次性粘贴。

5. 常见问题

- `wechat: option "--text" requires an argument`

  通常是命令被打断或引号未闭合，重新完整输入即可。

- `wechat_send_text failed: ESP_ERR_INVALID_ARG`

  常见原因是 `chat_id` 填错、`--text` 为空，或尚未成功执行 `wechat --set-config`。

- `transport_base ... Connection reset by peer`

  多见于复用连接被服务端关闭；若随后出现 `WeChat text sent`，说明已自动重试并发送成功。

- `Rule im_any_message_agent agent action failed: ESP_ERR_INVALID_STATE`

  表示 Agent/LLM 规则未满足运行条件（例如 LLM 配置未完成），不影响微信基础收发链路。

