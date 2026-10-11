/* SPDX-License-Identifier: GPL-2.0 */
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
#ifndef R6S_RK3588_MMIO_H
#define R6S_RK3588_MMIO_H
#include <linux/ioport.h>
#include <linux/errno.h>
/* Normal exclusive claims for the upstream PC/CNA/CORE banks. The installed
 * vendor DT envelope remains unchanged; IOMMU reservations are not mapped.
 * A PC-only resource can supply PC, but must not invent absent CNA/CORE. */
enum r6s_rk3588_bank { R6S_BANK_PC, R6S_BANK_CNA, R6S_BANK_CORE };
static inline int r6s_rk3588_bank_resource(const struct resource *res,
										int core, int bank, struct resource *out)
{
	static const unsigned int offsets[] = { 0, 0x1000, 0x3000 };
	static const char * const names[] = {
		"r6s-rk3588-pc", "r6s-rk3588-cna", "r6s-rk3588-core"
	};
	if (!res || !out || core < 0 || core >= 3 || bank < R6S_BANK_PC || bank > R6S_BANK_CORE ||
		resource_type(res) != IORESOURCE_MEM ||
		res->start != 0xfdab0000ULL + (unsigned long long)core * 0x10000 || res->end < res->start ||
		(resource_size(res) != 0x1000 && resource_size(res) != 0x10000) ||
		resource_size(res) < offsets[bank] + 0x1000)
		return -EINVAL;
	*out = (struct resource) {
		.start = res->start + offsets[bank],
		.end = res->start + offsets[bank] + 0xfff,
		.flags = res->flags, .name = names[bank],
	};
	return 0;
}
#endif
