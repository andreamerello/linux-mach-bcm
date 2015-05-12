/*
 * Raspberry cpufreq driver
 * Copyright (C) Andrea Merello <andrea.merello@gmail.com>
 *
 * Partially based on tegra cpufreq driver, which is
 * Copyright (C) 2010 Google, Inc.
 *
 * Partially based on bcm2708 cpufreq driver, which is
 * Copyright (C) 2011 Broadcom
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <soc/bcm2835/raspberrypi-firmware-property.h>

/* It seems raspberry could handle only two freqs (so called max and min).
 * The table will be filled by asking for these values to the fw
 */
static struct cpufreq_frequency_table raspberrypi_freq_table[3] = {
	[2] = {.frequency = CPUFREQ_TABLE_END},
};

static struct clk *arm_clk;

static unsigned int raspberrypi_cpufreq_get_clock(unsigned int cpu)
{
	unsigned long val;
	const unsigned long mean = (raspberrypi_freq_table[0].frequency +
				raspberrypi_freq_table[1].frequency) / 2;

	/* This value floats. It seems the FW actually
	 * _measures_ the clock (roughly)
	 */
	val = clk_get_rate(arm_clk) / 1000;

	return (val < mean) ? raspberrypi_freq_table[0].frequency :
		raspberrypi_freq_table[1].frequency;
}

static int raspberrypi_cpufreq_set_clock(struct cpufreq_policy *policy,
					unsigned int index)
{
	int ret;
	unsigned int target_freq = raspberrypi_freq_table[index].frequency;

	ret = clk_set_rate(arm_clk, target_freq * 1000);
	if (ret)
		return ret;

	policy->cur = target_freq;
	return 0;
}

static int raspberrypi_clk_get_range(struct device_node *of_node,
				unsigned long *min, unsigned long *max)
{
	u32 packet[2];
	int ret;
	const u32 RASPBERRY_CLOCK_ARM = 3;

	packet[0] = RASPBERRY_CLOCK_ARM;
	packet[1] = 0;
	ret = raspberrypi_firmware_property(of_node,
					RASPBERRYPI_FIRMWARE_GET_MIN_CLOCK_RATE,
					&packet, sizeof(packet));
	if (ret)
		return ret;
	*min = packet[1];

	packet[0] = RASPBERRY_CLOCK_ARM;
	packet[1] = 0;
	ret = raspberrypi_firmware_property(of_node,
					RASPBERRYPI_FIRMWARE_GET_MAX_CLOCK_RATE,
					&packet, sizeof(packet));
	if (ret)
		return ret;
	*max = packet[1];
	return 0;
}


static int raspberrypi_cpufreq_init(struct cpufreq_policy *policy)
{
	/* from reference code: it has been measured (nS) */
	const unsigned int transition_latency = 355000;

	return cpufreq_generic_init(policy, raspberrypi_freq_table, transition_latency);
}


static struct cpufreq_driver raspberrypi_cpufreq_driver = {
	.verify	       	= cpufreq_generic_frequency_table_verify,
	.target_index	= raspberrypi_cpufreq_set_clock,
	.get		= raspberrypi_cpufreq_get_clock,
	.name		= "rpi cpufreq",
	.init		= raspberrypi_cpufreq_init,
	.attr		= cpufreq_generic_attr,
};

static int raspberrypi_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *firmware_node;
	unsigned long max, min;
	int ret;

	arm_clk = clk_get_sys("arm", NULL);
	if (IS_ERR(arm_clk)) {
		pr_err("Failed to get the 'arm' clock %x\n", arm_clk);
		return -EPROBE_DEFER;
	}

	firmware_node = of_parse_phandle(pdev->dev.of_node, "firmware", 0);
	if (!firmware_node) {
		pr_err("Failed to get 'firmware' OF node");
		return -EPROBE_DEFER;
	}

	ret = raspberrypi_clk_get_range(firmware_node, &min, &max);
	if (ret) {
		pr_err("Failed to get clock range from FW\n");
		return ret;
	}


	raspberrypi_freq_table[0].frequency = min / 1000;
	raspberrypi_freq_table[1].frequency = max / 1000;

	return cpufreq_register_driver(&raspberrypi_cpufreq_driver);
}

static int raspberrypi_cpufreq_remove(struct platform_device *pdev)
{
        return cpufreq_unregister_driver(&raspberrypi_cpufreq_driver);
}

static const struct of_device_id raspberrypi_cpufreq_of_match[] =
{
	{ .compatible = "raspberrypi,cpufreq" },
	{}
}

MODULE_DEVICE_TABLE(of, raspberrypi_cpufreq_of_match);

static struct platform_driver raspberrypi_cpufreq_platdrv = {
	.driver = {
		.name		= "raspberrypi-cpufreq",
		.of_match_table = raspberrypi_cpufreq_of_match
	},
	.probe		= raspberrypi_cpufreq_probe,
	.remove		= raspberrypi_cpufreq_remove,
};
module_platform_driver(raspberrypi_cpufreq_platdrv);


MODULE_AUTHOR("Andrea Merello <andrea.merello@gmail.com>");
MODULE_DESCRIPTION("Cpufreq driver for Raspberry");
MODULE_LICENSE("GPL");
