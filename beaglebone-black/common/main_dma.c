#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/timekeeping.h>

//device tree support
#include <linux/of.h>
#include "generic.h"
#include "config.h"
#include "drvr.h"
#include "logi_dma.h"
#include "ioctl.h"


static int dm_open(struct inode *inode, struct file *filp);
static int dm_release(struct inode *inode, struct file *filp);
static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);

static struct i2c_board_info io_exp_info= {
	I2C_BOARD_INFO("fpga_ctrl", I2C_IO_EXP_ADDR),
};

static struct file_operations dm_ops = {
	.read = dm_read,
	.write = dm_write,
	.compat_ioctl = dm_ioctl,
	.unlocked_ioctl = dm_ioctl,
	.open = dm_open,
	.release = dm_release,
};

static dma_addr_t dmaphysbuf;
static unsigned char gDrvrMajor;
static struct device *prog_device;
static struct device *dma_parent_dev;
static struct class *drvr_class;
static struct drvr_device *drvr_devices;

#ifdef PROFILE

static struct timespec64 start_ts, end_ts;

static inline void start_profile(void)
{
	ktime_get_real_ts64(&start_ts);
}

static inline void stop_profile(void)
{
	ktime_get_real_ts64(&end_ts);
}

static inline void compute_bandwidth(const unsigned int nb_byte)
{
	struct timespec64 dt = timespec64_sub(end_ts, start_ts);
	long elapsed_u_time = dt.tv_sec * 1000000 + dt.tv_nsec / 1000;

	DBG_LOG("Time=%ld us\n", elapsed_u_time);
	DBG_LOG("Bandwidth=%d kBytes/s\n", 1000000 * (nb_byte >> 10) / elapsed_u_time);
}

#endif


static inline ssize_t writeMem(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	unsigned long src_addr, trgt_addr;
	int result;
	struct drvr_mem *mem_to_write = &(((struct drvr_device *) filp->private_data)->data.mem);

#ifdef USE_WORD_ADDRESSING
	if (count % 2 != 0) {
		DBG_LOG("write: Transfer must be 16bits aligned\n");

		return -EFAULT;
	}

	trgt_addr = (unsigned long) &(mem_to_write->base_addr[(*f_pos) / 2]);
#else
	trgt_addr = (unsigned long) &(mem_to_write->base_addr[(*f_pos)]);
#endif

	src_addr = (unsigned long) dmaphysbuf;

	if (count < MAX_DMA_TRANSFER_IN_BYTES) {
#ifdef PROFILE
		DBG_LOG("Write\n");
		start_profile();
#endif

		if (copy_from_user(mem_to_write->dma.buf, buf, count)) {
			return -EFAULT;
		}

		result = logi_dma_copy(mem_to_write, trgt_addr, src_addr, count);

		if (result < 0) {
			DBG_LOG("write: Failed to trigger DMA transfer\n");

			return result;
		}

#ifdef PROFILE
		stop_profile();
		compute_bandwidth(count);
#endif

		return count;
	} else {
		ssize_t transferred = 0;
		unsigned short int transfer_size;

		transfer_size = MAX_DMA_TRANSFER_IN_BYTES;

		if (copy_from_user(mem_to_write->dma.buf, buf, transfer_size)) {
			return -EFAULT;
		}

		while (transferred < count) {
#ifdef PROFILE
			DBG_LOG("Write\n");
			start_profile();
#endif

			result = logi_dma_copy(mem_to_write, trgt_addr, src_addr, transfer_size);

			if (result < 0) {
				DBG_LOG("write: Failed to trigger DMA transfer\n");

				return result;
			}

			trgt_addr += transfer_size;
			transferred += transfer_size;

			if ((count - transferred) < MAX_DMA_TRANSFER_IN_BYTES) {
				transfer_size = count - transferred;
			} else {
				transfer_size = MAX_DMA_TRANSFER_IN_BYTES;
			}

			if (copy_from_user(mem_to_write->dma.buf, &buf[transferred], transfer_size)) {
				return -EFAULT;
			}

#ifdef PROFILE
			stop_profile();
			compute_bandwidth(transfer_size);
#endif
		}

		return transferred;
	}
}

static inline ssize_t readMem(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	unsigned long src_addr, trgt_addr;
	int result;
	struct drvr_mem *mem_to_read = &(((struct drvr_device *) filp->private_data)->data.mem);

#ifdef USE_WORD_ADDRESSING
	if (count % 2 != 0) {
		DBG_LOG("read: Transfer must be 16bits aligned\n");

		return -EFAULT;
	}

	src_addr = (unsigned long) &(mem_to_read->base_addr[(*f_pos) / 2]);
#else
	src_addr = (unsigned long) &(mem_to_read->base_addr[(*f_pos)]);
#endif

	trgt_addr = (unsigned long) dmaphysbuf;

	if (count < MAX_DMA_TRANSFER_IN_BYTES) {

#ifdef PROFILE
		DBG_LOG("Read\n");
		start_profile();
#endif

		result = logi_dma_copy(mem_to_read, trgt_addr, src_addr, count);

		if (result < 0) {
			DBG_LOG("read: Failed to trigger DMA transfer\n");

			return result;
		}

		if (copy_to_user(buf, mem_to_read->dma.buf, count)) {
			return -EFAULT;
		}

#ifdef PROFILE
		stop_profile();
		compute_bandwidth(count);
#endif

		return count;
	} else {
		ssize_t transferred = 0;
		unsigned short int transfer_size;

		transfer_size = MAX_DMA_TRANSFER_IN_BYTES;

		while (transferred < count) {

#ifdef PROFILE
			DBG_LOG("Read\n");
			start_profile();
#endif

			result = logi_dma_copy(mem_to_read, trgt_addr, src_addr, transfer_size);

			if (result < 0) {
				DBG_LOG("read: Failed to trigger DMA transfer\n");

				return result;
			}

			if (copy_to_user(&buf[transferred], mem_to_read->dma.buf, transfer_size)) {
				return -EFAULT;
			}

#ifdef PROFILE
			stop_profile();
			compute_bandwidth(transfer_size);
#endif

			src_addr += transfer_size;
			transferred += transfer_size;

			if ((count - transferred) < MAX_DMA_TRANSFER_IN_BYTES) {
				transfer_size = (count - transferred);
			} else {
				transfer_size = MAX_DMA_TRANSFER_IN_BYTES;
			}
		}

		return transferred;
	}
}

static int dm_open(struct inode *inode, struct file *filp)
{
	struct drvr_device* dev = container_of(inode->i_cdev, struct drvr_device, cdev);

	filp->private_data = dev;

	if (dev == NULL) {
		DBG_LOG("Failed to retrieve driver structure!\n");

		return -ENODEV;
	}

	if (dev->opened != 1) {
		if (dev->type != prog) {
			struct drvr_mem* mem_dev = &((dev->data).mem);
			int result;

			if (request_mem_region((unsigned long) mem_dev->base_addr, FPGA_MEM_SIZE, DEVICE_NAME) == NULL) {
				DBG_LOG("Failed to request I/O memory region\n");

				return -ENOMEM;
			}

			mem_dev->virt_addr = ioremap(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);

			if (mem_dev->virt_addr == NULL) {
				DBG_LOG("Failed to remap I/O memory\n");
				release_mem_region(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);

				return -ENOMEM;
			}

			result = logi_dma_open(dma_parent_dev, mem_dev, &dmaphysbuf);

			if (result != 0) {
				iounmap(mem_dev->virt_addr);
				release_mem_region(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);
				return result;
			}

			DBG_LOG("mem interface opened\n");
		}

		dev->opened = 1;
	}

	return 0;
}

static int dm_release(struct inode *inode, struct file *filp)
{
	struct drvr_device* dev = container_of(inode->i_cdev, struct drvr_device, cdev);
	struct drvr_mem* mem_dev = &((dev->data).mem);

	if (dev->opened != 0) {
		if (dev->type == mem) {
			iounmap(mem_dev->virt_addr);
			release_mem_region(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);
			logi_dma_release(dma_parent_dev, mem_dev);
			DBG_LOG("module released\n");
		}

		dev->opened = 0;
	}

	return 0;
}

static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_device *dev = filp->private_data;

	switch (dev->type) {
		case prog:
			return loadBitFile((dev->data.prog.i2c_io), buf, count);

		case mem:
			return writeMem(filp, buf, count, f_pos);

		default:
			return loadBitFile((dev->data.prog.i2c_io), buf, count);
	}
}

static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_device *dev = filp->private_data;

	switch (dev->type) {
		case prog:
			return -EPERM;

		case mem:
			return readMem(filp, buf, count, f_pos);

		default:
			return -EPERM;
	}
}

static int logibone_probe(struct platform_device *pdev)
{
	int result;
	int devno;
	struct drvr_mem *memDev;
	struct drvr_prog *progDev;
	struct i2c_adapter *i2c_adap;
	dev_t dev = 0;

	dma_parent_dev = &pdev->dev;

	result = alloc_chrdev_region(&dev, 0, 2, DEVICE_NAME);
	if (result < 0) {
		DBG_LOG("Registering char device failed with %d\n", result);
		return result;
	}

	gDrvrMajor = MAJOR(dev);
	drvr_devices = kcalloc(2, sizeof(struct drvr_device), GFP_KERNEL);
	if (!drvr_devices) {
		result = -ENOMEM;
		goto err_chrdev;
	}

	drvr_class = class_create(DEVICE_NAME);
	if (IS_ERR(drvr_class)) {
		result = PTR_ERR(drvr_class);
		drvr_class = NULL;
		goto err_devices;
	}

	devno = MKDEV(gDrvrMajor, 0);
	drvr_devices[0].type = prog;
	progDev = &(drvr_devices[0].data.prog);
	prog_device = device_create(drvr_class, NULL, devno, NULL, DEVICE_NAME);
	if (IS_ERR(prog_device)) {
		result = PTR_ERR(prog_device);
		prog_device = NULL;
		goto err_class;
	}
	drvr_devices[0].opened = 0;

	i2c_adap = i2c_get_adapter(I2C_ADAPTER);
	if (i2c_adap == NULL) {
		DBG_LOG("Cannot get I2C adapter %i\n", I2C_ADAPTER);
		result = -ENODEV;
		goto err_prog_device;
	}

	progDev->i2c_io = i2c_new_client_device(i2c_adap, &io_exp_info);
	i2c_put_adapter(i2c_adap);
	if (IS_ERR(progDev->i2c_io)) {
		result = PTR_ERR(progDev->i2c_io);
		goto err_prog_device;
	}

	cdev_init(&(drvr_devices[0].cdev), &dm_ops);
	drvr_devices[0].cdev.owner = THIS_MODULE;
	drvr_devices[0].cdev.ops = &dm_ops;
	result = cdev_add(&(drvr_devices[0].cdev), devno, 1);
	if (result < 0) {
		goto err_i2c;
	}

	devno = MKDEV(gDrvrMajor, 1);
	drvr_devices[1].type = mem;
	memDev = &(drvr_devices[1].data.mem);
	memDev->base_addr = (unsigned short *) (FPGA_BASE_ADDR);
	if (IS_ERR(device_create(drvr_class, prog_device, devno, NULL, DEVICE_NAME_MEM))) {
		result = -ENODEV;
		goto err_cdev0;
	}
	cdev_init(&(drvr_devices[1].cdev), &dm_ops);
	(drvr_devices[1].cdev).owner = THIS_MODULE;
	(drvr_devices[1].cdev).ops = &dm_ops;
	result = cdev_add(&(drvr_devices[1].cdev), devno, 1);
	if (result < 0) {
		goto err_mem_device;
	}
	
	drvr_devices[1].opened = 0;
	logi_dma_init();

	result = ioctl_init();
	if (result < 0) {
		goto err_cdev1;
	}

	platform_set_drvdata(pdev, drvr_devices);
	return 0;

err_cdev1:
	cdev_del(&(drvr_devices[1].cdev));
err_mem_device:
	device_destroy(drvr_class, MKDEV(gDrvrMajor, 1));
err_cdev0:
	cdev_del(&(drvr_devices[0].cdev));
err_i2c:
	i2c_unregister_device(drvr_devices[0].data.prog.i2c_io);
err_prog_device:
	device_destroy(drvr_class, MKDEV(gDrvrMajor, 0));
err_class:
	class_destroy(drvr_class);
	drvr_class = NULL;
err_devices:
	kfree(drvr_devices);
	drvr_devices = NULL;
err_chrdev:
	unregister_chrdev_region(MKDEV(gDrvrMajor, 0), 2);
	return result;
}

static void logibone_remove(struct platform_device *pdev)
{
	if (drvr_devices) {
		i2c_unregister_device(drvr_devices[0].data.prog.i2c_io);
		device_destroy(drvr_class, MKDEV(gDrvrMajor, 1));
		device_destroy(drvr_class, MKDEV(gDrvrMajor, 0));
		cdev_del(&drvr_devices[1].cdev);
		cdev_del(&drvr_devices[0].cdev);
		kfree(drvr_devices);
		drvr_devices = NULL;
	}

	if (drvr_class) {
		class_destroy(drvr_class);
		drvr_class = NULL;
	}

	unregister_chrdev_region(MKDEV(gDrvrMajor, 0), 2);
	ioctl_exit();
	dma_parent_dev = NULL;
	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id logibone_of_match[] = {
	{ .compatible = "logibone_ra1" },
	{ },
};
MODULE_DEVICE_TABLE(of, logibone_of_match);

static struct platform_driver logibone_driver = {
	.probe = logibone_probe,
	.remove = logibone_remove,
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = logibone_of_match,
	},
};

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");
MODULE_AUTHOR("Martin Schmitt <test051102@hotmail.com>");
MODULE_DESCRIPTION("Logibone R1 DMA platform driver");

module_platform_driver(logibone_driver);
