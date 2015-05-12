/*
 * Copyright © 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Implements a clock provider for the clocks controlled by the
 * firmware on Raspberry Pi.
 *
 * These clocks are controlled by the CLOCKMAN peripheral in the
 * hardware, but the ARM doesn't have access to the registers for
 * them.  As a result, we have to call into the firmware to get it to
 * enable, disable, and set their frequencies.
 *
 * We don't have an interface for getting the set of frequencies
 * available from the hardware.  We can request a min/max, but other
 * than that we have to request a frequency and take what it gives us.
 */

#include <dt-bindings/clk/raspberrypi.h>
#include <linux/clk-provider.h>
#include <soc/bcm2835/raspberrypi-firmware-property.h>

struct rpi_firmware_clock {
	/* Clock definitions in our static struct. */
	int clock_id;
	const char *name;
	int flags;

	/* The rest are filled in at init time. */
	struct clk_hw hw;
	struct device *dev;
	struct device_node *firmware_node;
};

static struct rpi_firmware_clock rpi_clocks[] = {
	[RPI_CLOCK_EMMC] = { 1, "emmc", CLK_IS_ROOT | CLK_IGNORE_UNUSED },
	[RPI_CLOCK_UART0] = { 2, "uart0", CLK_IS_ROOT | CLK_IGNORE_UNUSED },
	[RPI_CLOCK_ARM] = { 3, "arm", CLK_IS_ROOT | CLK_IGNORE_UNUSED },
	[RPI_CLOCK_CORE] = { 4, "core", CLK_IS_ROOT | CLK_IGNORE_UNUSED },
	[RPI_CLOCK_V3D] = { 5, "v3d", CLK_IS_ROOT },
	[RPI_CLOCK_H264] = { 6, "h264", CLK_IS_ROOT },
	[RPI_CLOCK_ISP] = { 7, "isp", CLK_IS_ROOT },
	[RPI_CLOCK_SDRAM] = { 8, "sdram", CLK_IS_ROOT | CLK_IGNORE_UNUSED },
	[RPI_CLOCK_PIXEL] = { 9, "pixel", CLK_IS_ROOT | CLK_IGNORE_UNUSED },
	[RPI_CLOCK_PWM] = { 10, "pwm", CLK_IS_ROOT },
};

static int rpi_clk_is_on(struct clk_hw *hw)
{
	struct rpi_firmware_clock *rpi_clk =
		container_of(hw, struct rpi_firmware_clock, hw);
	u32 packet[2];
	int ret;

	packet[0] = rpi_clk->clock_id;
	packet[1] = 0;
	ret = rpi_firmware_property(rpi_clk->firmware_node,
				    RPI_FIRMWARE_GET_CLOCK_STATE,
				    &packet, sizeof(packet));
	if (ret) {
		dev_err(rpi_clk->dev, "Failed to get clock state\n");
		return 0;
	}
	dev_err(rpi_clk->dev, "%s: %s\n", rpi_clk->name, packet[1] ? "on" : "off");

	return packet[1] != 0;
}

static int rpi_clk_set_enable(struct rpi_firmware_clock *rpi_clk, bool enable)
{
	u32 packet[2];
	int ret;

	dev_err(rpi_clk->dev, "Setting %s %s\n", rpi_clk->name, enable ? "on" : "off");

	packet[0] = rpi_clk->clock_id;
	packet[1] = enable;
	ret = rpi_firmware_property(rpi_clk->firmware_node,
				    RPI_FIRMWARE_SET_CLOCK_STATE,
				    &packet, sizeof(packet));
	if (ret || (packet[1] & (1 << 1))) {
		dev_err(rpi_clk->dev, "Failed to set clock state\n");
		return -EINVAL;
	}
	dev_err(rpi_clk->dev, "Checking...\n");
	rpi_clk_is_on(&rpi_clk->hw);

	return 0;
}

static int rpi_clk_on(struct clk_hw *hw)
{
	struct rpi_firmware_clock *rpi_clk =
		container_of(hw, struct rpi_firmware_clock, hw);

	return rpi_clk_set_enable(rpi_clk, true);
}

static void rpi_clk_off(struct clk_hw *hw)
{
	struct rpi_firmware_clock *rpi_clk =
		container_of(hw, struct rpi_firmware_clock, hw);

	rpi_clk_set_enable(rpi_clk, false);
}

static unsigned long rpi_clk_get_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct rpi_firmware_clock *rpi_clk =
		container_of(hw, struct rpi_firmware_clock, hw);
	u32 packet[2];
	int ret;

	packet[0] = rpi_clk->clock_id;
	packet[1] = 0;
	ret = rpi_firmware_property(rpi_clk->firmware_node,
				    RPI_FIRMWARE_GET_CLOCK_RATE,
				    &packet, sizeof(packet));
	if (ret) {
		dev_err(rpi_clk->dev, "Failed to get clock rate\n");
		return 0;
	}

	dev_err(rpi_clk->dev, "%s rate: %d\n", rpi_clk->name, packet[1]);

	return packet[1];
}

static int rpi_clk_set_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long parent_rate)
{
	struct rpi_firmware_clock *rpi_clk =
		container_of(hw, struct rpi_firmware_clock, hw);
	u32 packet[2];
	int ret;

	packet[0] = rpi_clk->clock_id;
	packet[1] = rate;
	ret = rpi_firmware_property(rpi_clk->firmware_node,
				    RPI_FIRMWARE_SET_CLOCK_RATE,
				    &packet, sizeof(packet));
	if (ret) {
		dev_err(rpi_clk->dev, "Failed to set clock rate\n");
		return ret;
	}

	/*
	 * The firmware will have adjusted our requested rate and
	 * qreturned it in packet[1].  The clk core code will call
	 * rpi_clk_get_rate() to get the adjusted rate.
	 */
	dev_err(rpi_clk->dev, "Set %s clock rate to %d\n", rpi_clk->name, packet[1]);

	return 0;
}

static long rpi_clk_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	/*
	 * The firmware will end up rounding our rate to something,
	 * but we don't have an interface for it.  Just return the
	 * requested value, and it'll get updated after the clock gets
	 * set.
	 */
	return rate;
}

static struct clk_ops rpi_clk_ops = {
	.is_prepared = rpi_clk_is_on,
	.prepare = rpi_clk_on,
	.unprepare = rpi_clk_off,
	.recalc_rate = rpi_clk_get_rate,
	.set_rate = rpi_clk_set_rate,
	.round_rate = rpi_clk_round_rate,
};

DEFINE_MUTEX(delayed_clock_init);
static struct clk *rpi_firmware_delayed_get_clk(struct of_phandle_args *clkspec,
						void *_data)
{
	struct device_node *of_node = _data;
	struct platform_device *pdev = of_find_device_by_node(of_node);
	struct device *dev = &pdev->dev;
	struct device_node *firmware_node;
	struct platform_device *firmware_pdev;
	struct clk_init_data init;
	struct rpi_firmware_clock *rpi_clk;
	struct clk *ret;

	if (clkspec->args_count != 1) {
		dev_err(dev, "clock phandle should have 1 argument\n");
		return ERR_PTR(-ENODEV);
	}

	if (clkspec->args[0] >= ARRAY_SIZE(rpi_clocks)) {
		dev_err(dev, "clock phandle index %d too large\n",
			clkspec->args[0]);
		return ERR_PTR(-ENODEV);
	}

	rpi_clk = &rpi_clocks[clkspec->args[0]];
	if (rpi_clk->hw.clk)
		return rpi_clk->hw.clk;

	firmware_node = of_parse_phandle(of_node, "firmware", 0);
	if (!firmware_node) {
		dev_err(dev, "%s: Missing firmware node\n", rpi_clk->name);
		return ERR_PTR(-ENODEV);
	}

	firmware_pdev = of_find_device_by_node(firmware_node);
	if (!platform_get_drvdata(firmware_pdev)) {
		dev_err(dev, "%s: PROBE DELAYING\n", rpi_clk->name);
		return ERR_PTR(-EPROBE_DEFER);
	}

	mutex_lock(&delayed_clock_init);
	if (rpi_clk->hw.clk) {
		mutex_unlock(&delayed_clock_init);
		return rpi_clk->hw.clk;
	}
	memset(&init, 0, sizeof(init));
	init.ops = &rpi_clk_ops;

	rpi_clk->firmware_node = firmware_node;
	rpi_clk->dev = dev;
	rpi_clk->hw.init = &init;
	init.name = rpi_clk->name;
	init.flags = rpi_clk->flags;

	ret = clk_register(dev, &rpi_clk->hw);
	mutex_unlock(&delayed_clock_init);
	if (!IS_ERR(ret))
		dev_info(dev, "clock %s registered\n", rpi_clk->name);
	else {
		dev_err(dev, "clock %s failed to init: %ld\n", rpi_clk->name,
			PTR_ERR(ret));
	}
	return ret;
}

void __init rpi_firmware_init_clock_provider(struct device_node *node)
{
	/* We delay construction of our struct clks until get time,
	 * because we need to be able to return -EPROBE_DEFER if the
	 * firmware driver isn't up yet.  clk core doesn't support
	 * re-probing on -EPROBE_DEFER, but callers of clk_get can.
	 */
	of_clk_add_provider(node, rpi_firmware_delayed_get_clk, node);
}

CLK_OF_DECLARE(rpi_firmware_clocks, "raspberrypi,firmware-clocks",
	       rpi_firmware_init_clock_provider);
