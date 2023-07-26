# 植物生长阶段检测

目前该示例仅支持ESP32-S3-EYE开发板，若要使用其他开发板，需要自己修改按键输入逻辑配置。同时配置了uart串口，使用GPIO38、39收发数据，这一对GPIO是默认内存卡使用的，如果你有使用内存卡的需求，请将uart串口部分代码注释或者另外寻找一对串口或者使用其他通讯方式。

## 按键控制说明

- 交互按键为Boot键，MENU键，PLAY键，UP键和DN键。
- 按下BOOT键：切换输出显示模式。
- 按下MENU：切换录入模式和检测模式。
- 按下PLAY键（录入模式）： 录入指示框内的颜色。
- 按下PLAY键（检测模式）： 删除最后一个被录入的颜色。
- 按下UP键（录入模式）： 增大指示框的尺寸。
- 按下UP键（检测模式）： 增大颜色检测过滤的最小色块的面积。
- 按下DN键（录入模式）： 减小指示框的尺寸。
- 按下DN键（检测模式）： 减小颜色检测过滤的最小色块的面积。

## 如何使用

在项目配置和构建之前，使用idf.py set-target <chip_name>设置正确的芯片目标，这里我的<chip_name>是esp32s3

### 所需硬件

* 带有ESP32/ESP32-S2/ESP32-S3 SoC的开发板（例如，ESP-EYE，ESP-WROVER-KIT，ESPS3-EYE等）
* 用于供电和编程的USB线
* 相机模块：OV2640/OV3660/GC0308/GC032A图像传感器（推荐焦距：5cm-20cm）
* LCD（可选）：ST7789等等
* 绿色的蔬菜，如没有，其他绿色的物品也可以。尽量在亮度充足的地方进行色块检测，本人测试鲜艳的绿色效果最好，暗绿、墨绿效果一般。

### 配置项目

根据开发板使用sdkconfig.defaults.<chip_name>已经设置了一些默认设置。

### 构建和烧录（Build and Flash）

运行idf.py -p PORT flash monitor来构建，刷新和监视项目。（要退出串行监视器，请输入Ctrl-]。）

请参阅[Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html)以获取配置和使用ESP-IDF构建项目的完整步骤。

## 输出示例

请确保代码清晰完整的被摄像头拍到。
如果成功解码一个二维码，你将能够看到如下显示的信息：

```
Growing stage
I (293823) UART: Sent: Growing stage
Mature stage
I (293823) UART: Sent: Mature  stage
Exceeded expected size
I (293823) UART: Sent: Exceeded expected size
```


## 故障排查

如有任何技术疑问，请在GitHub上开启一个[issue]我将尽快回复您。
