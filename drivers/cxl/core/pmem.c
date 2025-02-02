// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */

#include <linux/device.h>
#include <linux/slab.h>
#include <cxlmem.h>
#include <cxl.h>

#include "core.h"

static void cxl_nvdimm_bridge_release(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	kfree(cxl_nvb);
}

static const struct attribute_group *cxl_nvdimm_bridge_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

const struct device_type cxl_nvdimm_bridge_type = {
	.name = "cxl_nvdimm_bridge",
	.release = cxl_nvdimm_bridge_release,
	.groups = cxl_nvdimm_bridge_attribute_groups,
};

struct cxl_nvdimm_bridge *to_cxl_nvdimm_bridge(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_nvdimm_bridge_type,
			  "not a cxl_nvdimm_bridge device\n"))
		return NULL;
	return container_of(dev, struct cxl_nvdimm_bridge, dev);
}
EXPORT_SYMBOL_GPL(to_cxl_nvdimm_bridge);

static struct cxl_nvdimm_bridge *
cxl_nvdimm_bridge_alloc(struct cxl_port *port)
{
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *dev;

	cxl_nvb = kzalloc(sizeof(*cxl_nvb), GFP_KERNEL);
	if (!cxl_nvb)
		return ERR_PTR(-ENOMEM);

	dev = &cxl_nvb->dev;
	cxl_nvb->port = port;
	cxl_nvb->state = CXL_NVB_NEW;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = &port->dev;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_nvdimm_bridge_type;

	return cxl_nvb;
}

static void unregister_nvb(void *_cxl_nvb)
{
	struct cxl_nvdimm_bridge *cxl_nvb = _cxl_nvb;
	bool flush;

	/*
	 * If the bridge was ever activated then there might be in-flight state
	 * work to flush. Once the state has been changed to 'dead' then no new
	 * work can be queued by user-triggered bind.
	 */
	device_lock(&cxl_nvb->dev);
	flush = cxl_nvb->state != CXL_NVB_NEW;
	cxl_nvb->state = CXL_NVB_DEAD;
	device_unlock(&cxl_nvb->dev);

	/*
	 * Even though the device core will trigger device_release_driver()
	 * before the unregister, it does not know about the fact that
	 * cxl_nvdimm_bridge_driver defers ->remove() work. So, do the driver
	 * release not and flush it before tearing down the nvdimm device
	 * hierarchy.
	 */
	device_release_driver(&cxl_nvb->dev);
	if (flush)
		flush_work(&cxl_nvb->state_work);
	device_unregister(&cxl_nvb->dev);
}

struct cxl_nvdimm_bridge *devm_cxl_add_nvdimm_bridge(struct device *host,
						     struct cxl_port *port)
{
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *dev;
	int rc;

	if (!IS_ENABLED(CONFIG_CXL_PMEM))
		return ERR_PTR(-ENXIO);

	cxl_nvb = cxl_nvdimm_bridge_alloc(port);
	if (IS_ERR(cxl_nvb))
		return cxl_nvb;

	dev = &cxl_nvb->dev;
	rc = dev_set_name(dev, "nvdimm-bridge");
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, unregister_nvb, cxl_nvb);
	if (rc)
		return ERR_PTR(rc);

	return cxl_nvb;

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_cxl_add_nvdimm_bridge);

static void cxl_nvdimm_release(struct device *dev)
{
	struct cxl_nvdimm *cxl_nvd = to_cxl_nvdimm(dev);

	kfree(cxl_nvd);
}

static const struct attribute_group *cxl_nvdimm_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

const struct device_type cxl_nvdimm_type = {
	.name = "cxl_nvdimm",
	.release = cxl_nvdimm_release,
	.groups = cxl_nvdimm_attribute_groups,
};

bool is_cxl_nvdimm(struct device *dev)
{
	return dev->type == &cxl_nvdimm_type;
}
EXPORT_SYMBOL_GPL(is_cxl_nvdimm);

struct cxl_nvdimm *to_cxl_nvdimm(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_cxl_nvdimm(dev),
			  "not a cxl_nvdimm device\n"))
		return NULL;
	return container_of(dev, struct cxl_nvdimm, dev);
}
EXPORT_SYMBOL_GPL(to_cxl_nvdimm);

static struct cxl_nvdimm *cxl_nvdimm_alloc(struct cxl_memdev *cxlmd)
{
	struct cxl_nvdimm *cxl_nvd;
	struct device *dev;

	cxl_nvd = kzalloc(sizeof(*cxl_nvd), GFP_KERNEL);
	if (!cxl_nvd)
		return ERR_PTR(-ENOMEM);

	dev = &cxl_nvd->dev;
	cxl_nvd->cxlmd = cxlmd;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = &cxlmd->dev;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_nvdimm_type;

	return cxl_nvd;
}

int devm_cxl_add_nvdimm(struct device *host, struct cxl_memdev *cxlmd)
{
	struct cxl_nvdimm *cxl_nvd;
	struct device *dev;
	int rc;

	cxl_nvd = cxl_nvdimm_alloc(cxlmd);
	if (IS_ERR(cxl_nvd))
		return PTR_ERR(cxl_nvd);

	dev = &cxl_nvd->dev;
	rc = dev_set_name(dev, "pmem%d", cxlmd->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	dev_dbg(host, "%s: register %s\n", dev_name(dev->parent),
		dev_name(dev));

	return devm_add_action_or_reset(host, unregister_cxl_dev, dev);

err:
	put_device(dev);
	return rc;
}
EXPORT_SYMBOL_GPL(devm_cxl_add_nvdimm);
