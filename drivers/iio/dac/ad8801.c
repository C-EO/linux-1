// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO DAC driver for Analog Devices AD8801 DAC
 *
 * Copyright (C) 2016 Gwenhael Goavec-Merou
 */

#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>

#define AD8801_CFG_ADDR_OFFSET 8

enum ad8801_device_ids {
	ID_AD8801,
	ID_AD8803,
};

struct ad8801_state {
	struct spi_device *spi;
	unsigned char dac_cache[8]; /* Value write on each channel */
	unsigned int vrefh_mv;
	unsigned int vrefl_mv;

	__be16 data __aligned(IIO_DMA_MINALIGN);
};

static int ad8801_spi_write(struct ad8801_state *state,
	u8 channel, unsigned char value)
{
	state->data = cpu_to_be16((channel << AD8801_CFG_ADDR_OFFSET) | value);
	return spi_write(state->spi, &state->data, sizeof(state->data));
}

static int ad8801_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct ad8801_state *state = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= 256 || val < 0)
			return -EINVAL;

		ret = ad8801_spi_write(state, chan->channel, val);
		if (ret == 0)
			state->dac_cache[chan->channel] = val;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ad8801_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct ad8801_state *state = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		*val = state->dac_cache[chan->channel];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = state->vrefh_mv - state->vrefl_mv;
		*val2 = 8;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		*val = state->vrefl_mv;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static const struct iio_info ad8801_info = {
	.read_raw = ad8801_read_raw,
	.write_raw = ad8801_write_raw,
};

#define AD8801_CHANNEL(chan) {		\
	.type = IIO_VOLTAGE,			\
	.indexed = 1,				\
	.output = 1,				\
	.channel = chan,			\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
		BIT(IIO_CHAN_INFO_OFFSET), \
}

static const struct iio_chan_spec ad8801_channels[] = {
	AD8801_CHANNEL(0),
	AD8801_CHANNEL(1),
	AD8801_CHANNEL(2),
	AD8801_CHANNEL(3),
	AD8801_CHANNEL(4),
	AD8801_CHANNEL(5),
	AD8801_CHANNEL(6),
	AD8801_CHANNEL(7),
};

static int ad8801_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad8801_state *state;
	const struct spi_device_id *id;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*state));
	if (indio_dev == NULL)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	state->spi = spi;
	id = spi_get_device_id(spi);

	ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vrefh");
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret,
				     "failed to get Vrefh voltage\n");

	state->vrefh_mv = ret / 1000;

	if (id->driver_data == ID_AD8803) {
		ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vrefl");
		if (ret < 0)
			return dev_err_probe(&spi->dev, ret,
					     "failed to get Vrefl voltage\n");

		state->vrefl_mv = ret / 1000;
	}

	indio_dev->info = &ad8801_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad8801_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad8801_channels);
	indio_dev->name = id->name;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to register iio device\n");

	return 0;
}

static const struct spi_device_id ad8801_ids[] = {
	{"ad8801", ID_AD8801},
	{"ad8803", ID_AD8803},
	{ }
};
MODULE_DEVICE_TABLE(spi, ad8801_ids);

static struct spi_driver ad8801_driver = {
	.driver = {
		.name	= "ad8801",
	},
	.probe		= ad8801_probe,
	.id_table	= ad8801_ids,
};
module_spi_driver(ad8801_driver);

MODULE_AUTHOR("Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>");
MODULE_DESCRIPTION("Analog Devices AD8801/AD8803 DAC");
MODULE_LICENSE("GPL v2");
