/*
 * logi_dma.c - DMA support routines
*/

#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include "logi_dma.h"
#include "drvr.h"
#include "generic.h"


static volatile int irqraised1;
static dma_addr_t dmaphysbuf;
static struct completion dma_comp;
static dma_cookie_t cookie;

static void dma_callback(void *param)
{
	struct drvr_mem *mem_dev = (struct drvr_mem*) param;
	struct dma_chan *chan = mem_dev->dma.chan;

	switch (dma_async_is_tx_complete(chan, cookie, NULL, NULL)) {
		case DMA_COMPLETE:
			irqraised1 = 1;
			break;

		case DMA_ERROR:
			irqraised1 = -1;
			break;

		default:
			irqraised1 = -1;
			break;
	}

	complete(&dma_comp);
}

void logi_dma_init(void)
{
	init_completion(&dma_comp);
}

int logi_dma_open(struct device *dev, struct drvr_mem* mem_dev, dma_addr_t *physbuf)
{
	struct dma_slave_config	conf;
	dma_cap_mask_t mask;

	/* Allocate DMA buffer */
	mem_dev->dma.buf = dma_alloc_coherent(dev, MAX_DMA_TRANSFER_IN_BYTES,
				      &dmaphysbuf, GFP_KERNEL);

	if (!mem_dev->dma.buf) {
		DBG_LOG("failed to allocate DMA buffer\n");

		return -ENOMEM;
	}

	*physbuf = dmaphysbuf;

	/* Allocate DMA channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	mem_dev->dma.chan = dma_request_channel(mask, NULL, NULL);

	if (!mem_dev->dma.chan) {
		return -ENODEV;
	}

	/* Configure DMA channel */
	conf.direction = DMA_MEM_TO_MEM;
	/*conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;*/
	dmaengine_slave_config(mem_dev->dma.chan, &conf);

	DBG_LOG("Using Linux DMA Engine API");
	DBG_LOG("DMA channel reserved\n");

	return 0;
}

void logi_dma_release(struct device *dev, struct drvr_mem* mem_dev)
{
	dma_release_channel(mem_dev->dma.chan);
	dma_free_coherent(dev, MAX_DMA_TRANSFER_IN_BYTES, mem_dev->dma.buf, dmaphysbuf);
}

int logi_dma_copy(struct drvr_mem* mem_dev, unsigned long trgt_addr, unsigned long src_addr, int count)
{
	int result = 0;
	struct dma_chan *chan;
	struct dma_device *dev;
	struct dma_async_tx_descriptor *tx;
	unsigned long flags;

	chan = mem_dev->dma.chan;
	dev = chan->device;
	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	tx = dev->device_prep_dma_memcpy(chan, trgt_addr, src_addr, count, flags);

	if (!tx) {
		DBG_LOG("device_prep_dma_memcpy failed\n");

		return -ENODEV;
	}

	irqraised1 = 0u;
	dma_comp.done = 0;
	/* set the callback and submit the transaction */
	tx->callback = dma_callback;
	tx->callback_param = mem_dev;
	cookie = dmaengine_submit(tx);
	dma_async_issue_pending(chan);

	wait_for_completion(&dma_comp);

	/* Check the status of the completed transfer */

	if (irqraised1 < 0) {
		DBG_LOG("edma copy: Event Miss Occured!!!\n");
		dmaengine_terminate_sync(chan);
		result = -EAGAIN;
	}

	return result;
}

