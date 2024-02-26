.. _page_owner:

==================================================
page owner: Tracking about who allocated each page
==================================================

Introduction
============

page owner is for the tracking about who allocated each page.
It can be used to debug memory leak or to find a memory hogger.
When allocation happens, information about allocation such as call stack
and order of pages is stored into certain storage for each page.
When we need to know about status of all pages, we can get and analyze
this information.

Although we already have tracepoint for tracing page allocation/free,
using it for analyzing who allocate each page is rather complex. We need
to enlarge the trace buffer for preventing overlapping until userspace
program launched. And, launched program continually dump out the trace
buffer for later analysis and it would change system behaviour with more
possibility rather than just keeping it in memory, so bad for debugging.

page owner can also be used for various purposes. For example, accurate
fragmentation statistics can be obtained through gfp flag information of
each page. It is already implemented and activated if page owner is
enabled. Other usages are more than welcome.

It can also be used to show all the stacks and their outstanding
allocations, which gives us a quick overview of where the memory is going
without the need to screen through all the pages and match the allocation
and free operation.

page owner is disabled in default. So, if you'd like to use it, you need
to add "page_owner=on" into your boot cmdline. If the kernel is built
with page owner and page owner is disabled in runtime due to no enabling
boot option, runtime overhead is marginal. If disabled in runtime, it
doesn't require memory to store owner information, so there is no runtime
memory overhead. And, page owner inserts just two unlikely branches into
the page allocator hotpath and if not enabled, then allocation is done
like as the kernel without page owner. These two unlikely branches should
not affect to allocation performance, especially if the static keys jump
label patching functionality is available. Following is the kernel's code
size change due to this facility.

- Without page owner::

   text    data     bss     dec     hex filename
   48392   2333     644   51369    c8a9 mm/page_alloc.o

- With page owner::

   text    data     bss     dec     hex filename
   48800   2445     644   51889    cab1 mm/page_alloc.o
   6662     108      29    6799    1a8f mm/page_owner.o
   1025       8       8    1041     411 mm/page_ext.o

Although, roughly, 8 KB code is added in total, page_alloc.o increase by
520 bytes and less than half of it is in hotpath. Building the kernel with
page owner and turning it on if needed would be great option to debug
kernel memory problem.

There is one notice that is caused by implementation detail. page owner
stores information into the memory from struct page extension. This memory
is initialized some time later than that page allocator starts in sparse
memory system, so, until initialization, many pages can be allocated and
they would have no owner information. To fix it up, these early allocated
pages are investigated and marked as allocated in initialization phase.
Although it doesn't mean that they have the right owner information,
at least, we can tell whether the page is allocated or not,
more accurately. On 2GB memory x86-64 VM box, 13343 early allocated pages
are catched and marked, although they are mostly allocated from struct
page extension feature. Anyway, after that, no page is left in
un-tracking state.

Usage
=====

1) Build user-space helper::

	cd tools/vm
	make page_owner_sort

2) Enable page owner: add "page_owner=on" to boot cmdline.

3) Do the job what you want to debug

4) Analyze information from page owner::

	cat /sys/kernel/debug/page_owner_stacks/show_stacks > stacks.txt
	cat stacks.txt
	 prep_new_page+0xa9/0x120
	 get_page_from_freelist+0x7e6/0x2140
	 __alloc_pages+0x18a/0x370
	 new_slab+0xc8/0x580
	 ___slab_alloc+0x1f2/0xaf0
	 __slab_alloc.isra.86+0x22/0x40
	 kmem_cache_alloc+0x31b/0x350
	 __khugepaged_enter+0x39/0x100
	 dup_mmap+0x1c7/0x5ce
	 copy_process+0x1afe/0x1c90
	 kernel_clone+0x9a/0x3c0
	 __do_sys_clone+0x66/0x90
	 do_syscall_64+0x7f/0x160
	 entry_SYSCALL_64_after_hwframe+0x6c/0x74
	stack_count: 234
	...
	...
	echo 7000 > /sys/kernel/debug/page_owner_stacks/count_threshold
	cat /sys/kernel/debug/page_owner_stacks/show_stacks> stacks_7000.txt
	cat stacks_7000.txt
	 prep_new_page+0xa9/0x120
	 get_page_from_freelist+0x7e6/0x2140
	 __alloc_pages+0x18a/0x370
	 alloc_pages_mpol+0xdf/0x1e0
	 folio_alloc+0x14/0x50
	 filemap_alloc_folio+0xb0/0x100
	 page_cache_ra_unbounded+0x97/0x180
	 filemap_fault+0x4b4/0x1200
	 __do_fault+0x2d/0x110
	 do_pte_missing+0x4b0/0xa30
	 __handle_mm_fault+0x7fa/0xb70
	 handle_mm_fault+0x125/0x300
	 do_user_addr_fault+0x3c9/0x840
	 exc_page_fault+0x68/0x150
	 asm_exc_page_fault+0x22/0x30
	stack_count: 8248
	...

	cat /sys/kernel/debug/page_owner > page_owner_full.txt
	./page_owner_sort page_owner_full.txt sorted_page_owner.txt

   See the result about who allocated each page
   in the ``sorted_page_owner.txt``.
