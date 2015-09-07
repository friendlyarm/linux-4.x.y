/*
o - GPIO w1 bus master driver
 *
 * Copyright (C) 2007 Ville Syrjala <syrjala@sci.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/w1-gpio.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>

#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-samsung.h>


#include "../w1.h"
#include "../w1_int.h"

#define DEVICE_NAME "w1GPIO"

static u8 w1_gpio_set_pullup(void *data, int delay)
{
	struct w1_gpio_platform_data *pdata = data;

	if (delay) {
		pdata->pullup_duration = delay;
	} else {
		if (pdata->pullup_duration) {
			gpio_direction_output(pdata->pin, 1);

			msleep(pdata->pullup_duration);

			gpio_direction_input(pdata->pin);
		}
		pdata->pullup_duration = 0;
	}

	return 0;
}

static void w1_gpio_write_bit_dir(void *data, u8 bit)
{
	struct w1_gpio_platform_data *pdata = data;

	if (bit)
		gpio_direction_input(pdata->pin);
	else
		gpio_direction_output(pdata->pin, 0);
}

static void w1_gpio_write_bit_val(void *data, u8 bit)
{
	struct w1_gpio_platform_data *pdata = data;

	gpio_set_value(pdata->pin, bit);
}

static u8 w1_gpio_read_bit(void *data)
{
	struct w1_gpio_platform_data *pdata = data;

	return gpio_get_value(pdata->pin) ? 1 : 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id w1_gpio_dt_ids[] = {
	{ .compatible = "w1-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, w1_gpio_dt_ids);
#endif

static int w1_gpio_probe_dt(struct platform_device *pdev)
{
	struct w1_gpio_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	int gpio;
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (of_get_property(np, "linux,open-drain", NULL))
		pdata->is_open_drain = 1;

	gpio = of_get_gpio(np, 0);
	if (gpio < 0) {
		if (gpio != -EPROBE_DEFER)
			dev_err(&pdev->dev,
					"Failed to parse gpio property for data pin (%d)\n",
					gpio);

		return gpio;
	}
	pdata->pin = gpio;

	gpio = of_get_gpio(np, 1);
	if (gpio == -EPROBE_DEFER)
		return gpio;
	/* ignore other errors as the pullup gpio is optional */
	pdata->ext_pullup_enable_pin = gpio;

	pdev->dev.platform_data = pdata;

	return 0;
}

static struct platform_device *g_pdev;

static int w1_gpio_hwinit(struct platform_device *pdev)
{
	struct w1_bus_master *master;
	struct w1_gpio_platform_data *pdata;
	int err;

	if (of_have_populated_dt()) {
		err = w1_gpio_probe_dt(pdev);
		if (err < 0)
			return err;
	}

	pdata = dev_get_platdata(&pdev->dev);

	if (!pdata) {
		dev_err(&pdev->dev, "No configuration data\n");
		return -ENXIO;
	}

	master = devm_kzalloc(&pdev->dev, sizeof(struct w1_bus_master),
			GFP_KERNEL);
	if (!master) {
		dev_err(&pdev->dev, "Out of memory\n");
		return -ENOMEM;
	}

	err = devm_gpio_request(&pdev->dev, pdata->pin, "w1");
	if (err) {
		dev_err(&pdev->dev, "gpio_request (pin) failed\n");
		goto free_master;
	}

	if (gpio_is_valid(pdata->ext_pullup_enable_pin)) {
		err = devm_gpio_request_one(&pdev->dev,
				pdata->ext_pullup_enable_pin, GPIOF_INIT_LOW,
				"w1 pullup");
		if (err < 0) {
			dev_err(&pdev->dev, "gpio_request_one "
					"(ext_pullup_enable_pin) failed\n");
			goto free_gpio;
		}
	}

	master->data = pdata;
	master->read_bit = w1_gpio_read_bit;

	if (pdata->is_open_drain) {
		gpio_direction_output(pdata->pin, 1);
		master->write_bit = w1_gpio_write_bit_val;
	} else {
		gpio_direction_input(pdata->pin);
		master->write_bit = w1_gpio_write_bit_dir;
		master->set_pullup = w1_gpio_set_pullup;
	}

	err = w1_add_master_device(master);
	if (err) {
		dev_err(&pdev->dev, "w1_add_master device failed\n");
		goto free_gpio_ext_pu;
	}

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(1);

	if (gpio_is_valid(pdata->ext_pullup_enable_pin))
		gpio_set_value(pdata->ext_pullup_enable_pin, 1);

	platform_set_drvdata(pdev, master);

	return 0;

 free_gpio_ext_pu:
        if (gpio_is_valid(pdata->ext_pullup_enable_pin))
                devm_gpio_free(&pdev->dev, pdata->ext_pullup_enable_pin);
 free_gpio:
       devm_gpio_free(&pdev->dev, pdata->pin);
 free_master:
        w1_remove_master_device(master);
        return err;
}

static int w1_gpio_probe(struct platform_device *pdev)
{
        struct w1_gpio_platform_data *pdata;

        g_pdev = pdev;
        pdata = g_pdev->dev.platform_data;
        pdata->pin = -1;
        return 0;
}

static int w1_gpio_hwexit(struct platform_device *pdev)
{
	struct w1_bus_master *master = platform_get_drvdata(pdev);
	struct w1_gpio_platform_data *pdata = dev_get_platdata(&pdev->dev);

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(0);

	if (gpio_is_valid(pdata->ext_pullup_enable_pin))
		gpio_set_value(pdata->ext_pullup_enable_pin, 0);

	w1_remove_master_device(master);
	devm_gpio_free(&pdev->dev, pdata->pin);
	return 0;
}

static int w1_gpio_remove(struct platform_device *pdev)
{
        return 0;
}

#ifdef CONFIG_PM

static int w1_gpio_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct w1_gpio_platform_data *pdata = dev_get_platdata(&pdev->dev);

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(0);

	return 0;
}

static int w1_gpio_resume(struct platform_device *pdev)
{
	struct w1_gpio_platform_data *pdata = dev_get_platdata(&pdev->dev);

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(1);

	return 0;
}

#else
#define w1_gpio_suspend	NULL
#define w1_gpio_resume	NULL
#endif

static struct platform_driver w1_gpio_driver = {
	.driver = {
		.name	= "w1-gpio",
		.of_match_table = of_match_ptr(w1_gpio_dt_ids),
	},
	.probe = w1_gpio_probe,
	.remove	= w1_gpio_remove,
	.suspend = w1_gpio_suspend,
	.resume = w1_gpio_resume,
};

static long nanopi_w1GPIO_ioctl(struct file *filp, unsigned int cmd,
                unsigned long arg)
{
#define SET_W1GPIO_PIN                  (0)
#define UNSET_W1GPIO_PIN                                (1)
#define GET_W1GPIO_PIN                              (4)

        int *pin = (int *)arg;
        int gpio;
        struct w1_gpio_platform_data *pdata = g_pdev->dev.platform_data;;

        switch(cmd) {
                case SET_W1GPIO_PIN:
                        if ( pin != NULL) {
                                gpio = *pin;
                                if (gpio>S3C2410_GPA(0) && gpio<S3C_GPIO_END && pdata->pin == -1) {
                                        pdata->pin = gpio;
                                        if (w1_gpio_hwinit(g_pdev)) {
                                                return -EINVAL;
                                        }
                                } else {
                                        return -EINVAL;
                                }
                        } else {
                                return -EINVAL;
                        }
                        break;
                case UNSET_W1GPIO_PIN:
                        if (pdata->pin != -1) {
                                if (w1_gpio_hwexit(g_pdev)) {
                                                return -EINVAL;
                                }
                                pdata->pin = -1;
                        } else {
                                return -EINVAL;
                        }
                        break;
                case GET_W1GPIO_PIN:
                        if (pin != NULL) {
                                *pin = pdata->pin;
                        } else {
                                return -EINVAL;
                        }
                        break;
                default:
                        return -EINVAL;
        }

        return 0;
}

static struct file_operations nanopi_w1GPIO_fops = {
        .owner                  = THIS_MODULE,
        .unlocked_ioctl         = nanopi_w1GPIO_ioctl,
};

static struct miscdevice nanopi_w1GPIO_dev = {
        .minor                  = MISC_DYNAMIC_MINOR,
        .name                   = DEVICE_NAME,
        .fops                   = &nanopi_w1GPIO_fops,
};

static int __init nanopi_w1GPIO_dev_init(void) {
        int ret;
        ret = misc_register(&nanopi_w1GPIO_dev);
        printk(DEVICE_NAME"\tinitialized\n");
        return ret;
}

static void __exit nanopi_w1GPIO_dev_exit(void) {
        misc_deregister(&nanopi_w1GPIO_dev);
}

module_init(nanopi_w1GPIO_dev_init);
module_exit(nanopi_w1GPIO_dev_exit);
module_platform_driver(w1_gpio_driver);

MODULE_DESCRIPTION("GPIO w1 bus master driver");
MODULE_AUTHOR("Ville Syrjala <syrjala@sci.fi>");
MODULE_LICENSE("GPL");
