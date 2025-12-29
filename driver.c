
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/rtc.h>
#include <linux/time64.h>
#include <linux/types.h>

#define DRIVER_NAME "clock_drv"
#define CLASS_NAME  "clock_drv_class"


#define DS_RST   17
#define DS_DAT   4
#define DS_SCLK  22

#define ENC_S1   24
#define ENC_S2   25
#define ENC_SW   26

#define DHT_GPIO 23


#define LED0  5
#define LED1  6
#define LED2  12
#define LED3  13
#define LED4  16
#define LED5  19
#define LED6  20
#define LED7  21

static int leds[8] = {
    LED0, LED1, LED2, LED3,
    LED4, LED5, LED6, LED7
};

#define DEBOUNCE_MS      6
#define LONGPRESS_MS     1000
#define PAGE_DEBOUNCE_MS 200

#define DHT_CACHE_MS     2000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kkk");
MODULE_DESCRIPTION("DS1302 + rotary/button + DHT11 via /dev/clock_drv");

struct rtc_simple {
    int hh, mm, ss;
    int ch;
};

static DEFINE_MUTEX(lock0);

static struct rtc_simple cur;
static struct rtc_simple edit;
static bool edit_mode = false;
static int edit_field = 2;

static int ui_page = 0;
static unsigned long last_page_switch_j = 0;

static int irq_s1, irq_sw;
static unsigned long last_irq_s1, last_irq_sw;
static unsigned long sw_pressed_jiffies;

static dev_t devno;
static struct cdev cdev0;
static struct class *cls;

static int dht_temp = -1;
static int dht_hum  = -1;
static unsigned long last_dht_j = 0;


static inline void ds_clk_pulse(void)
{
    gpio_set_value(DS_SCLK, 1); udelay(2);
    gpio_set_value(DS_SCLK, 0); udelay(2);
}

static void ds_write_byte(u8 b)
{
    int i;
    gpio_direction_output(DS_DAT, 0);
    for (i = 0; i < 8; i++) {
        gpio_set_value(DS_DAT, (b >> i) & 1);
        ds_clk_pulse();
    }
}

static u8 ds_read_byte(void)
{
    int i;
    u8 v = 0;

    gpio_direction_input(DS_DAT);
    for (i = 0; i < 8; i++) {
        if (gpio_get_value(DS_DAT)) v |= (1 << i);
        ds_clk_pulse();
    }
    return v;
}

static inline int bcd2int(u8 b) { return (b & 0x0F) + ((b >> 4) & 0x0F) * 10; }
static inline u8  int2bcd(int v){ return (u8)(((v/10) << 4) | (v%10)); }

static void ds1302_read_time(struct rtc_simple *t)
{
    u8 sec, min, hour;

    gpio_set_value(DS_SCLK, 0);
    gpio_set_value(DS_RST, 1);
    udelay(4);

    ds_write_byte(0xBF);
    sec  = ds_read_byte();
    min  = ds_read_byte();
    hour = ds_read_byte();

    ds_read_byte(); ds_read_byte(); ds_read_byte(); ds_read_byte(); ds_read_byte();

    gpio_set_value(DS_RST, 0);

    t->ch = (sec >> 7) & 1;
    t->ss = bcd2int(sec & 0x7F);
    t->mm = bcd2int(min);
    t->hh = bcd2int(hour & 0x3F);
}

static void ds1302_write_reg(u8 cmd, u8 data)
{
    gpio_set_value(DS_SCLK, 0);
    gpio_set_value(DS_RST, 1);
    udelay(4);

    ds_write_byte(cmd);
    ds_write_byte(data);

    gpio_set_value(DS_RST, 0);
}

static void ds1302_set_time(const struct rtc_simple *t)
{
    ds1302_write_reg(0x8E, 0x00);
    ds1302_write_reg(0x80, int2bcd(t->ss) & 0x7F);
    ds1302_write_reg(0x82, int2bcd(t->mm));
    ds1302_write_reg(0x84, int2bcd(t->hh));
    ds1302_write_reg(0x8E, 0x80);
}

static void clamp_time(struct rtc_simple *t)
{
    if (t->ss < 0) t->ss = 59;
    if (t->ss > 59) t->ss = 0;

    if (t->mm < 0) t->mm = 59;
    if (t->mm > 59) t->mm = 0;

    if (t->hh < 0) t->hh = 23;
    if (t->hh > 23) t->hh = 0;
}

static const char *field_name(int f)
{
    if (f == 2) return "HOUR";
    if (f == 1) return "MIN";
    return "SEC";
}


static int dht_wait_level(int level, int timeout_us)
{
    int i;
    for (i = 0; i < timeout_us; i++) {
        if (gpio_get_value(DHT_GPIO) == level)
            return i;
        udelay(1);
    }
    return -ETIMEDOUT;
}

static int dht_measure_high_us(int timeout_us)
{
    int i = 0;
    while (gpio_get_value(DHT_GPIO) == 1) {
        if (i++ >= timeout_us) return timeout_us;
        udelay(1);
    }
    return i;
}

static int dht11_read_once(int *out_temp, int *out_hum)
{
    u8 data[5] = {0};
    int bit, byte;
    unsigned long flags;

    gpio_direction_output(DHT_GPIO, 0);
    msleep(20);
    gpio_set_value(DHT_GPIO, 1);
    udelay(40);
    gpio_direction_input(DHT_GPIO);

    local_irq_save(flags);

    if (dht_wait_level(0, 100) < 0) goto err;
    if (dht_wait_level(1, 100) < 0) goto err;
    if (dht_wait_level(0, 100) < 0) goto err;

    for (bit = 0; bit < 40; bit++) {
        int high_us;

        if (dht_wait_level(1, 70) < 0) goto err;

        high_us = dht_measure_high_us(100);

        byte = bit / 8;
        data[byte] <<= 1;
        data[byte] |= (high_us > 40) ? 1 : 0;

        (void)dht_wait_level(0, 70);
    }

    local_irq_restore(flags);

    if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4])
        return -EIO;

    *out_hum  = data[0];
    *out_temp = data[2];
    return 0;

err:
    local_irq_restore(flags);
    return -ETIMEDOUT;
}

static void dht11_get_cached(int *out_temp, int *out_hum)
{
    unsigned long now = jiffies;

    if (time_after(now, last_dht_j + msecs_to_jiffies(DHT_CACHE_MS)) || last_dht_j == 0) {
        int t, h;
        int ret = dht11_read_once(&t, &h);
        if (ret == 0) {
            dht_temp = t;
            dht_hum  = h;
        }
        last_dht_j = now;
    }

    *out_temp = dht_temp;
    *out_hum  = dht_hum;
}

static void apply_delta_locked(int delta)
{
    if (!edit_mode) return;
    if (ui_page != 0) return;

    if (edit_field == 0) edit.ss += delta;
    else if (edit_field == 1) edit.mm += delta;
    else edit.hh += delta;

    clamp_time(&edit);
}

static void short_press_locked(void)
{
    if (!edit_mode) return;
    if (ui_page != 0) return;

    edit_field = (edit_field == 0) ? 2 : (edit_field - 1);
}

static void long_press_action(void)
{
    struct rtc_simple t;

    mutex_lock(&lock0);

    if (ui_page != 0) {
        mutex_unlock(&lock0);
        return;
    }

    if (!edit_mode) {
        ds1302_read_time(&cur);
        edit = cur;
        edit_mode = true;
        edit_field = 2;
        mutex_unlock(&lock0);
        return;
    }

    t = edit;
    edit_mode = false;
    mutex_unlock(&lock0);

    ds1302_set_time(&t);
}

static void page_toggle_if_cw_locked(void)
{
    unsigned long now = jiffies;

    if (edit_mode) return;

    if (time_before(now, last_page_switch_j + msecs_to_jiffies(PAGE_DEBOUNCE_MS)))
        return;

    if (gpio_get_value(ENC_S2)) {
        
        ui_page = (ui_page + 1) % 3;
    } else {
        ui_page = (ui_page + 3 - 1) % 3;
    }

    last_page_switch_j = now;
}



static irqreturn_t s1_irq_handler(int irq, void *dev_id)
{
    unsigned long now = jiffies;

    if (time_before(now, last_irq_s1 + msecs_to_jiffies(DEBOUNCE_MS)))
        return IRQ_HANDLED;
    last_irq_s1 = now;

    mutex_lock(&lock0);

    if (!edit_mode) {
        page_toggle_if_cw_locked();
    } else {
        if (gpio_get_value(ENC_S2))
            apply_delta_locked(+1);
        else
            apply_delta_locked(-1);
    }

    mutex_unlock(&lock0);
    return IRQ_HANDLED;
}

static irqreturn_t sw_irq_handler(int irq, void *dev_id)
{
    unsigned long now = jiffies;

    if (time_before(now, last_irq_sw + msecs_to_jiffies(DEBOUNCE_MS)))
        return IRQ_HANDLED;
    last_irq_sw = now;

    if (gpio_get_value(ENC_SW) == 0) {
        sw_pressed_jiffies = now;
    } else {
        unsigned long held_ms = jiffies_to_msecs(now - sw_pressed_jiffies);
        if (held_ms >= LONGPRESS_MS) {
            long_press_action();
        } else {
            mutex_lock(&lock0);
            short_press_locked();
            mutex_unlock(&lock0);
        }
    }

    return IRQ_HANDLED;
}

static void set_led_level(int level)
{
    int i;

    if (level < 0) level = 0;
    if (level > 8) level = 8;

    for (i = 0; i < 8; i++) {
        gpio_set_value(leds[i], (i < level) ? 1 : 0);
    }
}


static ssize_t dev_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
    char kbuf[160];
    int len;
    struct rtc_simple t;
    bool mode;
    int field, page;
    int temp, hum;

    if (*ppos > 0) return 0;

    ds1302_read_time(&cur);

    mutex_lock(&lock0);
    mode  = edit_mode;
    field = edit_field;
    page  = ui_page;
    t     = (mode && page==0) ? edit : cur;
    mutex_unlock(&lock0);

    dht11_get_cached(&temp, &hum);

    len = snprintf(kbuf, sizeof(kbuf),
                   "%02d:%02d:%02d MODE=%s FIELD=%s PAGE=%d TEMP=%d HUM=%d\n",
                   t.hh, t.mm, t.ss,
                   (mode && page==0) ? "EDIT" : "RUN",
                   field_name(field),
                   page,
                   temp, hum);

    if (len > cnt) len = cnt;
    if (copy_to_user(ubuf, kbuf, len)) return -EFAULT;

    *ppos += len;
    return len;
}

static ssize_t dev_write(struct file *f, const char __user *ubuf,
                         size_t cnt, loff_t *ppos)
{
    char kbuf[64];
    int hh, mm, ss;
    int level;

    if (cnt >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, cnt))
        return -EFAULT;

    kbuf[cnt] = '\0';   

    
    if (sscanf(kbuf, "LED %d", &level) == 1) {
        set_led_level(level);
        return cnt;
    }


    if (sscanf(kbuf, "SET %d:%d:%d", &hh, &mm, &ss) == 3) {
        struct rtc_simple t = {
            .hh = hh,
            .mm = mm,
            .ss = ss,
            .ch = 0
        };

        clamp_time(&t);
        ds1302_set_time(&t);

        mutex_lock(&lock0);
        edit_mode = false;
        mutex_unlock(&lock0);

        return cnt;
    }

    return -EINVAL;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = dev_read,
    .write = dev_write,
};

static int __init mod_init(void)
{
    int ret;
    struct timespec64 ts;
    struct rtc_time tm_val;

    printk(KERN_INFO "==== %s init ====\n", DRIVER_NAME);

    ret = alloc_chrdev_region(&devno, 0, 1, DRIVER_NAME);
    if (ret < 0) return ret;

    for (int i = 0; i < 8; i++) {
        gpio_request(leds[i], "led");
        gpio_direction_output(leds[i], 0);
    }
    
    cdev_init(&cdev0, &fops);
    ret = cdev_add(&cdev0, devno, 1);
    if (ret < 0) goto err_chr;

    cls = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(cls)) { ret = PTR_ERR(cls); goto err_cdev; }

    device_create(cls, NULL, devno, NULL, DRIVER_NAME);

    if (gpio_request(DS_RST,"ds_rst")||
        gpio_request(DS_DAT,"ds_dat")||
        gpio_request(DS_SCLK,"ds_sclk")||
        gpio_request(ENC_S1,"enc_s1")||
        gpio_request(ENC_S2,"enc_s2")||
        gpio_request(ENC_SW,"enc_sw")||
        gpio_request(DHT_GPIO,"dht11")) {
        ret = -EBUSY;
        goto err_dev;
    }

    gpio_direction_output(DS_RST,0);
    gpio_direction_output(DS_SCLK,0);
    gpio_direction_output(DS_DAT,0);

    gpio_direction_input(ENC_S1);
    gpio_direction_input(ENC_S2);
    gpio_direction_input(ENC_SW);
    gpio_direction_input(DHT_GPIO);

    irq_s1 = gpio_to_irq(ENC_S1);
    irq_sw = gpio_to_irq(ENC_SW);

    ret = request_irq(irq_s1,s1_irq_handler,IRQF_TRIGGER_FALLING,"enc_s1_irq",NULL);
    if (ret) goto err_gpio;

    ret = request_irq(irq_sw,sw_irq_handler,
                      IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING,
                      "enc_sw_irq",NULL);
    if (ret) { free_irq(irq_s1,NULL); goto err_gpio; }

    ds1302_read_time(&cur);
    if (cur.ch==1) {
        ds1302_set_time(&cur);
        ds1302_read_time(&cur);
    }

    ktime_get_real_ts64(&ts);
    rtc_time64_to_tm(ts.tv_sec,&tm_val);

    mutex_lock(&lock0);
    cur.hh = (tm_val.tm_hour+24-3)%24;
    cur.mm = tm_val.tm_min;
    cur.ss = tm_val.tm_sec;
    cur.ch = 0;

    ds1302_set_time(&cur);
    edit = cur;
    edit_mode = false;
    edit_field = 2;
    ui_page = 0;
    mutex_unlock(&lock0);

    printk(KERN_INFO "OK: DHT cached every %d ms\n", DHT_CACHE_MS);
    return 0;

err_gpio:
    gpio_free(DHT_GPIO);
    gpio_free(ENC_SW); gpio_free(ENC_S2); gpio_free(ENC_S1);
    gpio_free(DS_SCLK); gpio_free(DS_DAT); gpio_free(DS_RST);
err_dev:
    device_destroy(cls,devno);
    class_destroy(cls);
err_cdev:
    cdev_del(&cdev0);
err_chr:
    unregister_chrdev_region(devno,1);
    return ret;
}

static void __exit mod_exit(void)
{
    free_irq(irq_s1,NULL);
    free_irq(irq_sw,NULL);

    gpio_free(DHT_GPIO);
    gpio_free(ENC_SW); gpio_free(ENC_S2); gpio_free(ENC_S1);
    gpio_free(DS_SCLK); gpio_free(DS_DAT); gpio_free(DS_RST);

    for (int i = 0; i < 8; i++)
        gpio_free(leds[i]);

    device_destroy(cls,devno);
    class_destroy(cls);
    cdev_del(&cdev0);
    unregister_chrdev_region(devno,1);

    printk(KERN_INFO "==== %s exit ====\n", DRIVER_NAME);
}

module_init(mod_init);
module_exit(mod_exit);
