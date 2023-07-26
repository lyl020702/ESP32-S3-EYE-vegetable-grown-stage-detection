#include "color_detect.hpp"   // 导入颜色检测头文件
#include "esp_log.h"          // 导入ESP32日志头文件
#include "esp_camera.h"       // 导入ESP32摄像头头文件
#include "dl_image.hpp"       // 导入深度学习图像处理头文件
#include "fb_gfx.h"           // 导入图形缓冲区操作头文件
#include "color_detector.hpp" // 导入颜色检测器头文件
#include "who_ai_utils.hpp"   // 导入AI工具头文件
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

using namespace std; // 使用std命名空间
using namespace dl;  // 使用dl命名空间

static const char *TAG = "color_detection"; // 设置日志标签为"color_detection"

// 初始化队列句柄
static QueueHandle_t xQueueFrameI = NULL; // 输入帧队列
static QueueHandle_t xQueueEvent = NULL;  // 事件队列
static QueueHandle_t xQueueFrameO = NULL; // 输出帧队列
static QueueHandle_t xQueueResult = NULL; // 结果队列

static color_detection_state_t gEvent = COLOR_DETECTION_IDLE; // 设置全局事件初始状态为闲置
static bool register_mode = false;                            // 注册模式标志，初始设为否
static bool gReturnFB = true;                                 // 回传帧缓冲区标志，初始设为是
static bool draw_box = true;                                  // 绘制边框标志，初始设为是

static SemaphoreHandle_t xMutex; // 定义一个静态互斥信号量

// 颜色信息数组
vector<color_info_t> std_color_info = {
    {{156, 10, 70, 255, 90, 255}, 64, "red"},
    {{11, 22, 70, 255, 90, 255}, 64, "orange"},
    {{23, 33, 70, 255, 90, 255}, 64, "yellow"},
    {{34, 75, 70, 255, 90, 255}, 64, "green"},
    {{76, 96, 70, 255, 90, 255}, 64, "cyan"},
    {{97, 124, 70, 255, 90, 255}, 64, "blue"},
    {{125, 155, 70, 255, 90, 255}, 64, "purple"},
    {{0, 180, 0, 40, 220, 255}, 64, "white"},
    {{0, 180, 0, 50, 50, 219}, 64, "gray"},
    //    {{0, 180, 0, 255, 0, 45}, 64, "black"}
};

// 定义不同颜色的RGB565格式值
#define RGB565_LCD_RED 0x00F8
#define RGB565_LCD_ORANGE 0x20FD
#define RGB565_LCD_YELLOW 0xE0FF
#define RGB565_LCD_GREEN 0xE007
#define RGB565_LCD_CYAN 0xFF07
#define RGB565_LCD_BLUE 0x1F00
#define RGB565_LCD_PURPLE 0x1EA1
#define RGB565_LCD_WHITE 0xFFFF
#define RGB565_LCD_GRAY 0x1084
#define RGB565_LCD_BLACK 0x0000

#define FRAME_DELAY_NUM 16 // 帧延迟数量

#define UART_NUM UART_NUM_2
#define UART_TX_PIN 39
#define UART_RX_PIN 38

void uart_init()
{

    uart_config_t uart_config = {

        .baud_rate = 115200,

        .data_bits = UART_DATA_8_BITS,

        .parity = UART_PARITY_DISABLE,

        .stop_bits = UART_STOP_BITS_1,

        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,

    };

    uart_param_config(UART_NUM, &uart_config);

    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0);
}

// rgb_print函数：在帧缓冲区中按指定颜色打印字符串
static void rgb_print(camera_fb_t *fb, uint32_t color, const char *str)
{
    fb_gfx_print(fb, (fb->width - (strlen(str) * 14)) / 2, 10, color, str);
}

// rgb_printf函数：在帧缓冲区中按指定颜色打印格式化字符串
static int rgb_printf(camera_fb_t *fb, uint32_t color, const char *format, ...)
{
    char loc_buf[64];
    char *temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
        {
            return 0;
        }
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);
    rgb_print(fb, color, temp);
    if (len > 64)
    {
        free(temp);
    }
    return len;
}

// draw_color_detection_result函数：在图像中绘制颜色检测结果
static void draw_color_detection_result(uint16_t *image_ptr, int image_height, int image_width, vector<color_detect_result_t> &results, uint16_t color)
{
    for (int i = 0; i < results.size(); ++i)
    {
        dl::image::draw_hollow_rectangle(image_ptr, image_height, image_width,
                                         results[i].box[0],
                                         results[i].box[1],
                                         results[i].box[2],
                                         results[i].box[3],
                                         color);
    }
}

// task_process_handler函数：处理任务处理器
static void task_process_handler(void *arg)
{
    uart_init();
    camera_fb_t *frame = NULL;

    ColorDetector detector;
    detector.set_detection_shape({80, 80, 1});
    for (int i = 0; i < std_color_info.size(); ++i)
    {
        detector.register_color(std_color_info[i].color_thresh, std_color_info[i].area_thresh, std_color_info[i].name);
    }

    // 设置颜色阈值检测区域
    vector<vector<int>> color_thresh_boxes = {{110, 110, 130, 130}, {100, 100, 140, 140}, {90, 90, 150, 150}, {80, 80, 160, 160}, {60, 60, 180, 180}, {40, 40, 200, 200}, {20, 20, 220, 220}};
    // 获取color_thresh_boxes的大小，并存储在color_thresh_boxes_num中
    int color_thresh_boxes_num = color_thresh_boxes.size();
    // 计算并存储color_thresh_boxes的中间索引
    int color_thresh_boxes_index = color_thresh_boxes_num / 2;
    // 创建一个存储颜色区域阈值的向量
    vector<int> color_area_threshes = {1, 4, 16, 32, 64, 128, 256, 512, 1024};
    // 获取color_area_threshes的大小，并存储在color_area_thresh_num中
    int color_area_thresh_num = color_area_threshes.size();
    // 计算并存储color_area_threshes的中间索引
    int color_area_thresh_index = color_area_thresh_num / 2;
    // 设置颜色检测器的区域阈值
    detector.set_area_thresh({color_area_threshes[color_area_thresh_index]});
    // 创建一个存储LCD颜色的向量
    vector<uint16_t> draw_lcd_colors = {
        RGB565_LCD_RED,
        RGB565_LCD_ORANGE,
        RGB565_LCD_YELLOW,
        RGB565_LCD_GREEN,
        RGB565_LCD_CYAN,
        RGB565_LCD_BLUE,
        RGB565_LCD_PURPLE,
        RGB565_LCD_WHITE,
        RGB565_LCD_GRAY,
        // RGB565_LCD_BLACK
    };

    // 获取draw_lcd_colors的大小，并存储在draw_colors_num中
    int draw_colors_num = draw_lcd_colors.size();
    // 创建一个color_detection_state_t类型的变量
    color_detection_state_t _gEvent;
    // 创建一个存储颜色阈值的向量
    vector<uint8_t> color_thresh;
    // 开始无限循环
    while (true)
    {
        // 等待互斥信号量
        xSemaphoreTake(xMutex, portMAX_DELAY);
        // 将_gEvent的值设置为gEvent的当前值
        _gEvent = gEvent;
        // 将gEvent的值设置为COLOR_DETECTION_IDLE
        gEvent = COLOR_DETECTION_IDLE;
        // 释放互斥信号量
        xSemaphoreGive(xMutex);
        // 如果成功从输入队列中接收到帧
        if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
        {
            // 如果处于注册模式
            if (register_mode)
            {
                // 根据_gEvent的值执行不同的操作
                switch (_gEvent)
                {
                // 如果需要增加颜色区域
                case INCREASE_COLOR_AREA:
                    // 增加color_thresh_boxes_index的值，但不超过color_thresh_boxes_num - 1
                    color_thresh_boxes_index = min(color_thresh_boxes_num - 1, color_thresh_boxes_index + 1);
                    // 在控制台打印信息
                    ets_printf("increase color area\n");
                    // 在图像上绘制一个空心矩形，矩形的边界由color_thresh_boxes中的一个元素指定，颜色由draw_lcd_colors指定
                    dl::image::draw_hollow_rectangle((uint16_t *)frame->buf, (int)frame->height, (int)frame->width,
                                                     color_thresh_boxes[color_thresh_boxes_index][0],
                                                     color_thresh_boxes[color_thresh_boxes_index][1],
                                                     color_thresh_boxes[color_thresh_boxes_index][2],
                                                     color_thresh_boxes[color_thresh_boxes_index][3],
                                                     draw_lcd_colors[1]);
                    break;
                // 如果需要减少颜色区域
                case DECREASE_COLOR_AREA:
                    // 减少color_thresh_boxes_index的值，但不小于0
                    color_thresh_boxes_index = max(0, color_thresh_boxes_index - 1);
                    // 在控制台打印信息
                    ets_printf("decrease color area\n");
                    // 在图像上绘制一个空心矩形，矩形的边界由color_thresh_boxes中的一个元素指定，颜色由draw_lcd_colors指定
                    dl::image::draw_hollow_rectangle((uint16_t *)frame->buf, (int)frame->height, (int)frame->width,
                                                     color_thresh_boxes[color_thresh_boxes_index][0],
                                                     color_thresh_boxes[color_thresh_boxes_index][1],
                                                     color_thresh_boxes[color_thresh_boxes_index][2],
                                                     color_thresh_boxes[color_thresh_boxes_index][3],
                                                     draw_lcd_colors[1]);
                    break;
                // 如果需要注册颜色
                case REGISTER_COLOR:
                    // 调用检测器的cal_color_thresh方法计算颜色阈值，并存储在color_thresh中
                    color_thresh = detector.cal_color_thresh((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, color_thresh_boxes[color_thresh_boxes_index]);
                    // 将color_thresh注册到检测器
                    detector.register_color(color_thresh);
                    // 在控制台打印信息
                    ets_printf("register color, color_thresh: %d, %d, %d, %d, %d, %d\n", color_thresh[0], color_thresh[1], color_thresh[2], color_thresh[3], color_thresh[4], color_thresh[5]);
                    // 等待互斥信号量
                    xSemaphoreTake(xMutex, portMAX_DELAY);
                    // 结束注册模式
                    register_mode = false;
                    // 释放互斥信号量
                    xSemaphoreGive(xMutex);
                    break;
                // 如果需要关闭颜色注册框
                case CLOSE_REGISTER_COLOR_BOX:
                    // 在控制台打印信息
                    ets_printf("close register color box \n");
                    // 等待互斥信号量
                    xSemaphoreTake(xMutex, portMAX_DELAY);
                    // 结束注册模式
                    register_mode = false;
                    // 释放互斥信号量
                    xSemaphoreGive(xMutex);
                    break;
                // 对于其他事件
                default:
                    // 在图像上绘制一个空心矩形，矩形的边界由color_thresh_boxes中的一个元素指定，颜色由draw_lcd_colors指定
                    dl::image::draw_hollow_rectangle((uint16_t *)frame->buf, (int)frame->height, (int)frame->width,
                                                     color_thresh_boxes[color_thresh_boxes_index][0],
                                                     color_thresh_boxes[color_thresh_boxes_index][1],
                                                     color_thresh_boxes[color_thresh_boxes_index][2],
                                                     color_thresh_boxes[color_thresh_boxes_index][3],
                                                     draw_lcd_colors[1]);
                    break;
                }
            }
            // 如果不处于注册模式
            else
            {
                // 根据_gEvent的值执行不同的操作
                switch (_gEvent)
                {
                // 如果需要增加颜色区域
                case INCREASE_COLOR_AREA:
                    // 增加color_area_thresh_index的值，但不超过color_area_thresh_num - 1
                    color_area_thresh_index = min(color_area_thresh_num - 1, color_area_thresh_index + 1);
                    // 设置颜色检测器的区域阈值
                    detector.set_area_thresh({color_area_threshes[color_area_thresh_index]});
                    // 在控制台打印信息
                    ets_printf("increase color area thresh to %d\n", color_area_threshes[color_area_thresh_index]);
                    break;

                // 如果需要减少颜色区域
                case DECREASE_COLOR_AREA:
                    // 减少color_area_thresh_index的值，但不小于0
                    color_area_thresh_index = max(0, color_area_thresh_index - 1);
                    // 设置颜色检测器的区域阈值
                    detector.set_area_thresh({color_area_threshes[color_area_thresh_index]});
                    // 在控制台打印信息
                    ets_printf("decrease color area thresh to %d\n", color_area_threshes[color_area_thresh_index]);
                    break;
                // 如果需要删除颜色
                case DELETE_COLOR:
                    // 调用检测器的delete_color方法删除颜色
                    detector.delete_color();
                    // 在控制台打印信息
                    ets_printf("delete color \n");
                    break;
                // 对于其他事件
                default:
                    // 调用检测器的detect方法检测颜色，结果存储在results中
                    std::vector<std::vector<color_detect_result_t>> &results = detector.detect((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                    // 如果需要绘制框
                    if (draw_box)
                    {
                        for (int i = 0; i < results.size(); ++i)
                        {
                            // 只处理绿色框
                            if (draw_lcd_colors[i % draw_colors_num] == RGB565_LCD_GREEN)
                            {
                                draw_color_detection_result((uint16_t *)frame->buf, (int)frame->height, (int)frame->width, results[i], draw_lcd_colors[i % draw_colors_num]);

                                for (int j = 0; j < results[i].size(); ++j)
                                {
                                    // 计算边界框的宽度和高度
                                    int box_width = results[i][j].box[2] - results[i][j].box[0];
                                    int box_height = results[i][j].box[3] - results[i][j].box[1];
                                    int len;
                                    // 根据边界框的大小确定生长阶段
                                    if (box_width * box_height <= 10 * 10)
                                    {
                                        // 发芽期
                                        ets_printf("Germination stage\n");
                                        len = uart_write_bytes(UART_NUM, "Germination stage", strlen("Germination stage"));
                                        if (len == strlen("Germination stage"))
                                        {
                                            ESP_LOGI("UART", "Sent: %s", "Germination stage");
                                        }
                                        else
                                        {
                                            ESP_LOGE("UART", "Failed to send: %s", "Germination stage");
                                        }
                                    }
                                    else if (box_width * box_height <= 50 * 50)
                                    {
                                        // 生长期
                                        ets_printf("Growing stage\n");
                                        len = uart_write_bytes(UART_NUM, "Growing stage", strlen("Growing stage"));
                                        if (len == strlen("Growing stage"))
                                        {
                                            ESP_LOGI("UART", "Sent: %s", "Growing stage");
                                        }
                                        else
                                        {
                                            ESP_LOGE("UART", "Failed to send: %s", "Growing stage");
                                        }
                                    }
                                    else if (box_width * box_height <= 100 * 100)
                                    {
                                        // 成熟期
                                        ets_printf("Mature  stage\n");
                                    }
                                    else
                                    {
                                        // 超过预期大小
                                        ets_printf("Exceeded expected size\n");
                                    }
                                }
                            }
                        }
                    }
                    // 如果不需要绘制框
                    else
                    {
                        // 调用检测器的draw_segmentation_results方法在图像上绘制分割结果，颜色由draw_lcd_colors指定
                        detector.draw_segmentation_results((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, draw_lcd_colors, true, 0x0000);
                    }
                    break;
                }
            }
        }

        // 如果存在输出队列
        if (xQueueFrameO)
        {
            // 将帧发送到输出队列
            xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
        }
        // 如果需要返回帧缓冲
        else if (gReturnFB)
        {
            // 调用esp_camera_fb_return方法返回帧缓冲
            esp_camera_fb_return(frame);
        }
        // 否则
        else
        {
            // 释放帧
            free(frame);
        }
    }
}

// 定义一个用于处理事件的任务
static void task_event_handler(void *arg)
{
    // 创建一个color_detection_state_t类型的变量
    color_detection_state_t _gEvent;
    // 开始无限循环
    while (true)
    {
        // 从事件队列中接收事件，存储在_gEvent中
        xQueueReceive(xQueueEvent, &(_gEvent), portMAX_DELAY);
        // 等待互斥信号量
        xSemaphoreTake(xMutex, portMAX_DELAY);
        // 将gEvent的值设置为_gEvent的当前值
        gEvent = _gEvent;
        // 释放互斥信号量
        xSemaphoreGive(xMutex);
        // 如果需要打开颜色注册框
        if (gEvent == OPEN_REGISTER_COLOR_BOX)
        {
            // 等待互斥信号量
            xSemaphoreTake(xMutex, portMAX_DELAY);
            // 开始注册模式
            register_mode = true;
            // 释放互斥信号量
            xSemaphoreGive(xMutex);
        }
        // 如果需要切换结果显示方式
        else if (gEvent == SWITCH_RESULT)
        {
            // 切换draw_box的值
            draw_box = !draw_box;
        }
    }
}

// 定义一个用于注册颜色检测的函数
void register_color_detection(const QueueHandle_t frame_i,
                              const QueueHandle_t event,
                              const QueueHandle_t result,
                              const QueueHandle_t frame_o,
                              const bool camera_fb_return)
{
    // 将frame_i赋值给xQueueFrameI
    xQueueFrameI = frame_i;
    // 将frame_o赋值给xQueueFrameO
    xQueueFrameO = frame_o;
    // 将event赋值给xQueueEvent
    xQueueEvent = event;
    // 将result赋值给xQueueResult
    xQueueResult = result;
    // 将camera_fb_return赋值给gReturnFB
    gReturnFB = camera_fb_return;
    // 创建一个互斥信号量
    xMutex = xSemaphoreCreateMutex();
    // 创建一个处理任务并绑定到核心
    xTaskCreatePinnedToCore(task_process_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
    // 如果存在事件队列
    if (xQueueEvent)
        // 创建一个事件处理任务并绑定到核心
        xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
}
