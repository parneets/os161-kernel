#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <array.h>
#include <uw-vmstats.h>
#include "opt-A3.h"

#if OPT_A3

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

	as->isLoaded = false;
	as->pt = array_create();

	return as;
}

void
as_destroy(struct addrspace *as)
{
	kfree(as);
	// array_destroy(as->pt);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	struct pte * e;
	for (unsigned int i = 0; i < npages; i++)
	{
		e = kmalloc(sizeof(struct pte));
		e->vaddr = vaddr + i * PAGE_SIZE;
		e->flags = readable | writeable | executable;
		e->valid = 0;
		array_add(as->pt, e, NULL);
	}

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;

	as->isLoaded = true;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0); // DIFF

	struct pte * e;
	int i;

	for (i = 0; i < DUMBVM_STACKPAGES; i++)
	{
		e = kmalloc(sizeof(struct pte));
		e->vaddr = USERSTACK - i * PAGE_SIZE;
		e->flags = 0x7;
		e->valid = 0;
		array_add(as->pt, e, NULL);
	}

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	// if (as_prepare_load(new)) {
	// 	as_destroy(new);
	// 	return ENOMEM;
	// }

	// KASSERT(new->as_pbase1 != 0);
	// KASSERT(new->as_pbase2 != 0);
	// KASSERT(new->as_stackpbase != 0);

	unsigned int i;
	struct pte * e;

	new->as_pbase1 = getppages(new->as_npages1);
	if (new->as_pbase1 == 0) {
		return ENOMEM;
	}

	for (i = 0; i < new->as_npages1; i++)
	{
		e = kmalloc(sizeof(struct pte));
		e->vaddr = new->as_vbase1 + i * PAGE_SIZE;
		e->paddr = new->as_pbase1 + i * PAGE_SIZE;
		e->valid = 1;
		e->flags = 0x5;
		array_add(new->pt, e, NULL);
	}

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	new->as_pbase2 = getppages(new->as_npages2);
	if (new->as_pbase2 == 0) {
		return ENOMEM;
	}

	for (i = 0; i < new->as_npages2; i++)
	{
		e = kmalloc(sizeof(struct pte));
		e->vaddr = new->as_vbase2 + i * PAGE_SIZE;
		e->paddr = new->as_pbase2 + i * PAGE_SIZE;
		e->valid = 1;
		e->flags = 0x6;
		array_add(new->pt, e, NULL);
	}
	
	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	new->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (new->as_stackpbase == 0) {
		return ENOMEM;
	}

	for (i = 0; i < DUMBVM_STACKPAGES; i++)
	{
		e = kmalloc(sizeof(struct pte));
		e->vaddr = USERSTACK - i * PAGE_SIZE;
		e->paddr = new->as_stackpbase + i * PAGE_SIZE;
		e->valid = 1;
		e->flags = 0x7;
		array_add(new->pt, e, NULL);
	}

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}


#endif
