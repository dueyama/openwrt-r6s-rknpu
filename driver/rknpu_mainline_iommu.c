// SPDX-License-Identifier: GPL-2.0
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/* R6S Linux 6.12 port: public DMA/IOMMU APIs only. */
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/dev_printk.h>
#include "rknpu_iommu.h"
int rknpu_iommu_dma_map_sg(struct device *dev, struct scatterlist *sg,
 int nents, enum dma_data_direction dir, bool iova_aligned)
{
 struct scatterlist *entry;
 dma_addr_t next = 0;
 int mapped, i, seen = 0;
 mapped = dma_map_sg(dev, sg, nents, dir);
 if (!mapped) return 0;
 /* Vendor command buffers require a contiguous device-address range.
  * The kernel DMA mapper owns allocation/unmapping; never cast its cookie. */
 for_each_sg(sg, entry, mapped, i) {
  if (!sg_dma_len(entry)) continue;
  if (seen && sg_dma_address(entry) != next) goto reject;
  next = sg_dma_address(entry) + sg_dma_len(entry);
  if (next < sg_dma_address(entry)) goto reject;
  seen++;
 }
 if (seen != mapped) goto reject;
 return mapped;
reject:
 dma_unmap_sg(dev, sg, nents, dir);
 return 0;
}
void rknpu_iommu_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
 int nents, enum dma_data_direction dir, bool iova_aligned)
{
 dma_unmap_sg(dev, sg, nents, dir);
}
int rknpu_iommu_init_domain(struct rknpu_device *npu)
{
 struct iommu_domain *domain = iommu_get_domain_for_dev(npu->dev);
 if (!npu->iommu_en || !domain) return -ENODEV;
 if (!(domain->type & __IOMMU_DOMAIN_DMA_API))
  return dev_err_probe(npu->dev, -EOPNOTSUPP,
                       "translated DMA domain required; current type=%u\n",
                       domain->type);
 npu->iommu_domains[0] = domain;
 npu->iommu_domain_id = 0;
 npu->iommu_domain_num = 1;
 return 0;
}
int rknpu_iommu_switch_domain(struct rknpu_device *npu, int id)
{
 if (id != 0) return -EOPNOTSUPP;
 if (iommu_get_domain_for_dev(npu->dev) != npu->iommu_domains[0])
  return -ENODEV;
 return 0;
}
int rknpu_iommu_domain_get_and_switch(struct rknpu_device *npu, int id)
{
 int ret;
 mutex_lock(&npu->domain_lock);
 ret = rknpu_iommu_switch_domain(npu, id);
 if (!ret) atomic_inc(&npu->iommu_domain_refcount);
 mutex_unlock(&npu->domain_lock);
 return ret;
}
int rknpu_iommu_domain_put(struct rknpu_device *npu)
{
 if (WARN_ON_ONCE(atomic_dec_return(&npu->iommu_domain_refcount) < 0))
  return -EINVAL;
 return 0;
}
void rknpu_iommu_free_domains(struct rknpu_device *npu)
{
 /* Domain 0 belongs to the IOMMU core, not this driver. */
 WARN_ON_ONCE(atomic_read(&npu->iommu_domain_refcount));
 npu->iommu_domains[0] = NULL;
 npu->iommu_domain_num = 0;
}
