/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/node.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/page-flags.h>
#include <uapi/linux/mempolicy.h>
#include <asm/page.h>

struct notifier_block;

struct bio;

struct pagevec;

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_DISCARD	0x10000 /* enable discard for swap */
#define SWAP_FLAG_DISCARD_ONCE	0x20000 /* discard swap area at swapon-time */
#define SWAP_FLAG_DISCARD_PAGES 0x40000 /* discard page-clusters after use */

#define SWAP_FLAGS_VALID	(SWAP_FLAG_PRIO_MASK | SWAP_FLAG_PREFER | \
				 SWAP_FLAG_DISCARD | SWAP_FLAG_DISCARD_ONCE | \
				 SWAP_FLAG_DISCARD_PAGES)
#define SWAP_BATCH 64

static inline int current_is_kswapd(void)
{
	return current->flags & PF_KSWAPD;
}

/*
 * MAX_SWAPFILES defines the maximum number of swaptypes: things which can
 * be swapped to.  The swap type and the offset into that swap type are
 * encoded into pte's and into pgoff_t's in the swapcache.  Using five bits
 * for the type means that the maximum number of swapcache pages is 27 bits
 * on 32-bit-pgoff_t architectures.  And that assumes that the architecture packs
 * the type/offset into the pte as 5/27 as well.
 */
#define MAX_SWAPFILES_SHIFT	5

/*
 * Use some of the swap files numbers for other purposes. This
 * is a convenient way to hook into the VM to trigger special
 * actions on faults.
 */

/*
 * PTE markers are used to persist information onto PTEs that otherwise
 * should be a none pte.  As its name "PTE" hints, it should only be
 * applied to the leaves of pgtables.
 */
#define SWP_PTE_MARKER_NUM 1
#define SWP_PTE_MARKER     (MAX_SWAPFILES + SWP_HWPOISON_NUM + \
			    SWP_MIGRATION_NUM + SWP_DEVICE_NUM)

/*
 * Unaddressable device memory support. See include/linux/hmm.h and
 * Documentation/mm/hmm.rst. Short description is we need struct pages for
 * device memory that is unaddressable (inaccessible) by CPU, so that we can
 * migrate part of a process memory to device memory.
 *
 * When a page is migrated from CPU to device, we set the CPU page table entry
 * to a special SWP_DEVICE_{READ|WRITE} entry.
 *
 * When a page is mapped by the device for exclusive access we set the CPU page
 * table entries to a special SWP_DEVICE_EXCLUSIVE entry.
 */
#ifdef CONFIG_DEVICE_PRIVATE
#define SWP_DEVICE_NUM 3
#define SWP_DEVICE_WRITE (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM)
#define SWP_DEVICE_READ (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM+1)
#define SWP_DEVICE_EXCLUSIVE (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM+2)
#else
#define SWP_DEVICE_NUM 0
#endif

/*
 * Page migration support.
 *
 * SWP_MIGRATION_READ_EXCLUSIVE is only applicable to anonymous pages and
 * indicates that the referenced (part of) an anonymous page is exclusive to
 * a single process. For SWP_MIGRATION_WRITE, that information is implicit:
 * (part of) an anonymous page that are mapped writable are exclusive to a
 * single process.
 */
#ifdef CONFIG_MIGRATION
#define SWP_MIGRATION_NUM 3
#define SWP_MIGRATION_READ (MAX_SWAPFILES + SWP_HWPOISON_NUM)
#define SWP_MIGRATION_READ_EXCLUSIVE (MAX_SWAPFILES + SWP_HWPOISON_NUM + 1)
#define SWP_MIGRATION_WRITE (MAX_SWAPFILES + SWP_HWPOISON_NUM + 2)
#else
#define SWP_MIGRATION_NUM 0
#endif

/*
 * Handling of hardware poisoned pages with memory corruption.
 */
#ifdef CONFIG_MEMORY_FAILURE
#define SWP_HWPOISON_NUM 1
#define SWP_HWPOISON		MAX_SWAPFILES
#else
#define SWP_HWPOISON_NUM 0
#endif

#define MAX_SWAPFILES \
	((1 << MAX_SWAPFILES_SHIFT) - SWP_DEVICE_NUM - \
	SWP_MIGRATION_NUM - SWP_HWPOISON_NUM - \
	SWP_PTE_MARKER_NUM)

/*
 * Magic header for a swap area. The first part of the union is
 * what the swap magic looks like for the old (limited to 128MB)
 * swap area format, the second part of the union adds - in the
 * old reserved area - some extra information. Note that the first
 * kilobyte is reserved for boot loader or disk label stuff...
 *
 * Having the magic at the end of the PAGE_SIZE makes detecting swap
 * areas somewhat tricky on machines that support multiple page sizes.
 * For 2.5 we'll probably want to move the magic to just beyond the
 * bootbits...
 */
union swap_header {
	struct {
		char reserved[PAGE_SIZE - 10];
		char magic[10];			/* SWAP-SPACE or SWAPSPACE2 */
	} magic;
	struct {
		char		bootbits[1024];	/* Space for disklabel etc. */
		__u32		version;
		__u32		last_page;
		__u32		nr_badpages;
		unsigned char	sws_uuid[16];
		unsigned char	sws_volume[16];
		__u32		padding[117];
		__u32		badpages[1];
	} info;
};

/*
 * current->reclaim_state points to one of these when a task is running
 * memory reclaim
 */
struct reclaim_state {
	/* pages reclaimed outside of LRU-based reclaim */
	unsigned long reclaimed;
#ifdef CONFIG_LRU_GEN
	/* per-thread mm walk data */
	struct lru_gen_mm_walk *mm_walk;
#endif
};

/*
 * mm_account_reclaimed_pages(): account reclaimed pages outside of LRU-based
 * reclaim
 * @pages: number of pages reclaimed
 *
 * If the current process is undergoing a reclaim operation, increment the
 * number of reclaimed pages by @pages.
 */
static inline void mm_account_reclaimed_pages(unsigned long pages)
{
	if (current->reclaim_state)
		current->reclaim_state->reclaimed += pages;
}

#ifdef __KERNEL__

struct address_space;
struct sysinfo;
struct writeback_control;
struct zone;

/*
 * A swap extent maps a range of a swapfile's PAGE_SIZE pages onto a range of
 * disk blocks.  A rbtree of swap extents maps the entire swapfile (Where the
 * term `swapfile' refers to either a blockdevice or an IS_REG file). Apart
 * from setup, they're handled identically.
 *
 * We always assume that blocks are of size PAGE_SIZE.
 */
struct swap_extent {
	struct rb_node rb_node;
	pgoff_t start_page;
	pgoff_t nr_pages;
	sector_t start_block;
};

/*
 * Max bad pages in the new format..
 */
#define MAX_SWAP_BADPAGES \
	((offsetof(union swap_header, magic.magic) - \
	  offsetof(union swap_header, info.badpages)) / sizeof(int))

enum {
	SWP_USED	= (1 << 0),	/* is slot in swap_info[] used? */
	SWP_WRITEOK	= (1 << 1),	/* ok to write to this swap?	*/
	SWP_DISCARDABLE = (1 << 2),	/* blkdev support discard */
	SWP_DISCARDING	= (1 << 3),	/* now discarding a free cluster */
	SWP_SOLIDSTATE	= (1 << 4),	/* blkdev seeks are cheap */
	SWP_CONTINUED	= (1 << 5),	/* swap_map has count continuation */
	SWP_BLKDEV	= (1 << 6),	/* its a block device */
	SWP_ACTIVATED	= (1 << 7),	/* set after swap_activate success */
	SWP_FS_OPS	= (1 << 8),	/* swapfile operations go through fs */
	SWP_AREA_DISCARD = (1 << 9),	/* single-time swap area discards */
	SWP_PAGE_DISCARD = (1 << 10),	/* freed swap page-cluster discards */
	SWP_STABLE_WRITES = (1 << 11),	/* no overwrite PG_writeback pages */
	SWP_SYNCHRONOUS_IO = (1 << 12),	/* synchronous IO is efficient */
					/* add others here before... */
};

#define SWAP_CLUSTER_MAX 32UL
#define SWAP_CLUSTER_MAX_SKIPPED (SWAP_CLUSTER_MAX << 10)
#define COMPACT_CLUSTER_MAX SWAP_CLUSTER_MAX

/* Bit flag in swap_map */
#define SWAP_HAS_CACHE	0x40	/* Flag page is cached, in first swap_map */
#define COUNT_CONTINUED	0x80	/* Flag swap_map continuation for full count */

/* Special value in first swap_map */
#define SWAP_MAP_MAX	0x3e	/* Max count */
#define SWAP_MAP_BAD	0x3f	/* Note page is bad */
#define SWAP_MAP_SHMEM	0xbf	/* Owned by shmem/tmpfs */

/* Special value in each swap_map continuation */
#define SWAP_CONT_MAX	0x7f	/* Max count */

/*
 * We use this to track usage of a cluster. A cluster is a block of swap disk
 * space with SWAPFILE_CLUSTER pages long and naturally aligns in disk. All
 * free clusters are organized into a list. We fetch an entry from the list to
 * get a free cluster.
 *
 * The flags field determines if a cluster is free. This is
 * protected by cluster lock.
 */
struct swap_cluster_info {
	spinlock_t lock;	/*
				 * Protect swap_cluster_info fields
				 * other than list, and swap_info_struct->swap_map
				 * elements corresponding to the swap cluster.
				 */
	u16 count;
	u8 flags;
	u8 order;
	struct list_head list;
};

/* All on-list cluster must have a non-zero flag. */
enum swap_cluster_flags {
	CLUSTER_FLAG_NONE = 0, /* For temporary off-list cluster */
	CLUSTER_FLAG_FREE,
	CLUSTER_FLAG_NONFULL,
	CLUSTER_FLAG_FRAG,
	/* Clusters with flags above are allocatable */
	CLUSTER_FLAG_USABLE = CLUSTER_FLAG_FRAG,
	CLUSTER_FLAG_FULL,
	CLUSTER_FLAG_DISCARD,
	CLUSTER_FLAG_MAX,
};

/*
 * The first page in the swap file is the swap header, which is always marked
 * bad to prevent it from being allocated as an entry. This also prevents the
 * cluster to which it belongs being marked free. Therefore 0 is safe to use as
 * a sentinel to indicate an entry is not valid.
 */
#define SWAP_ENTRY_INVALID	0

#ifdef CONFIG_THP_SWAP
#define SWAP_NR_ORDERS		(PMD_ORDER + 1)
#else
#define SWAP_NR_ORDERS		1
#endif

/*
 * We keep using same cluster for rotational device so IO will be sequential.
 * The purpose is to optimize SWAP throughput on these device.
 */
struct swap_sequential_cluster {
	unsigned int next[SWAP_NR_ORDERS]; /* Likely next allocation offset */
};

/*
 * The in-memory structure used to track swap areas.
 */
struct swap_info_struct {
	struct percpu_ref users;	/* indicate and keep swap device valid. */
	unsigned long	flags;		/* SWP_USED etc: see above */
	signed short	prio;		/* swap priority of this type */
	struct plist_node list;		/* entry in swap_active_head */
	signed char	type;		/* strange name for an index */
	unsigned int	max;		/* extent of the swap_map */
	unsigned char *swap_map;	/* vmalloc'ed array of usage counts */
	unsigned long *zeromap;		/* kvmalloc'ed bitmap to track zero pages */
	struct swap_cluster_info *cluster_info; /* cluster info. Only for SSD */
	struct list_head free_clusters; /* free clusters list */
	struct list_head full_clusters; /* full clusters list */
	struct list_head nonfull_clusters[SWAP_NR_ORDERS];
					/* list of cluster that contains at least one free slot */
	struct list_head frag_clusters[SWAP_NR_ORDERS];
					/* list of cluster that are fragmented or contented */
	atomic_long_t frag_cluster_nr[SWAP_NR_ORDERS];
	unsigned int pages;		/* total of usable pages of swap */
	atomic_long_t inuse_pages;	/* number of those currently in use */
	struct swap_sequential_cluster *global_cluster; /* Use one global cluster for rotating device */
	spinlock_t global_cluster_lock;	/* Serialize usage of global cluster */
	struct rb_root swap_extent_root;/* root of the swap extent rbtree */
	struct block_device *bdev;	/* swap device or bdev of swap file */
	struct file *swap_file;		/* seldom referenced */
	struct completion comp;		/* seldom referenced */
	spinlock_t lock;		/*
					 * protect map scan related fields like
					 * swap_map, lowest_bit, highest_bit,
					 * inuse_pages, cluster_next,
					 * cluster_nr, lowest_alloc,
					 * highest_alloc, free/discard cluster
					 * list. other fields are only changed
					 * at swapon/swapoff, so are protected
					 * by swap_lock. changing flags need
					 * hold this lock and swap_lock. If
					 * both locks need hold, hold swap_lock
					 * first.
					 */
	spinlock_t cont_lock;		/*
					 * protect swap count continuation page
					 * list.
					 */
	struct work_struct discard_work; /* discard worker */
	struct work_struct reclaim_work; /* reclaim worker */
	struct list_head discard_clusters; /* discard clusters list */
	struct plist_node avail_lists[]; /*
					   * entries in swap_avail_heads, one
					   * entry per node.
					   * Must be last as the number of the
					   * array is nr_node_ids, which is not
					   * a fixed value so have to allocate
					   * dynamically.
					   * And it has to be an array so that
					   * plist_for_each_* can work.
					   */
};

static inline swp_entry_t page_swap_entry(struct page *page)
{
	struct folio *folio = page_folio(page);
	swp_entry_t entry = folio->swap;

	entry.val += folio_page_idx(folio, page);
	return entry;
}

/* linux/mm/workingset.c */
bool workingset_test_recent(void *shadow, bool file, bool *workingset,
				bool flush);
void workingset_age_nonresident(struct lruvec *lruvec, unsigned long nr_pages);
void *workingset_eviction(struct folio *folio, struct mem_cgroup *target_memcg);
void workingset_refault(struct folio *folio, void *shadow);
void workingset_activation(struct folio *folio);

/* linux/mm/page_alloc.c */
extern unsigned long totalreserve_pages;

/* Definition of global_zone_page_state not available yet */
#define nr_free_pages() global_zone_page_state(NR_FREE_PAGES)


/* linux/mm/swap.c */
void lru_note_cost_unlock_irq(struct lruvec *lruvec, bool file,
		unsigned int nr_io, unsigned int nr_rotated)
		__releases(lruvec->lru_lock);
void lru_note_cost_refault(struct folio *);
void folio_add_lru(struct folio *);
void folio_add_lru_vma(struct folio *, struct vm_area_struct *);
void mark_page_accessed(struct page *);
void folio_mark_accessed(struct folio *);

extern atomic_t lru_disable_count;

static inline bool lru_cache_disabled(void)
{
	return atomic_read(&lru_disable_count);
}

static inline void lru_cache_enable(void)
{
	atomic_dec(&lru_disable_count);
}

extern void lru_cache_disable(void);
extern void lru_add_drain(void);
extern void lru_add_drain_cpu(int cpu);
extern void lru_add_drain_cpu_zone(struct zone *zone);
extern void lru_add_drain_all(void);
void folio_deactivate(struct folio *folio);
void folio_mark_lazyfree(struct folio *folio);
extern void swap_setup(void);

/* linux/mm/vmscan.c */
extern unsigned long zone_reclaimable_pages(struct zone *zone);
extern unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
					gfp_t gfp_mask, nodemask_t *mask);

#define MEMCG_RECLAIM_MAY_SWAP (1 << 1)
#define MEMCG_RECLAIM_PROACTIVE (1 << 2)
#define MIN_SWAPPINESS 0
#define MAX_SWAPPINESS 200

/* Just reclaim from anon folios in proactive memory reclaim */
#define SWAPPINESS_ANON_ONLY (MAX_SWAPPINESS + 1)

extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
						  unsigned long nr_pages,
						  gfp_t gfp_mask,
						  unsigned int reclaim_options,
						  int *swappiness);
extern unsigned long mem_cgroup_shrink_node(struct mem_cgroup *mem,
						gfp_t gfp_mask, bool noswap,
						pg_data_t *pgdat,
						unsigned long *nr_scanned);
extern unsigned long shrink_all_memory(unsigned long nr_pages);
extern int vm_swappiness;
long remove_mapping(struct address_space *mapping, struct folio *folio);

#if defined(CONFIG_SYSFS) && defined(CONFIG_NUMA)
extern int reclaim_register_node(struct node *node);
extern void reclaim_unregister_node(struct node *node);

#else

static inline int reclaim_register_node(struct node *node)
{
	return 0;
}

static inline void reclaim_unregister_node(struct node *node)
{
}
#endif /* CONFIG_SYSFS && CONFIG_NUMA */

#ifdef CONFIG_NUMA
extern int sysctl_min_unmapped_ratio;
extern int sysctl_min_slab_ratio;
#endif

void check_move_unevictable_folios(struct folio_batch *fbatch);

extern void __meminit kswapd_run(int nid);
extern void __meminit kswapd_stop(int nid);

#ifdef CONFIG_SWAP

int add_swap_extent(struct swap_info_struct *sis, unsigned long start_page,
		unsigned long nr_pages, sector_t start_block);
int generic_swapfile_activate(struct swap_info_struct *, struct file *,
		sector_t *);

static inline unsigned long total_swapcache_pages(void)
{
	return global_node_page_state(NR_SWAPCACHE);
}

void free_swap_cache(struct folio *folio);
void free_folio_and_swap_cache(struct folio *folio);
void free_pages_and_swap_cache(struct encoded_page **, int);
/* linux/mm/swapfile.c */
extern atomic_long_t nr_swap_pages;
extern long total_swap_pages;
extern atomic_t nr_rotate_swap;

/* Swap 50% full? Release swapcache more aggressively.. */
static inline bool vm_swap_full(void)
{
	return atomic_long_read(&nr_swap_pages) * 2 < total_swap_pages;
}

static inline long get_nr_swap_pages(void)
{
	return atomic_long_read(&nr_swap_pages);
}

extern void si_swapinfo(struct sysinfo *);
int folio_alloc_swap(struct folio *folio, gfp_t gfp_mask);
bool folio_free_swap(struct folio *folio);
void put_swap_folio(struct folio *folio, swp_entry_t entry);
extern swp_entry_t get_swap_page_of_type(int);
extern int add_swap_count_continuation(swp_entry_t, gfp_t);
extern void swap_shmem_alloc(swp_entry_t, int);
extern int swap_duplicate(swp_entry_t);
extern int swapcache_prepare(swp_entry_t entry, int nr);
extern void swap_free_nr(swp_entry_t entry, int nr_pages);
extern void free_swap_and_cache_nr(swp_entry_t entry, int nr);
int swap_type_of(dev_t device, sector_t offset);
int find_first_swap(dev_t *device);
extern unsigned int count_swap_pages(int, int);
extern sector_t swapdev_block(int, pgoff_t);
extern int __swap_count(swp_entry_t entry);
extern bool swap_entry_swapped(struct swap_info_struct *si, swp_entry_t entry);
extern int swp_swapcount(swp_entry_t entry);
struct swap_info_struct *swp_swap_info(swp_entry_t entry);
struct backing_dev_info;
extern int init_swap_address_space(unsigned int type, unsigned long nr_pages);
extern void exit_swap_address_space(unsigned int type);
extern struct swap_info_struct *get_swap_device(swp_entry_t entry);
sector_t swap_folio_sector(struct folio *folio);

static inline void put_swap_device(struct swap_info_struct *si)
{
	percpu_ref_put(&si->users);
}

#else /* CONFIG_SWAP */
static inline struct swap_info_struct *swp_swap_info(swp_entry_t entry)
{
	return NULL;
}

static inline struct swap_info_struct *get_swap_device(swp_entry_t entry)
{
	return NULL;
}

static inline void put_swap_device(struct swap_info_struct *si)
{
}

#define get_nr_swap_pages()			0L
#define total_swap_pages			0L
#define total_swapcache_pages()			0UL
#define vm_swap_full()				0

#define si_swapinfo(val) \
	do { (val)->freeswap = (val)->totalswap = 0; } while (0)
#define free_folio_and_swap_cache(folio) \
	folio_put(folio)
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

static inline void free_swap_and_cache_nr(swp_entry_t entry, int nr)
{
}

static inline void free_swap_cache(struct folio *folio)
{
}

static inline int add_swap_count_continuation(swp_entry_t swp, gfp_t gfp_mask)
{
	return 0;
}

static inline void swap_shmem_alloc(swp_entry_t swp, int nr)
{
}

static inline int swap_duplicate(swp_entry_t swp)
{
	return 0;
}

static inline int swapcache_prepare(swp_entry_t swp, int nr)
{
	return 0;
}

static inline void swap_free_nr(swp_entry_t entry, int nr_pages)
{
}

static inline void put_swap_folio(struct folio *folio, swp_entry_t swp)
{
}

static inline int __swap_count(swp_entry_t entry)
{
	return 0;
}

static inline bool swap_entry_swapped(struct swap_info_struct *si, swp_entry_t entry)
{
	return false;
}

static inline int swp_swapcount(swp_entry_t entry)
{
	return 0;
}

static inline int folio_alloc_swap(struct folio *folio, gfp_t gfp_mask)
{
	return -EINVAL;
}

static inline bool folio_free_swap(struct folio *folio)
{
	return false;
}

static inline int add_swap_extent(struct swap_info_struct *sis,
				  unsigned long start_page,
				  unsigned long nr_pages, sector_t start_block)
{
	return -EINVAL;
}
#endif /* CONFIG_SWAP */

static inline void free_swap_and_cache(swp_entry_t entry)
{
	free_swap_and_cache_nr(entry, 1);
}

static inline void swap_free(swp_entry_t entry)
{
	swap_free_nr(entry, 1);
}

#ifdef CONFIG_MEMCG
static inline int mem_cgroup_swappiness(struct mem_cgroup *memcg)
{
	/* Cgroup2 doesn't have per-cgroup swappiness */
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return READ_ONCE(vm_swappiness);

	/* root ? */
	if (mem_cgroup_disabled() || mem_cgroup_is_root(memcg))
		return READ_ONCE(vm_swappiness);

	return READ_ONCE(memcg->swappiness);
}
#else
static inline int mem_cgroup_swappiness(struct mem_cgroup *mem)
{
	return READ_ONCE(vm_swappiness);
}
#endif

#if defined(CONFIG_SWAP) && defined(CONFIG_MEMCG) && defined(CONFIG_BLK_CGROUP)
void __folio_throttle_swaprate(struct folio *folio, gfp_t gfp);
static inline void folio_throttle_swaprate(struct folio *folio, gfp_t gfp)
{
	if (mem_cgroup_disabled())
		return;
	__folio_throttle_swaprate(folio, gfp);
}
#else
static inline void folio_throttle_swaprate(struct folio *folio, gfp_t gfp)
{
}
#endif

#if defined(CONFIG_MEMCG) && defined(CONFIG_SWAP)
int __mem_cgroup_try_charge_swap(struct folio *folio, swp_entry_t entry);
static inline int mem_cgroup_try_charge_swap(struct folio *folio,
		swp_entry_t entry)
{
	if (mem_cgroup_disabled())
		return 0;
	return __mem_cgroup_try_charge_swap(folio, entry);
}

extern void __mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages);
static inline void mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages)
{
	if (mem_cgroup_disabled())
		return;
	__mem_cgroup_uncharge_swap(entry, nr_pages);
}

extern long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg);
extern bool mem_cgroup_swap_full(struct folio *folio);
#else
static inline int mem_cgroup_try_charge_swap(struct folio *folio,
					     swp_entry_t entry)
{
	return 0;
}

static inline void mem_cgroup_uncharge_swap(swp_entry_t entry,
					    unsigned int nr_pages)
{
}

static inline long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg)
{
	return get_nr_swap_pages();
}

static inline bool mem_cgroup_swap_full(struct folio *folio)
{
	return vm_swap_full();
}
#endif

#endif /* __KERNEL__*/
#endif /* _LINUX_SWAP_H */
