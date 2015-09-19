#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/gpio-samsung.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_data/spi-s3c64xx.h>

#define DRVNAME "ads7846_device"

#ifdef MODULE
static struct spi_device *spi_device;
static void ads7846_device_spi_delete(struct spi_master *master, unsigned cs)
{
	struct device *dev;
	char str[32];

	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);

	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	if (dev) {
		pr_info(DRVNAME": Deleting %s\n", str);
		device_del(dev);
	}
}

static int ads7846_device_spi_device_register(struct spi_board_info *spi)
{
	struct spi_master *master;

	master = spi_busnum_to_master(spi->bus_num);
	if (!master) {
		pr_err(DRVNAME ":  spi_busnum_to_master(%d) returned NULL\n",
								spi->bus_num);
		return -EINVAL;
	}
	/* make sure it's available */
	ads7846_device_spi_delete(master, spi->chip_select);
	spi_device = spi_new_device(master, spi);
	put_device(&master->dev);
	if (!spi_device) {
		pr_err(DRVNAME ":    spi_new_device() returned NULL\n");
		return -EPERM;
	}
	return 0;
}
#else
static int ads7846_device_spi_device_register(struct spi_board_info *spi)
{
	return spi_register_board_info(spi, 1);
}
#endif

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(S3C2410_GPG(11));
}

static struct s3c64xx_spi_csinfo spi0_csi = {
	.line = S3C2410_GPG(10),
	.fb_delay = 0x2,
};

static const struct ads7846_platform_data ads7846_ts_info = {
	.model		= 7846,
	.x_min		= 100,
	.y_min		= 100,
	.x_max		= 0x0fff,
	.y_max		= 0x0fff,
	.vref_mv	= 3300,
	.x_plate_ohms	= 256,
	.penirq_recheck_delay_usecs = 10,
	.settle_delay_usecs = 100,
	.keep_vref_on	= 1,
	.pressure_max	= 1500,
	.debounce_max	= 10,
	.debounce_tol	= 30,
	.debounce_rep	= 1,
	.get_pendown_state	= ads7846_get_pendown_state,    
};

static struct spi_board_info __initdata ads7846_boardinfo = {
    /* MicroWire (bus 2) CS0 has an ads7846e */
    .modalias               = "ads7846",
    .platform_data          = &ads7846_ts_info,
    .max_speed_hz           = 800 * 1000,
    .bus_num                = 0,
    .chip_select            = 1,		// second spi dev, first is fbtft
    .controller_data		= &spi0_csi,
};

static int ads7846_dev_init(void)
{	
	ads7846_boardinfo.irq = gpio_to_irq(S3C2410_GPG(11));
	ads7846_device_spi_device_register(&ads7846_boardinfo);
	return 0;
}

static void ads7846_dev_exit(void)
{
	if (spi_device) {
		if (spi_device->master->cleanup) {
			spi_device->master->cleanup(spi_device);
		}	
		device_del(&spi_device->dev);
		kfree(spi_device);
	}
}

module_init(ads7846_dev_init);
module_exit(ads7846_dev_exit);
MODULE_DESCRIPTION("Add a ads7846 spi device.");
MODULE_AUTHOR("FriendlyARM");
MODULE_LICENSE("GPL");
