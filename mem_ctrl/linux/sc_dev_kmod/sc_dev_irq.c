// sc_dev_irq.c
/*
	matches compatible = "my,sc-dev"
	maps MMIO from device tree
	requests two IRQs (DMA + CTRL) from kernel
	enables both device-side IRQs (REG_IRQ_ENABLE)
	after receiving interrupt, clears in ISR (REG_IRQ_CLEAR)

	exposes two blocking char devices: /dev/sc_dev_dma and /dev/sc_dev_ctrl

	read() blocks until an IRQ occurs, then returns a 32-bit count:
	- /dev/sc_dev_dma  returns number of DMA_DONE interrupts since last read()
	- /dev/sc_dev_ctrl returns number of CTRL_DONE interrupts since last read()

	Notes:
	- Uses "counting" semantics: no interrupts are lost as long as the counter
	  does not overflow (32-bit wrap is possible if userspace never reads).
	- If read() is interrupted by a signal, it returns -ERESTARTSYS.
*/

// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>

#define REG_IRQ_STATUS  0x60
#define REG_IRQ_CLEAR   0x64
#define REG_IRQ_ENABLE  0x68

#define IRQ_CTRL_DONE  (1u << 0)
#define IRQ_DMA_DONE   (1u << 1)
#define IRQ_JOB_DONE   (1u << 2)

struct sc_dev {
	void __iomem *base;
	int irq_dma;
	int irq_ctrl;
	int irq_job;

	/* One wait queue per "channel" (/dev node). */
	wait_queue_head_t dma_wq, ctrl_wq, job_wq;

	/*
	 * Counting semantics:
	 * the ISR increments these counters; read() returns-and-clears them.
	 * Is protected by 'lock' (ISR and process context can acess.
	 */
	u32 dma_count;
	u32 ctrl_count;
	u32 job_count;

	spinlock_t lock;

	/* Two miscdevices for two separate /dev nodes. */
	struct miscdevice misc_dma;
	struct miscdevice misc_ctrl;
	struct miscdevice misc_job;
};

/*
 * Interrupt service routine (used for both DMA and CTRL IRQ lines).
 *
 * Important:
 * - This handler must not sleep.
 * - Clear device-side sources first so a level-triggered interrupt deasserts.
 * - Then, update software counters and wake any blocking readers.
 */
static irqreturn_t sc_dev_isr(int irq, void *data)
{
	struct sc_dev *dev = data;
	u32 status;
	unsigned long flags;

	/* Read device interrupt status. */
	status = readl(dev->base + REG_IRQ_STATUS);
	if (!status)
		return IRQ_NONE;

	/* Clear/acknowledge the device-side sources. */
	writel(status, dev->base + REG_IRQ_CLEAR);

	/* Update counters atomically with respect to readers. */
	spin_lock_irqsave(&dev->lock, flags);
		if (status & IRQ_DMA_DONE) dev->dma_count++;
		if (status & IRQ_CTRL_DONE) dev->ctrl_count++;
		if (status & IRQ_JOB_DONE) dev->job_count++;
	spin_unlock_irqrestore(&dev->lock, flags);

	/* Wake any readers waiting in read(). */
	if (status & IRQ_DMA_DONE) wake_up_interruptible(&dev->dma_wq);
	if (status & IRQ_CTRL_DONE) wake_up_interruptible(&dev->ctrl_wq);
	if (status & IRQ_JOB_DONE) wake_up_interruptible(&dev->job_wq);

	return IRQ_HANDLED;
}

/*
 * Helper: miscdevice sets file->private_data to (struct miscdevice *).
 * We store our sc_dev pointer as drvdata on the parent device (pdev->dev),
 * and miscdevice->parent points at that device, so we can recover sc_dev here.
 */
static struct sc_dev *sc_dev_from_file(struct file *f)
{
	struct miscdevice *m = f->private_data;
	return dev_get_drvdata(m->parent);
}

/*
 * /dev/sc_dev_dma: 
 * blocking read() returns a u32 count of DMA_DONE interrupts
 * that occurred since the last successful read() on this device node.
 */
static ssize_t sc_dev_dma_read(struct file *f, char __user *buf,
			       size_t len, loff_t *ppos)
{
	struct sc_dev *dev = sc_dev_from_file(f);
	unsigned long flags;
	u32 count;

	if (len < sizeof(count))
		return -EINVAL;

	/*
	 * Block until at least one DMA interrupt has been observed.
	 * The condition is re-checked after every wakeup.
	 */
	if (wait_event_interruptible(dev->dma_wq, READ_ONCE(dev->dma_count) != 0))
		return -ERESTARTSYS;

	/*
	 * Return-and-clear the accumulated count atomically.
	 * This guarantees counting semantics: no events are dropped between wake and clear.
	 */
	spin_lock_irqsave(&dev->lock, flags);
		count = dev->dma_count;
		dev->dma_count = 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (copy_to_user(buf, &count, sizeof(count)))
		return -EFAULT;

	return sizeof(count);
}

/*
 * /dev/sc_dev_ctrl: 
 * blocking read() returns a u32 count of CTRL_DONE interrupts
 * that occurred since the last successful read() on this device node.
 */
static ssize_t sc_dev_ctrl_read(struct file *f, char __user *buf,
				size_t len, loff_t *ppos)
{
	struct sc_dev *dev = sc_dev_from_file(f);
	unsigned long flags;
	u32 count;

	if (len < sizeof(count))
		return -EINVAL;

	/*
	 * Block until at least one CTRL interrupt has been observed.
	 * The condition is re-checked after every wakeup.
	 */
	if (wait_event_interruptible(dev->ctrl_wq, READ_ONCE(dev->ctrl_count) != 0))
		return -ERESTARTSYS;

	/*
	 * Return-and-clear the accumulated count atomically.
	 * This guarantees counting semantics: no events are dropped between wake and clear.
	 */
	spin_lock_irqsave(&dev->lock, flags);
		count = dev->ctrl_count;
		dev->ctrl_count = 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (copy_to_user(buf, &count, sizeof(count)))
		return -EFAULT;

	return sizeof(count);
}

/*
 * /dev/sc_dev_job: 
 * blocking read() returns a u32 count of JOB_DONE interrupts
 * that occurred since the last successful read() on this device node.
 */
static ssize_t sc_dev_job_read(struct file *f, char __user *buf,
				size_t len, loff_t *ppos)
{
	struct sc_dev *dev = sc_dev_from_file(f);
	unsigned long flags;
	u32 count;

	if (len < sizeof(count))
		return -EINVAL;

	/*
	 * Block until at least one JOB interrupt has been observed.
	 * The condition is re-checked after every wakeup.
	 */
	if (wait_event_interruptible(dev->job_wq, READ_ONCE(dev->job_count) != 0))
		return -ERESTARTSYS;

	/*
	 * Return-and-clear the accumulated count atomically.
	 * This guarantees counting semantics: no events are dropped between wake and clear.
	 */
	spin_lock_irqsave(&dev->lock, flags);
		count = dev->job_count;
		dev->job_count = 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (copy_to_user(buf, &count, sizeof(count)))
		return -EFAULT;

	return sizeof(count);
}

/* No .poll implementation is provided, userspace should block in read() */
static const struct file_operations sc_dev_dma_fops = {
	.owner  = THIS_MODULE,
	.read   = sc_dev_dma_read,
	.llseek = noop_llseek,
};

static const struct file_operations sc_dev_ctrl_fops = {
	.owner  = THIS_MODULE,
	.read   = sc_dev_ctrl_read,
	.llseek = noop_llseek,
};

static const struct file_operations sc_dev_job_fops = {
	.owner  = THIS_MODULE,
	.read   = sc_dev_job_read,
	.llseek = noop_llseek,
};


/* ---------- probe/remove ---------- */

static int sc_dev_probe(struct platform_device *pdev)
{
	struct sc_dev *dev;
	struct resource *res;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->irq_dma = platform_get_irq(pdev, 0);
	if (dev->irq_dma < 0)
		return dev->irq_dma;

	dev->irq_ctrl = platform_get_irq(pdev, 1);
	if (dev->irq_ctrl < 0)
		return dev->irq_ctrl;

	dev->irq_job = platform_get_irq(pdev, 2);
	if (dev->irq_job < 0)
		return dev->irq_ctrl;

	init_waitqueue_head(&dev->dma_wq);
	init_waitqueue_head(&dev->ctrl_wq);
	init_waitqueue_head(&dev->job_wq);
	spin_lock_init(&dev->lock);

	dev->dma_count = 0;
	dev->ctrl_count = 0;
	dev->job_count = 0;

	/* Make sc_dev retrievable from miscdevice->parent via dev_get_drvdata(). */
	platform_set_drvdata(pdev, dev);

	/* Create /dev/sc_dev_dma */
	dev->misc_dma.minor  = MISC_DYNAMIC_MINOR;
	dev->misc_dma.name   = "sc_dev_dma";
	dev->misc_dma.fops   = &sc_dev_dma_fops;
	dev->misc_dma.parent = &pdev->dev;

	ret = misc_register(&dev->misc_dma);
	if (ret)
		return ret;

	/* Create /dev/sc_dev_ctrl */
	dev->misc_ctrl.minor  = MISC_DYNAMIC_MINOR;
	dev->misc_ctrl.name   = "sc_dev_ctrl";
	dev->misc_ctrl.fops   = &sc_dev_ctrl_fops;
	dev->misc_ctrl.parent = &pdev->dev;

	ret = misc_register(&dev->misc_ctrl);
	if (ret) {
		misc_deregister(&dev->misc_dma);
		return ret;
	}

	/* Create /dev/sc_dev_job */
	dev->misc_job.minor  = MISC_DYNAMIC_MINOR;
	dev->misc_job.name   = "sc_dev_job";
	dev->misc_job.fops   = &sc_dev_job_fops;
	dev->misc_job.parent = &pdev->dev;

	ret = misc_register(&dev->misc_job);
	if (ret) {
		misc_deregister(&dev->misc_dma);
		misc_deregister(&dev->misc_ctrl);
		return ret;
	}

	/* Request IRQ lines. */
	ret = devm_request_irq(&pdev->dev, dev->irq_dma, sc_dev_isr, 0,
			       "sc_dev-dma", dev);
	if (ret)
		goto err_misc;

	ret = devm_request_irq(&pdev->dev, dev->irq_ctrl, sc_dev_isr, 0,
			       "sc_dev-ctrl", dev);
	if (ret)
		goto err_misc;

	ret = devm_request_irq(&pdev->dev, dev->irq_job, sc_dev_isr, 0,
			       "sc_dev-job", dev);
	if (ret)
		goto err_misc;

	/* Enable device-side IRQ generation. */
	writel(IRQ_CTRL_DONE | IRQ_DMA_DONE | IRQ_JOB_DONE, dev->base + REG_IRQ_ENABLE);

	dev_info(&pdev->dev, "probed, /dev/%s, /dev/%s, and /dev/%s ready\n",
		 dev->misc_dma.name, dev->misc_ctrl.name, dev->misc_job.name);
	return 0;

err_misc:
	misc_deregister(&dev->misc_job);
	misc_deregister(&dev->misc_ctrl);
	misc_deregister(&dev->misc_dma);
	return ret;
}

static void sc_dev_remove(struct platform_device *pdev)
{
	struct sc_dev *dev = platform_get_drvdata(pdev);

	/* Disable device-side IRQ generation early during removal. */
	if (dev && dev->base)
		writel(0, dev->base + REG_IRQ_ENABLE);

	if (dev) {
		misc_deregister(&dev->misc_job);
		misc_deregister(&dev->misc_ctrl);
		misc_deregister(&dev->misc_dma);
	}
}

static const struct of_device_id sc_dev_of_match[] = {
	{ .compatible = "my,sc-dev" },
	{}
};
MODULE_DEVICE_TABLE(of, sc_dev_of_match);

static struct platform_driver sc_dev_driver = {
	.probe  = sc_dev_probe,
	.remove = sc_dev_remove,
	.driver = {
		.name = "sc_dev",
		.of_match_table = sc_dev_of_match,
	},
};
module_platform_driver(sc_dev_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sc_dev IRQ driver: /dev/sc_dev_dma, /dev/sc_dev_ctrl, and /dev/sc_dev_job (counting, blocking read)");