/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H
#include <linux/bottom_half.h>

#include <asm/fpu/types.h>

/*
 * Use kernel_fpu_begin/end() if you intend to use FPU in kernel context. It
 * disables preemption and softirq processing, so be careful if you intend to
 * use it for long periods of time.  Kernel-mode FPU cannot be used in all
 * contexts -- see irq_fpu_usable() for details.
 */

/* Kernel FPU states to initialize in kernel_fpu_begin_mask() */
#define KFPU_387	_BITUL(0)	/* 387 state will be initialized */
#define KFPU_MXCSR	_BITUL(1)	/* MXCSR will be initialized */

extern void kernel_fpu_begin_mask(unsigned int kfpu_mask);
extern void kernel_fpu_end(void);
extern bool irq_fpu_usable(void);
extern void fpregs_mark_activate(void);

/* Code that is unaware of kernel_fpu_begin_mask() can use this */
static inline void kernel_fpu_begin(void)
{
#ifdef CONFIG_X86_64
	/*
	 * Any 64-bit code that uses 387 instructions must explicitly request
	 * KFPU_387.
	 */
	kernel_fpu_begin_mask(KFPU_MXCSR);
#else
	/*
	 * 32-bit kernel code may use 387 operations as well as SSE2, etc,
	 * as long as it checks that the CPU has the required capability.
	 */
	kernel_fpu_begin_mask(KFPU_387 | KFPU_MXCSR);
#endif
}

/*
 * Use fpregs_lock() while editing CPU's FPU registers or fpu->fpstate, or while
 * using the FPU in kernel mode.  A context switch will (and softirq might) save
 * CPU's FPU registers to fpu->fpstate.regs and set TIF_NEED_FPU_LOAD leaving
 * CPU's FPU registers in a random state.
 *
 * local_bh_disable() protects against both preemption and soft interrupts
 * on !RT kernels.
 *
 * On RT kernels local_bh_disable() is not sufficient because it only
 * serializes soft interrupt related sections via a local lock, but stays
 * preemptible. Disabling preemption is the right choice here as bottom
 * half processing is always in thread context on RT kernels so it
 * implicitly prevents bottom half processing as well.
 */
static inline void fpregs_lock(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_disable();
	else
		preempt_disable();
}

static inline void fpregs_unlock(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_enable();
	else
		preempt_enable();
}

/*
 * FPU state gets lazily restored before returning to userspace. So when in the
 * kernel, the valid FPU state may be kept in the buffer. This function will force
 * restore all the fpu state to the registers early if needed, and lock them from
 * being automatically saved/restored. Then FPU state can be modified safely in the
 * registers, before unlocking with fpregs_unlock().
 */
void fpregs_lock_and_load(void);

#ifdef CONFIG_X86_DEBUG_FPU
extern void fpregs_assert_state_consistent(void);
#else
static inline void fpregs_assert_state_consistent(void) { }
#endif

/*
 * Load the task FPU state before returning to userspace.
 */
extern void switch_fpu_return(void);

/*
 * Query the presence of one or more xfeatures. Works on any legacy CPU as well.
 *
 * If 'feature_name' is set then put a human-readable description of
 * the feature there as well - this can be used to print error (or success)
 * messages.
 */
extern int cpu_has_xfeatures(u64 xfeatures_mask, const char **feature_name);

/* Trap handling */
extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);
extern void fpu_sync_fpstate(struct fpu *fpu);
extern void fpu_reset_from_exception_fixup(void);

/* Boot, hotplug and resume */
extern void fpu__init_cpu(void);
extern void fpu__init_system(void);
extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);

#ifdef CONFIG_MATH_EMULATION
extern void fpstate_init_soft(struct swregs_state *soft);
#else
static inline void fpstate_init_soft(struct swregs_state *soft) {}
#endif

/* State tracking */
DECLARE_PER_CPU(bool, kernel_fpu_allowed);
DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

/* Process cleanup */
#ifdef CONFIG_X86_64
extern void fpstate_free(struct fpu *fpu);
#else
static inline void fpstate_free(struct fpu *fpu) { }
#endif

/* fpstate-related functions which are exported to KVM */
extern void fpstate_clear_xstate_component(struct fpstate *fpstate, unsigned int xfeature);

extern u64 xstate_get_guest_group_perm(void);

extern void *get_xsave_addr(struct xregs_state *xsave, int xfeature_nr);


/* KVM specific functions */
extern bool fpu_alloc_guest_fpstate(struct fpu_guest *gfpu);
extern void fpu_free_guest_fpstate(struct fpu_guest *gfpu);
extern int fpu_swap_kvm_fpstate(struct fpu_guest *gfpu, bool enter_guest);
extern int fpu_enable_guest_xfd_features(struct fpu_guest *guest_fpu, u64 xfeatures);

#ifdef CONFIG_X86_64
extern void fpu_update_guest_xfd(struct fpu_guest *guest_fpu, u64 xfd);
extern void fpu_sync_guest_vmexit_xfd_state(void);
#else
static inline void fpu_update_guest_xfd(struct fpu_guest *guest_fpu, u64 xfd) { }
static inline void fpu_sync_guest_vmexit_xfd_state(void) { }
#endif

extern void fpu_copy_guest_fpstate_to_uabi(struct fpu_guest *gfpu, void *buf,
					   unsigned int size, u64 xfeatures, u32 pkru);
extern int fpu_copy_uabi_to_guest_fpstate(struct fpu_guest *gfpu, const void *buf, u64 xcr0, u32 *vpkru);

static inline void fpstate_set_confidential(struct fpu_guest *gfpu)
{
	gfpu->fpstate->is_confidential = true;
}

static inline bool fpstate_is_confidential(struct fpu_guest *gfpu)
{
	return gfpu->fpstate->is_confidential;
}

/* prctl */
extern long fpu_xstate_prctl(int option, unsigned long arg2);

extern void fpu_idle_fpregs(void);

#endif /* _ASM_X86_FPU_API_H */
