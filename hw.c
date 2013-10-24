/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "edump.h"

static struct {
	uint32_t version;
	const char * name;
} mac_bb_names[] = {
	/* Devices with external radios */
	{ AR_SREV_VERSION_5416_PCI,	"5416" },
	{ AR_SREV_VERSION_5416_PCIE,	"5418" },
	{ AR_SREV_VERSION_9160,		"9160" },
	/* Single-chip solutions */
	{ AR_SREV_VERSION_9280,		"9280" },
	{ AR_SREV_VERSION_9285,		"9285" },
	{ AR_SREV_VERSION_9287,         "9287" },
	{ AR_SREV_VERSION_9300,         "9300" },
	{ AR_SREV_VERSION_9330,         "9330" },
	{ AR_SREV_VERSION_9485,         "9485" },
	{ AR_SREV_VERSION_9462,         "9462" },
	{ AR_SREV_VERSION_9565,         "9565" },
	{ AR_SREV_VERSION_9340,         "9340" },
	{ AR_SREV_VERSION_9550,         "9550" },
};

static const char *mac_bb_name(uint32_t mac_bb_version)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mac_bb_names); i++) {
		if (mac_bb_names[i].version == mac_bb_version) {
			return mac_bb_names[i].name;
		}
	}

	return "????";
}

static void hw_read_revisions(struct edump *edump)
{
	uint32_t val = REG_READ(AR_SREV);

	if ((val & AR_SREV_ID) == 0xFF) {
		edump->macVersion = (val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
		edump->macRev = MS(val, AR_SREV_REVISION2);
	} else {
		edump->macVersion = MS(val, AR_SREV_VERSION);
		edump->macRev = val & AR_SREV_REVISION;
	}

	printf("Atheros AR%s MAC/BB Rev:%x (SREV: 0x%08x)\n",
	       mac_bb_name(edump->macVersion), edump->macRev, val);
}

bool hw_wait(struct edump *edump, uint32_t reg, uint32_t mask,
	     uint32_t val, uint32_t timeout)
{
	int i;

	for (i = 0; i < (timeout / AH_TIME_QUANTUM); i++) {
		if ((REG_READ(reg) & mask) == val)
			return true;

		usleep(AH_TIME_QUANTUM);
	}

	return false;
}

static int hw_gpio_input_get_ar9xxx(struct edump *edump, unsigned gpio)
{
	uint32_t regval = REG_READ(AR9XXX_GPIO_IN_OUT);

	if (gpio >= edump->gpio_num)
		return 0;

	if (AR_SREV_9300_20_OR_LATER(edump))
		regval = MS(regval, AR9300_GPIO_IN_VAL);
	else if (AR_SREV_9287_11_OR_LATER(edump))
		regval = MS(regval, AR9287_GPIO_IN_VAL);
	else if (AR_SREV_9285_12_OR_LATER(edump))
		regval = MS(regval, AR9285_GPIO_IN_VAL);
	else if (AR_SREV_9280_20_OR_LATER(edump))
		regval = MS(regval, AR9280_GPIO_IN_VAL);
	else
		regval = MS(regval, AR5416_GPIO_IN_VAL);

	return !!(regval & BIT(gpio));
}

static int hw_gpio_output_get_ar9xxx(struct edump *edump, unsigned gpio)
{
	if (gpio >= edump->gpio_num)
		return 0;

	return !!(REG_READ(AR9XXX_GPIO_IN_OUT) & BIT(gpio));
}

static void hw_gpio_output_set_ar9xxx(struct edump *edump, unsigned gpio,
				      int val)
{
	REG_RMW(AR9XXX_GPIO_IN_OUT, !!val << gpio, 1 << gpio);
}

static int hw_gpio_out_mux_get_ar9xxx(struct edump *edump, unsigned gpio)
{
	uint32_t reg;
	unsigned sh = (gpio % 6) * 5;

	if (gpio >= edump->gpio_num)
		return 0;

	if (gpio > 11)
		reg = AR9XXX_GPIO_OUTPUT_MUX3;
	else if (gpio > 5)
		reg = AR9XXX_GPIO_OUTPUT_MUX2;
	else
		reg = AR9XXX_GPIO_OUTPUT_MUX1;

	return (REG_READ(reg) >> sh) & AR9XXX_GPIO_OUTPUT_MUX_MASK;
}

static void hw_gpio_out_mux_set_ar9xxx(struct edump *edump, unsigned gpio,
				       int type)
{
	uint32_t reg, tmp;
	unsigned sh = (gpio % 6) * 5;

	if (gpio >= edump->gpio_num)
		return;

	if (gpio > 11)
		reg = AR9XXX_GPIO_OUTPUT_MUX3;
	else if (gpio > 5)
		reg = AR9XXX_GPIO_OUTPUT_MUX2;
	else
		reg = AR9XXX_GPIO_OUTPUT_MUX1;

	if (AR_SREV_9280_20_OR_LATER(edump) ||
	    reg != AR9XXX_GPIO_OUTPUT_MUX1) {
		REG_RMW(reg, type << sh, AR9XXX_GPIO_OUTPUT_MUX_MASK << sh);
	} else {
		tmp = REG_READ(reg);
		tmp = ((tmp & 0x1f0) << 1) | (tmp & ~0x1f0);
		tmp &= ~(AR9XXX_GPIO_OUTPUT_MUX_MASK << sh);
		tmp |= type << sh;
		REG_WRITE(reg, tmp);
	}
}

static const char *hw_gpio_out_mux_get_str_ar9xxx(struct edump *edump,
						  unsigned gpio)
{
	int type = hw_gpio_out_mux_get_ar9xxx(edump, gpio);

	switch (type) {
	case AR9XXX_GPIO_OUTPUT_MUX_OUTPUT:
		return "Out";
	case AR9XXX_GPIO_OUTPUT_MUX_TX_FRAME:
		return "TxF";
	case AR9XXX_GPIO_OUTPUT_MUX_RX_CLEAR:
		return "RxC";
	case AR9XXX_GPIO_OUTPUT_MUX_MAC_NETWORK:
		return "Net";
	case AR9XXX_GPIO_OUTPUT_MUX_MAC_POWER:
		return "Pwr";
	}

	return "Unk";
}

static int hw_gpio_dir_get_ar9xxx(struct edump *edump, unsigned gpio)
{
	unsigned sh = gpio * 2;

	if (gpio >= edump->gpio_num)
		return -1;

	return (REG_READ(AR9XXX_GPIO_OE_OUT) >> sh) & AR9XXX_GPIO_OE_OUT_DRV;
}

static void hw_gpio_dir_set_out_ar9xxx(struct edump *edump, unsigned gpio)
{
	unsigned sh = gpio * 2;

	if (gpio >= edump->gpio_num)
		return;

	hw_gpio_out_mux_set_ar9xxx(edump, gpio, AR9XXX_GPIO_OUTPUT_MUX_OUTPUT);

	REG_RMW(AR9XXX_GPIO_OE_OUT,
		AR9XXX_GPIO_OE_OUT_DRV_ALL << sh,
		AR9XXX_GPIO_OE_OUT_DRV << sh);
}

static const char *hw_gpio_dir_get_str_ar9xxx(struct edump *edump,
					      unsigned gpio)
{
	int dir = hw_gpio_dir_get_ar9xxx(edump, gpio);

	switch (dir) {
	case AR9XXX_GPIO_OE_OUT_DRV_NO:
		return "In";
	case AR9XXX_GPIO_OE_OUT_DRV_LOW:
		return "Low";
	case AR9XXX_GPIO_OE_OUT_DRV_HI:
		return "Hi";
	case AR9XXX_GPIO_OE_OUT_DRV_ALL:
		return "Out";
	}

	return "Unk";
}

static const struct gpio_ops gpio_ops_ar9xxx = {
	.input_get = hw_gpio_input_get_ar9xxx,
	.output_get = hw_gpio_output_get_ar9xxx,
	.output_set = hw_gpio_output_set_ar9xxx,
	.dir_set_out = hw_gpio_dir_set_out_ar9xxx,
	.dir_get_str = hw_gpio_dir_get_str_ar9xxx,
	.out_mux_get_str = hw_gpio_out_mux_get_str_ar9xxx,
};

bool hw_eeprom_read_9xxx(struct edump *edump, uint32_t off, uint16_t *data)
{
#define WAIT_MASK	AR_EEPROM_STATUS_DATA_BUSY | \
			AR_EEPROM_STATUS_DATA_PROT_ACCESS
#define WAIT_TIME	AH_WAIT_TIMEOUT

	(void)REG_READ(AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));

	if (!hw_wait(edump, AR_EEPROM_STATUS_DATA, WAIT_MASK, 0, WAIT_TIME))
		return false;

	*data = MS(REG_READ(AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);

	return true;

#undef WAIT_TIME
#undef WAIT_MASK
}

bool hw_eeprom_write_9xxx(struct edump *edump, uint32_t off, uint16_t data)
{
#define WAIT_MASK	AR_EEPROM_STATUS_DATA_BUSY | \
			AR_EEPROM_STATUS_DATA_BUSY_ACCESS | \
			AR_EEPROM_STATUS_DATA_PROT_ACCESS | \
			AR_EEPROM_STATUS_DATA_ABSENT_ACCESS
#define WAIT_TIME	AH_WAIT_TIMEOUT

	REG_WRITE(AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S), data);
	if (!hw_wait(edump, AR_EEPROM_STATUS_DATA, WAIT_MASK, 0, WAIT_TIME))
		return false;

	return true;

#undef WAIT_TIME
#undef WAIT_MASK
}

bool hw_eeprom_read(struct edump *edump, uint32_t off, uint16_t *data)
{
	if (!edump->con->eep_read(edump, off, data))
		return false;

	if (edump->eep_io_swap)
		*data = bswap_16(*data);

	return true;
}

bool hw_eeprom_write(struct edump *edump, uint32_t off, uint16_t data)
{
	if (edump->eep_io_swap)
		data = bswap_16(data);

	if (!edump->con->eep_write(edump, off, data))
		return false;

	return true;
}

int hw_init(struct edump *edump)
{
	hw_read_revisions(edump);

	if (AR_SREV_5416_OR_LATER(edump)) {
		edump->gpio = &gpio_ops_ar9xxx;

		if (AR_SREV_9300_20_OR_LATER(edump))
			edump->gpio_num = 17;
		else if (AR_SREV_9287_11_OR_LATER(edump))
			edump->gpio_num = 11;
		else if (AR_SREV_9285_12_OR_LATER(edump))
			edump->gpio_num = 12;
		else if (AR_SREV_9280_20_OR_LATER(edump))
			edump->gpio_num = 10;
		else
			edump->gpio_num = 14;
	} else {
		fprintf(stderr, "Unable to configure chip GPIO support\n");
	}

	return 0;
}
