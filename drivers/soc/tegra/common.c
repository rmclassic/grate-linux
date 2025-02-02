// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 */

#define dev_fmt(fmt)	"tegra-soc: " fmt

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/sysfs.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

struct kobject *tegra_soc_kobj;

static const struct of_device_id tegra_machine_match[] = {
	{ .compatible = "nvidia,tegra20", },
	{ .compatible = "nvidia,tegra30", },
	{ .compatible = "nvidia,tegra114", },
	{ .compatible = "nvidia,tegra124", },
	{ .compatible = "nvidia,tegra132", },
	{ .compatible = "nvidia,tegra210", },
	{ }
};

static int __init tegra_soc_sysfs_init(void)
{
	if (!soc_is_tegra())
		return 0;

	tegra_soc_kobj = kobject_create_and_add("tegra", NULL);
	WARN_ON(!tegra_soc_kobj);

	return 0;
}
arch_initcall(tegra_soc_sysfs_init);

bool soc_is_tegra(void)
{
	const struct of_device_id *match;
	struct device_node *root;

	root = of_find_node_by_path("/");
	if (!root)
		return false;

	match = of_match_node(tegra_machine_match, root);
	of_node_put(root);

	return match != NULL;
}

/**
 * devm_tegra_core_dev_init_opp_table() - initialize OPP table
 * @dev: device for which OPP table is initialized
 * @params: pointer to the OPP table configuration
 *
 * This function will initialize OPP table and sync OPP state of a Tegra SoC
 * core device.
 *
 * Return: 0 on success or errorno.
 */
int devm_tegra_core_dev_init_opp_table(struct device *dev,
				       struct tegra_core_opp_params *params)
{
	u32 hw_version;
	int err;

	err = devm_pm_opp_set_clkname(dev, NULL);
	if (err) {
		dev_err(dev, "failed to set OPP clk: %d\n", err);
		return err;
	}

	/* Tegra114+ doesn't support OPP yet */
	if (!of_machine_is_compatible("nvidia,tegra20") &&
	    !of_machine_is_compatible("nvidia,tegra30"))
		return -ENODEV;

	if (of_machine_is_compatible("nvidia,tegra20"))
		hw_version = BIT(tegra_sku_info.soc_process_id);
	else
		hw_version = BIT(tegra_sku_info.soc_speedo_id);

	err = devm_pm_opp_set_supported_hw(dev, &hw_version, 1);
	if (err) {
		dev_err(dev, "failed to set OPP supported HW: %d\n", err);
		return err;
	}

	/*
	 * Older device-trees have an empty OPP table, we will get
	 * -ENODEV from devm_pm_opp_of_add_table() in this case.
	 */
	err = devm_pm_opp_of_add_table(dev);
	if (err) {
		if (err != -ENODEV)
			dev_err(dev, "failed to add OPP table: %d\n", err);

		return err;
	}

	if (params->init_state) {
		err = dev_pm_opp_sync(dev);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(devm_tegra_core_dev_init_opp_table);
