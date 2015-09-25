#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/interrupt.h> 
#include <linux/sched.h>
#include <mach/gpio-samsung.h>

static int sw_pin = S3C2410_GPF(3);
static int sia_pin = S3C2410_GPF(1);
static int sib_pin = S3C2410_GPF(2);
static int sia_irq = -1;
static int sw_irq = -1;
static int counter = 0;
static int sw = -1;

// This function is called when you read /sys/class/rotary_sensor/value
static ssize_t rotary_sensor_sw_read(struct class *class, struct class_attribute *attr, char *buf)
{
	if (sw == 0 || sw == 1)
    	return sprintf(buf, "%d\n", sw);
    return -1;
}

static ssize_t rotary_sensor_value_read(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", counter);
}

// Sysfs definitions for rotary_sensor class
static struct class_attribute rotary_sensor_class_attrs[] = {
        __ATTR(value,	S_IRUGO, rotary_sensor_value_read, NULL),
        __ATTR(sw,	S_IRUGO, rotary_sensor_sw_read, NULL),
        __ATTR_NULL,
};

// Name of directory created in /sys/class
static struct class rotary_sensor_class = {
        .name =			"rotary_sensor",
        .owner =		THIS_MODULE, 
        .class_attrs =	rotary_sensor_class_attrs,
};

static irqreturn_t sw_isr(int irq, void *data)
{	
	if (gpio_get_value(sw_pin) == 1) {
		sw = 1;
	} else {
		sw = 0;
	}
    // printk("%s sw=%d\n", __func__, sw);
    return IRQ_HANDLED;
}

static irqreturn_t rotate_isr(int irq, void *data)
{	
	if (gpio_get_value(sib_pin) == 1) {
		counter++;
	} else {
		counter--;
	}
    // printk("%s value=%d\n", __func__, counter);
    return IRQ_HANDLED;
}

static int rotary_sensor_init(void)
{		
    int ret = -1;

    printk(KERN_INFO "Matrix-rotary_encoder init.\n");
    ret = gpio_request(sw_pin,"sw");
    if (ret) {
        printk(KERN_ERR"gpio request sw_pin fail\n");
        return -1;
    }
    ret = gpio_request(sia_pin,"sia_pin");
    if (ret) {
        printk(KERN_ERR"gpio request sia_pin fail\n");
        goto fail_gpio_sw;
    }
    ret = gpio_request(sib_pin,"sib_pin");
    if (ret) {
        printk(KERN_ERR"gpio request sib_pin fail\n");
        goto fail_gpio_sia;
    }

    sw_irq = gpio_to_irq(sw_pin);
    ret = request_irq(sw_irq, sw_isr, IRQ_TYPE_EDGE_BOTH, "rotary_sensor_sw", NULL);
    if (ret) {
        printk(KERN_ERR"%s fail to request_irq sw\n", __func__);
        goto fail_gpio_sib;
    }
    
    sia_irq = gpio_to_irq(sia_pin);
    ret = request_irq(sia_irq, rotate_isr, IRQ_TYPE_EDGE_FALLING , "rotary_sensor_sia", NULL);
    if (ret) {
        printk(KERN_ERR"%s fail to request_irq\n", __func__);
        goto fail_irq_sw;
    }
    
    ret = class_register(&rotary_sensor_class);
    if (ret) 
        goto fail_irq_sia;
    
    return 0;
    
fail_irq_sia:
    free_irq(sia_irq, NULL);
fail_irq_sw:
	free_irq(sw_irq, NULL);
fail_gpio_sib:
    gpio_free(sib_pin);
fail_gpio_sia:
	gpio_free(sia_pin);
fail_gpio_sw:
	gpio_free(sw_pin);
    return ret;

}

static void rotary_sensor_exit(void)
{
    printk(KERN_INFO "Matrix-rotary_encoder exit.\n");
    
    class_unregister(&rotary_sensor_class);
    free_irq(sia_irq, NULL);
    gpio_free(sib_pin);    
    gpio_free(sia_pin);
    gpio_free(sw_pin);
}

module_init(rotary_sensor_init);
module_exit(rotary_sensor_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("FriendlyARM");
MODULE_DESCRIPTION("Driver for Matrix-Rotary_Sensor");
