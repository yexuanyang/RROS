// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec for arm64
 *
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 */

#include "asm/io.h"
#include "asm/processor.h"
#include "linux/crash_dump.h"
#include "linux/gfp.h"
#include "linux/io.h"
#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/types.h"
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/page-flags.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/errno.h>

#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>
#include <asm/daifflags.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>

#include "cpu-reset.h"

/* Global variables for the arm64_relocate_new_kernel routine. */
extern const unsigned char arm64_relocate_new_kernel[];
extern const unsigned long arm64_relocate_new_kernel_size;
bool is_crash_kernel;
void* migration_threads;

/**
 * kexec_image_info - For debugging output.
 */
#define kexec_image_info(_i) _kexec_image_info(__func__, __LINE__, _i)
static void _kexec_image_info(const char *func, int line,
	const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("%s:%d:\n", func, line);
	pr_debug("  kexec kimage info:\n");
	pr_debug("    type:        %d\n", kimage->type);
	pr_debug("    start:       %lx\n", kimage->start);
	pr_debug("    head:        %lx\n", kimage->head);
	pr_debug("    nr_segments: %lu\n", kimage->nr_segments);
	pr_debug("    kern_reloc: %pa\n", &kimage->arch.kern_reloc);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("      segment[%lu]: %016lx - %016lx, 0x%lx bytes, %lu pages\n",
			i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz,
			kimage->segment[i].memsz,
			kimage->segment[i].memsz /  PAGE_SIZE);
	}
}

void machine_kexec_cleanup(struct kimage *kimage)
{
	/* Empty routine needed to avoid build errors. */
}

int machine_kexec_post_load(struct kimage *kimage)
{
	void *reloc_code = page_to_virt(kimage->control_code_page);

	memcpy(reloc_code, arm64_relocate_new_kernel,
	       arm64_relocate_new_kernel_size);
	kimage->arch.kern_reloc = __pa(reloc_code);
	kexec_image_info(kimage);

	/* Flush the reloc_code in preparation for its execution. */
	__flush_dcache_area(reloc_code, arm64_relocate_new_kernel_size);
	flush_icache_range((uintptr_t)reloc_code, (uintptr_t)reloc_code +
			   arm64_relocate_new_kernel_size);

	return 0;
}

/**
 * machine_kexec_prepare - Prepare for a kexec reboot.
 *
 * Called from the core kexec code when a kernel image is loaded.
 * Forbid loading a kexec kernel if we have no way of hotplugging cpus or cpus
 * are stuck in the kernel. This avoids a panic once we hit machine_kexec().
 */
int machine_kexec_prepare(struct kimage *kimage)
{
	if (kimage->type != KEXEC_TYPE_CRASH && cpus_are_stuck_in_kernel()) {
		pr_err("Can't kexec: CPUs are stuck in the kernel.\n");
		return -EBUSY;
	}

	return 0;
}

/**
 * kexec_list_flush - Helper to flush the kimage list and source pages to PoC.
 */
static void kexec_list_flush(struct kimage *kimage)
{
	kimage_entry_t *entry;

	for (entry = &kimage->head; ; entry++) {
		unsigned int flag;
		void *addr;

		/* flush the list entries. */
		__flush_dcache_area(entry, sizeof(kimage_entry_t));

		flag = *entry & IND_FLAGS;
		if (flag == IND_DONE)
			break;

		addr = phys_to_virt(*entry & PAGE_MASK);

		switch (flag) {
		case IND_INDIRECTION:
			/* Set entry point just before the new list page. */
			entry = (kimage_entry_t *)addr - 1;
			break;
		case IND_SOURCE:
			/* flush the source pages. */
			__flush_dcache_area(addr, PAGE_SIZE);
			break;
		case IND_DESTINATION:
			break;
		default:
			BUG();
		}
	}
}

/**
 * kexec_segment_flush - Helper to flush the kimage segments to PoC.
 */
static void kexec_segment_flush(const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("%s:\n", __func__);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("  segment[%lu]: %016lx - %016lx, 0x%lx bytes, %lu pages\n",
			i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz,
			kimage->segment[i].memsz,
			kimage->segment[i].memsz /  PAGE_SIZE);

		__flush_dcache_area(phys_to_virt(kimage->segment[i].mem),
			kimage->segment[i].memsz);
	}
}

/**
 * machine_kexec - Do the kexec reboot.
 *
 * Called from the core kexec code for a sys_reboot with LINUX_REBOOT_CMD_KEXEC.
 */
void machine_kexec(struct kimage *kimage)
{
	bool in_kexec_crash = (kimage == kexec_crash_image);
	bool stuck_cpus = cpus_are_stuck_in_kernel();

	/*
	 * New cpus may have become stuck_in_kernel after we loaded the image.
	 */
	BUG_ON(!in_kexec_crash && (stuck_cpus || (num_online_cpus() > 1)));
	WARN(in_kexec_crash && (stuck_cpus || smp_crash_stop_failed()),
		"Some CPUs may be stale, kdump will be unreliable.\n");

	/* Flush the kimage list and its buffers. */
	kexec_list_flush(kimage);

	/* Flush the new image if already in place. */
	if ((kimage != kexec_crash_image) && (kimage->head & IND_DONE))
		kexec_segment_flush(kimage);

	pr_info("Bye!\n");

	local_daif_mask();

	/*
	 * cpu_soft_restart will shutdown the MMU, disable data caches, then
	 * transfer control to the kern_reloc which contains a copy of
	 * the arm64_relocate_new_kernel routine.  arm64_relocate_new_kernel
	 * uses physical addressing to relocate the new image to its final
	 * position and transfers control to the image entry point when the
	 * relocation is complete.
	 * In kexec case, kimage->start points to purgatory assuming that
	 * kernel entry and dtb address are embedded in purgatory by
	 * userspace (kexec-tools).
	 * In kexec_file case, the kernel starts directly without purgatory.
	 */
	cpu_soft_restart(kimage->arch.kern_reloc, kimage->head, kimage->start,
			 kimage->arch.dtb_mem);

	BUG(); /* Should never get here. */
}

/**
 * sys_rros_restore_thread - Restore thread state from DTS reserved memory
 * 
 * This system call restores critical thread data (migration_threads pointer,
 * stack, and CPU context) from the DTS reserved memory region after a crash.
 * 
 * Returns:
 *   0 on success
 *   -ENOMEM on memory allocation failure  
 *   -EFAULT on memory mapping failure
 *   -ENODATA if no crash kernel data is found
 */
SYSCALL_DEFINE0(rros_restore_thread)
{
	void *src_vaddr;
	void *dst_vaddr;
	size_t stack_size = 16384;
	size_t context_size = sizeof(struct cpu_context);
	size_t is_crash_kernel_offset = sizeof(void*) + stack_size + context_size;
	size_t total_size = sizeof(void*) + stack_size + context_size + sizeof(bool);
	bool crash_kernel_flag = false;
	
	/* Check if caller has appropriate privileges */
	if (!capable(CAP_SYS_ADMIN)) {
		pr_err("rros_restore_thread: Permission denied\n");
		return -EPERM;
	}
	
	/* Allocate memory for migration_threads and is_crash_kernel flag */
	migration_threads = kmalloc(sizeof(struct task_struct), GFP_KERNEL);
	if (!migration_threads) {
		pr_err("rros_restore_thread: Failed to allocate migration_threads\n");
		return -ENOMEM;
	}

	/* Allocate stack for migration_threads */
	((struct task_struct*)migration_threads)->stack = kmalloc(stack_size, GFP_KERNEL);
	
	/* Map DTS reserved memory */
	src_vaddr = memremap(RESERVED_PHYS_MEM_ADDR, total_size, MEMREMAP_WB);
	if (!src_vaddr) {
		pr_err("rros_restore_thread: Failed to map DTS reserved memory at 0x%llx (size: %zu)\n", 
			RESERVED_PHYS_MEM_ADDR, total_size);
		kfree(migration_threads);
		migration_threads = NULL;
		return -EFAULT;
	}

	/*
	 * Restore content from DTS reserved memory (0x50000000-0x51000000)
	 * Memory layout:
	 * +0x00000000: migration_threads pointer  (8 bytes)
	 * +0x00000008: thread stack              (16384 bytes)
	 * +0x00004008: cpu_context               (sizeof(struct cpu_context) bytes)
	 * +0x0000406C: is_crash_kernel flag      (1 byte)
	 */
	
	/* Read crash kernel flag first */
	memcpy(&crash_kernel_flag, src_vaddr + is_crash_kernel_offset, sizeof(bool));
	pr_info("rros_restore_thread: crash_kernel_flag is %d\n", crash_kernel_flag);
	
	if (!crash_kernel_flag) {
		pr_info("rros_restore_thread: No crash kernel data found in reserved memory\n");
		memunmap(src_vaddr);
		kfree(migration_threads);
		migration_threads = NULL;
		return -ENODATA;
	}
	
	/* Set global flag */
	is_crash_kernel = true;
	
	/* Restore migration_threads pointer */
	// FIXME: Do this later, currently migration_threads pointer is invalid.
	// memcpy(&migration_threads, src_vaddr, sizeof(void*));
	// pr_info("rros_restore_thread: migration_threads pointer: 0x%16llx\n", (u64)migration_threads);
	
	/* Validate the pointer */
	if (!migration_threads) {
		pr_err("rros_restore_thread: Invalid migration_threads pointer\n");
		memunmap(src_vaddr);
		return -EINVAL;
	}
	
	/* Restore thread stack */
	dst_vaddr = (void*)((struct task_struct*)migration_threads)->stack;
	if (!dst_vaddr) {
		pr_err("rros_restore_thread: Invalid stack pointer\n");
		memunmap(src_vaddr);
		return -EINVAL;
	}
	
	memcpy(dst_vaddr, src_vaddr + sizeof(void*), stack_size);
	pr_info("rros_restore_thread: Thread stack restored, first word: 0x%lx\n", 
		*(unsigned long*)((struct task_struct*)migration_threads)->stack);

	/* Restore CPU context */
	dst_vaddr = (void*)&((struct task_struct*)migration_threads)->thread.cpu_context;
	memcpy(dst_vaddr, src_vaddr + sizeof(void*) + stack_size, context_size);
	pr_info("rros_restore_thread: CPU context restored, sp: 0x%lx\n", 
		(unsigned long)((struct task_struct*)migration_threads)->thread.cpu_context.sp);
	
	/* Cleanup */
	memunmap(src_vaddr);
	
	pr_info("rros_restore_thread: Thread restoration completed successfully\n");
	return 0;
}

static void machine_kexec_mask_interrupts(void)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_chip *chip;
		int ret;

		chip = irq_desc_get_chip(desc);
		if (!chip)
			continue;

		/*
		 * First try to remove the active state. If this
		 * fails, try to EOI the interrupt.
		 */
		ret = irq_set_irqchip_state(i, IRQCHIP_STATE_ACTIVE, false);

		if (ret && irqd_irq_inprogress(&desc->irq_data) &&
		    chip->irq_eoi)
			chip->irq_eoi(&desc->irq_data);

		if (chip->irq_mask)
			chip->irq_mask(&desc->irq_data);

		if (chip->irq_disable && !irqd_irq_disabled(&desc->irq_data))
			chip->irq_disable(&desc->irq_data);
	}
}

/**
 * machine_crash_shutdown - shutdown non-crashing cpus and save registers
 */
void machine_crash_shutdown(struct pt_regs *regs)
{
	local_irq_disable();

	/* shutdown non-crashing cpus */
	crash_smp_send_stop();

	/* for crashing cpu */
	crash_save_cpu(regs, smp_processor_id());
	machine_kexec_mask_interrupts();

	pr_info("Starting crashdump kernel...\n");
}

void arch_kexec_protect_crashkres(void)
{
	int i;

	kexec_segment_flush(kexec_crash_image);

	for (i = 0; i < kexec_crash_image->nr_segments; i++)
		set_memory_valid(
			__phys_to_virt(kexec_crash_image->segment[i].mem),
			kexec_crash_image->segment[i].memsz >> PAGE_SHIFT, 0);
}

void arch_kexec_unprotect_crashkres(void)
{
	int i;

	for (i = 0; i < kexec_crash_image->nr_segments; i++)
		set_memory_valid(
			__phys_to_virt(kexec_crash_image->segment[i].mem),
			kexec_crash_image->segment[i].memsz >> PAGE_SHIFT, 1);
}

#ifdef CONFIG_HIBERNATION
/*
 * To preserve the crash dump kernel image, the relevant memory segments
 * should be mapped again around the hibernation.
 */
void crash_prepare_suspend(void)
{
	if (kexec_crash_image)
		arch_kexec_unprotect_crashkres();
}

void crash_post_resume(void)
{
	if (kexec_crash_image)
		arch_kexec_protect_crashkres();
}

/*
 * crash_is_nosave
 *
 * Return true only if a page is part of reserved memory for crash dump kernel,
 * but does not hold any data of loaded kernel image.
 *
 * Note that all the pages in crash dump kernel memory have been initially
 * marked as Reserved as memory was allocated via memblock_reserve().
 *
 * In hibernation, the pages which are Reserved and yet "nosave" are excluded
 * from the hibernation iamge. crash_is_nosave() does thich check for crash
 * dump kernel and will reduce the total size of hibernation image.
 */

bool crash_is_nosave(unsigned long pfn)
{
	int i;
	phys_addr_t addr;

	if (!crashk_res.end)
		return false;

	/* in reserved memory? */
	addr = __pfn_to_phys(pfn);
	if ((addr < crashk_res.start) || (crashk_res.end < addr))
		return false;

	if (!kexec_crash_image)
		return true;

	/* not part of loaded kernel image? */
	for (i = 0; i < kexec_crash_image->nr_segments; i++)
		if (addr >= kexec_crash_image->segment[i].mem &&
				addr < (kexec_crash_image->segment[i].mem +
					kexec_crash_image->segment[i].memsz))
			return false;

	return true;
}

void crash_free_reserved_phys_range(unsigned long begin, unsigned long end)
{
	unsigned long addr;
	struct page *page;

	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		page = phys_to_page(addr);
		free_reserved_page(page);
	}
}
#endif /* CONFIG_HIBERNATION */
