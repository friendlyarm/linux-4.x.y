/*
 * linux/drivers/char/nanopi_pwm.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <mach/gpio-samsung.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>


#define DEVICE_NAME				"pwm"

#define PWM_IOCTL_SET_FREQ		(0x1)
#define PWM_IOCTL_STOP			(0x0)

#define NS_IN_1HZ				(1000000000UL)


static int is_pwm_working[] = {0,0};
static struct pwm_device *pwm4buzzer[2];
static struct semaphore lock;

static int nanopi_pwm_hw_init(int index, int pin);
static void nanopi_pwm_hw_exit(int index, int pin);

static int pwm_set_freq(int index, int pin, unsigned long freq, int duty) {
	int period_ns = NS_IN_1HZ / freq;
	int duty_ns = period_ns  / 1000 * duty;

	if (pwm_config(pwm4buzzer[index], duty_ns, period_ns) < 0) {
		printk("fail to pwm_config\n");
		return -1;
	}
	if (pwm_enable(pwm4buzzer[index]) < 0) {
		printk("fail to pwm_enable\n");
		return -1;
	}
	s3c_gpio_cfgpin(pin, S3C_GPIO_SFN(2));
	return 0;
}

static void pwm_stop(int index, int pin) {
	s3c_gpio_cfgpin(pin, S3C_GPIO_OUTPUT);
	pwm_config(pwm4buzzer[index], 0, NS_IN_1HZ / 100);
	pwm_disable(pwm4buzzer[index]);
}

static int nanopi_pwm_open(struct inode *inode, struct file *file) {
	if (!down_trylock(&lock))
		return 0;
	else
		return -EBUSY;
}

static int nanopi_pwm_close(struct inode *inode, struct file *file) {
	up(&lock);
	return 0;
}

static long nanopi_pwm_ioctl(struct file *filep, unsigned int cmd,unsigned long arg)
{
	long * value;
	int pwm_id;
	int gpio;
	int freq;
	int duty;

	switch (cmd) {
		case PWM_IOCTL_SET_FREQ:
			value = (long *)arg;
			if (value != NULL) {
				gpio = value[0];
				freq = value[1];
				duty = value[2];
				if (gpio == S3C2410_GPB(0)) {
					pwm_id = 0;
				} else if (gpio == S3C2410_GPB(1)) {
					pwm_id = 1;
				} else {
					printk("Invalid pwm id\n");
					return -EINVAL;
				}

				if (duty < 0 || duty > 1000) {
					printk("Invalid pwm duty\n");
					return -EINVAL;
				}
				
				if (is_pwm_working[pwm_id] == 1) {
					nanopi_pwm_hw_exit(pwm_id, gpio);
				}
				
				if (nanopi_pwm_hw_init(pwm_id, gpio)) {
					printk("fail to nanopi_pwm_hw_init\n");
					return -EINVAL;
				}
				is_pwm_working[pwm_id] = 1;
				if (pwm_set_freq(pwm_id, gpio, freq, duty)) {
					printk("fail to pwm_set_freq\n");
					return -EINVAL;
				}
			}
			break;
		case PWM_IOCTL_STOP:
			value = (long *)arg;
			if (value != NULL) {
				gpio = value[0];
				if (gpio == S3C2410_GPB(0)) {
					pwm_id = 0;
				} else if (gpio == S3C2410_GPB(1)) {
					pwm_id = 1;
				} else {
					printk("Invalid pwm id\n");
					return -EINVAL;
				}
				if (is_pwm_working[pwm_id] == 1) {
					nanopi_pwm_hw_exit(pwm_id, gpio);
					is_pwm_working[pwm_id] = 0;
				}
			}
			break;
		default:
			printk("%s unsupported pwm ioctl %d", __FUNCTION__, cmd);
			break;
	}

	return 0;
}


static struct file_operations nanopi_pwm_ops = {
	.owner			= THIS_MODULE,
	.open			= nanopi_pwm_open,
	.release		= nanopi_pwm_close, 
	.unlocked_ioctl		= nanopi_pwm_ioctl,
};

static struct miscdevice nanopi_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &nanopi_pwm_ops,
};

static int nanopi_pwm_hw_init(int index, int pin)
{
	int ret;

	ret = gpio_request(pin, DEVICE_NAME);
	if (ret) {
		printk("request GPIO %d for pwm failed\n", pin);
		return ret;
	}

	gpio_set_value(pin, 0);
	s3c_gpio_cfgpin(pin, S3C_GPIO_OUTPUT);

	pwm4buzzer[index] = pwm_request(index, DEVICE_NAME);
	if (IS_ERR(pwm4buzzer[index])) {
		printk("request pwm%d failed\n", index);
		gpio_free(pin);
		return -ENODEV;
	}

	pwm_stop(index, pin);
	return ret;
}

static void nanopi_pwm_hw_exit(int index,int pin) 
{
	pwm_stop(index, pin);
	pwm_free(pwm4buzzer[index]);
	gpio_free(pin);
}

static int __init nanopi_pwm_dev_init(void) 
{
	int ret;
	ret = misc_register(&nanopi_misc_dev);
	printk(DEVICE_NAME "\tinitialized\n");

	sema_init(&lock, 1);
	return ret;
}

static void __exit nanopi_pwm_dev_exit(void) {
	misc_deregister(&nanopi_misc_dev);
}

module_init(nanopi_pwm_dev_init);
module_exit(nanopi_pwm_dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FriendlyARM Inc.");
MODULE_DESCRIPTION("FriendlyARM nanopi PWM Driver");

