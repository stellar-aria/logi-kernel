#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <asm/io.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/property.h>
#include "generic.h"
#include "config.h"
#include "../common/drvr.h"


//SSI
#define SSI_CLK 110
#define SSI_DATA 112
#define SSI_DONE 3
#define SSI_PROG 5
#define SSI_INIT 2
#define MODE0 0
#define MODE1 1
#define SSI_DELAY 1

//GPIO
#define GPIO3_BASE 0x481AE000
#define GPIO3_SETDATAOUT *(gpio_regs+1)
#define GPIO3_CLEARDATAOUT *(gpio_regs)

//I2C
#define I2C_IO_EXP_CONFIG_REG 0x03
#define I2C_IO_EXP_IN_REG 0x00
#define I2C_IO_EXP_OUT_REG 0x01


volatile unsigned * gpio_regs;
static struct gpio_desc *ssi_clk_desc;
static struct gpio_desc *ssi_data_desc;

static int acquire_ssi_gpios(void)
{
	struct device_node *np;
	struct fwnode_handle *fwnode;

	np = of_find_compatible_node(NULL, NULL, "logibone_ra1");
	if (!np) {
		DBG_LOG("Failed to find DT node for logibone_ra1\n");
		return -ENODEV;
	}

	fwnode = of_fwnode_handle(np);
	ssi_clk_desc = fwnode_gpiod_get_index(fwnode, "ssi-clk", 0,
					      GPIOD_OUT_LOW, "ssi-clk");
	if (IS_ERR(ssi_clk_desc)) {
		int err = PTR_ERR(ssi_clk_desc);
		ssi_clk_desc = NULL;
		of_node_put(np);
		return err;
	}

	ssi_data_desc = fwnode_gpiod_get_index(fwnode, "ssi-data", 0,
					       GPIOD_OUT_LOW, "ssi-data");
	of_node_put(np);
	if (IS_ERR(ssi_data_desc)) {
		int err = PTR_ERR(ssi_data_desc);
		ssi_data_desc = NULL;
		gpiod_put(ssi_clk_desc);
		ssi_clk_desc = NULL;
		return err;
	}

	return 0;
}

static void release_ssi_gpios(void)
{
	if (ssi_clk_desc) {
		gpiod_put(ssi_clk_desc);
		ssi_clk_desc = NULL;
	}

	if (ssi_data_desc) {
		gpiod_put(ssi_data_desc);
		ssi_data_desc = NULL;
	}
}


static inline void __delay_cycles(unsigned long cycles)
{
	while (cycles != 0) {
		cycles--;
	}
}

static inline void ssiSetClk(void)
{
	//gpio_set_value(SSI_CLK, 1);
	GPIO3_SETDATAOUT = (1 << 14);
}

static inline void ssiClearClk(void)
{
	//gpio_set_value(SSI_CLK, 0);
	GPIO3_CLEARDATAOUT = (1 << 14);
}

static inline void ssiSetData(void)
{
	//gpio_set_value(SSI_DATA, 1);
	GPIO3_SETDATAOUT = (1 << 16);
}

static inline void ssiClearData(void)
{
	//gpio_set_value(SSI_DATA, 0);
	GPIO3_CLEARDATAOUT = (1 << 16);
}

static inline void serialConfigWriteByte(unsigned char val)
{
	unsigned char bitCount = 0;
	unsigned char valBuf = val;

	for (bitCount = 0; bitCount < 8; bitCount++) {
		ssiClearClk();

		if ((valBuf & 0x80) != 0) {
			ssiSetData();
		} else {
			ssiClearData();
		}

		//__delay_cycles(SSI_DELAY);
		ssiSetClk();
		valBuf = (valBuf << 1);
		//__delay_cycles(SSI_DELAY);
	}
}

static inline void i2c_set_pin(struct i2c_client * io_cli, unsigned char pin, unsigned char val)
{
	unsigned char i2c_buffer[2];

	i2c_buffer[0] = I2C_IO_EXP_OUT_REG;
	i2c_master_send(io_cli, i2c_buffer, 1);
	i2c_master_recv(io_cli, &i2c_buffer[1], 1);

	if (val == 1) {
		i2c_buffer[1] |= (1 << pin);
	} else {
		i2c_buffer[1] &= ~(1 << pin);
	}

	i2c_master_send(io_cli, i2c_buffer, 2);
}

static inline unsigned char i2c_get_pin(struct i2c_client * io_cli, unsigned char pin)
{
	unsigned char i2c_buffer;

	i2c_buffer = I2C_IO_EXP_IN_REG;
	i2c_master_send(io_cli, &i2c_buffer, 1);
	i2c_master_recv(io_cli, &i2c_buffer, 1);

	return ((i2c_buffer >> pin) & 0x01);
}

int loadBitFile(struct i2c_client * io_cli, const unsigned char * bitBuffer_user, const unsigned int length)
{
	int res;
	unsigned long int i;
	unsigned long int timer = 0;
	unsigned char * bitBuffer;
	unsigned char i2c_buffer[4];

	//request_mem_region(GPIO3_BASE + 0x190, 8, gDrvrName);
	gpio_regs = ioremap(GPIO3_BASE + 0x190, 2 * sizeof(int));

	bitBuffer = kmalloc(length, GFP_KERNEL);

	if (bitBuffer == NULL) {
		DBG_LOG("Failed allocate buffer for configuration file\n");

		return -ENOMEM;
	}

	if (copy_from_user(bitBuffer, bitBuffer_user, length))
		return EFAULT;

	res = acquire_ssi_gpios();
	if (res < 0) {
		DBG_LOG("Failed to acquire GPIO descriptors from DT\n");
		kfree(bitBuffer);
		iounmap(gpio_regs);
		return res;
	}

	i2c_buffer[0] = I2C_IO_EXP_CONFIG_REG;
	i2c_buffer[1] = 0xFF;
	i2c_buffer[1] &= ~((1 << SSI_PROG) | (1 << MODE1) | (1 << MODE0));
	i2c_master_send(io_cli, i2c_buffer, 2);//set SSI_PROG, MODE0, MODE1 as output others as inputs
	i2c_set_pin(io_cli, MODE0, 1);
	i2c_set_pin(io_cli, MODE1, 1);
	i2c_set_pin(io_cli, SSI_PROG, 0);

	gpiod_direction_output(ssi_clk_desc, 0);
	gpiod_direction_output(ssi_data_desc, 0);

	gpiod_set_value(ssi_clk_desc, 0);
	i2c_set_pin(io_cli, SSI_PROG, 1);
	__delay_cycles(10 * SSI_DELAY);
	i2c_set_pin(io_cli, SSI_PROG, 0);
	__delay_cycles(5 * SSI_DELAY);

	while (i2c_get_pin(io_cli, SSI_INIT) > 0 && timer < 200)
		timer++;//waiting for init pin to go down

	if (timer >= 200) {
		DBG_LOG("FPGA did not answer to prog request, init pin not going low\n");
		i2c_set_pin(io_cli, SSI_PROG, 1);
		release_ssi_gpios();
		kfree(bitBuffer);
		iounmap(gpio_regs);

		return -EIO;
	}

	timer = 0;
	__delay_cycles(5 * SSI_DELAY);
	i2c_set_pin(io_cli, SSI_PROG, 1);

	while (i2c_get_pin(io_cli, SSI_INIT) == 0 && timer < 256) {//need to find a better way ...
		timer++;//waiting for init pin to go up
	}

	if (timer >= 256) {
		DBG_LOG("FPGA did not answer to prog request, init pin not going high\n");
		release_ssi_gpios();
		kfree(bitBuffer);
		iounmap(gpio_regs);

		return -EIO;
	}

	timer = 0;
	DBG_LOG("Starting configuration of %d bits\n", length * 8);

	for (i = 0; i < length; i++) {
		serialConfigWriteByte(bitBuffer[i]);
		schedule();
	}

	DBG_LOG("Waiting for done pin to go high\n");

	while (timer < 50) {
		ssiClearClk();
		__delay_cycles(SSI_DELAY);
		ssiSetClk();
		__delay_cycles(SSI_DELAY);
		timer++;
	}

	gpiod_set_value(ssi_clk_desc, 0);
	gpiod_set_value(ssi_data_desc, 1);

	if (i2c_get_pin(io_cli, SSI_DONE) == 0) {
		DBG_LOG("FPGA prog failed, done pin not going high\n");
		release_ssi_gpios();
		kfree(bitBuffer);
		iounmap(gpio_regs);

		return -EIO;
	}

	i2c_buffer[0] = I2C_IO_EXP_CONFIG_REG;
	i2c_buffer[1] = 0xDC;
	i2c_master_send(io_cli, i2c_buffer, 2);//set all unused config pins as input (keeping mode pins and PROG as output)
	gpiod_direction_input(ssi_clk_desc);
	gpiod_direction_input(ssi_data_desc);
	release_ssi_gpios();
	iounmap(gpio_regs);
	//release_mem_region(GPIO3_BASE + 0x190, 8);
	kfree(bitBuffer);

	return length;
}
