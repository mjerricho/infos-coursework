/*
 * Buddy Page Allocation Algorithm
 * SKELETON IMPLEMENTATION -- TO BE FILLED IN FOR TASK (2)
 */

/*
 * STUDENT NUMBER: s
 */
#include <infos/mm/page-allocator.h>
#include <infos/mm/mm.h>
#include <infos/kernel/kernel.h>
#include <infos/kernel/log.h>
#include <infos/util/math.h>
#include <infos/util/printf.h>

using namespace infos::kernel;
using namespace infos::mm;
using namespace infos::util;

#define MAX_ORDER	17

/**
 * A buddy page allocation algorithm.
 */
class BuddyPageAllocator : public PageAllocatorAlgorithm
{
private:
  /**
   * Returns the number of pages that comprise a 'block', in a given order.
   * @param order The order to base the calculation off of.
   * @return Returns the number of pages in a block, in the order.
   */
  static inline constexpr uint64_t pages_per_block(int order)
  {
    /* The number of pages per block in a given order is simply 1, shifted left by the order number.
     * For example, in order-2, there are (1 << 2) == 4 pages in each block.
     */
    return (1 << order);
  }
	
  /**
   * Returns TRUE if the supplied page descriptor is correctly aligned for the 
   * given order.  Returns FALSE otherwise.
   * @param pgd The page descriptor to test alignment for.
   * @param order The order to use for calculations.
   */
  static inline bool is_correct_alignment_for_order(const PageDescriptor *pgd, int order)
  {
    // Calculate the page-frame-number for the page descriptor, and return TRUE if
    // it divides evenly into the number pages in a block of the given order.
    return (sys.mm().pgalloc().pgd_to_pfn(pgd) % pages_per_block(order)) == 0;
  }
	
  /** Given a page descriptor, and an order, returns the buddy PGD.  The buddy could either be
   * to the left or the right of PGD, in the given order.
   * @param pgd The page descriptor to find the buddy for.
   * @param order The order in which the page descriptor lives.
   * @return Returns the buddy of the given page descriptor, in the given order.
   */
  PageDescriptor *buddy_of(PageDescriptor *pgd, int order)
  {
    // (1) Make sure 'order' is within range
    if (order >= MAX_ORDER) {
      return NULL;
    }

    // (2) Check to make sure that PGD is correctly aligned in the order
    if (!is_correct_alignment_for_order(pgd, order)) {
      return NULL;
    }
				
    // (3) Calculate the page-frame-number of the buddy of this page.
    // * If the PFN is aligned to the next order, then the buddy is the next block in THIS order.
    // * If it's not aligned, then the buddy must be the previous block in THIS order.
    uint64_t buddy_pfn = is_correct_alignment_for_order(pgd, order + 1) ?
      sys.mm().pgalloc().pgd_to_pfn(pgd) + pages_per_block(order) : 
      sys.mm().pgalloc().pgd_to_pfn(pgd) - pages_per_block(order);
		
    // (4) Return the page descriptor associated with the buddy page-frame-number.
    return sys.mm().pgalloc().pfn_to_pgd(buddy_pfn);
  }
	
  /**
   * Inserts a block into the free list of the given order.  The block is inserted in ascending order.
   * @param pgd The page descriptor of the block to insert.
   * @param order The order in which to insert the block.
   * @return Returns the slot (i.e. a pointer to the pointer that points to the block) that the block
   * was inserted into.
   */
  PageDescriptor **insert_block(PageDescriptor *pgd, int order)
  {
    // Starting from the _free_area array, find the slot in which the page descriptor
    // should be inserted.
    PageDescriptor **slot = &_free_areas[order];
		
    // Iterate whilst there is a slot, and whilst the page descriptor pointer is numerically
    // greater than what the slot is pointing to.
    while (*slot && pgd > *slot) {
      slot = &(*slot)->next_free;
    }
		
    // Insert the page descriptor into the linked list.
    pgd->next_free = *slot;
    *slot = pgd;
		
    // Return the insert point (i.e. slot)
    return slot;
  }
	
  /**
   * Removes a block from the free list of the given order.  The block MUST be present in the free-list, otherwise
   * the system will panic.
   * @param pgd The page descriptor of the block to remove.
   * @param order The order in which to remove the block from.
   */
  void remove_block(PageDescriptor *pgd, int order)
  {
    // Starting from the _free_area array, iterate until the block has been located in the linked-list.
    PageDescriptor **slot = &_free_areas[order];
    while (*slot && pgd != *slot) {
      slot = &(*slot)->next_free;
    }

    // Make sure the block actually exists.  Panic the system if it does not.
    assert(*slot == pgd);
		
    // Remove the block from the free list.
    *slot = pgd->next_free;
    pgd->next_free = NULL;
  }
	
  /**
   * Given a pointer to a block of free memory in the order "source_order", this function will
   * split the block in half, and insert it into the order below.
   * @param block_pointer A pointer to a pointer containing the beginning of a block of free memory.
   * @param source_order The order in which the block of free memory exists.  Naturally,
   * the split will insert the two new blocks into the order below.
   * @return Returns the left-hand-side of the new block.
   */
  PageDescriptor *split_block(PageDescriptor **block_pointer, int source_order)
  {
    // Make sure there is an incoming pointer.
    assert(*block_pointer);
		
    // Make sure the block_pointer is correctly aligned.
    assert(is_correct_alignment_for_order(*block_pointer, source_order));

    // Make sure that the source order > 0
    assert(source_order > 0);
    assert(source_order <= MAX_ORDER);
    
    // mm_log.messagef(LogLevel::DEBUG, "SPLIT_BLOCK: Splitting block, pd=%p, source order=%d", block_pointer, source_order);
    // dump_state();
    int target_order = source_order - 1;
    PageDescriptor *left_block = *block_pointer;
    PageDescriptor *right_block = buddy_of(*block_pointer, target_order);

    // Make sure that left_block < right_block
    assert(left_block < right_block);
		
    // remove block and add new ones
    remove_block(*block_pointer, source_order);
    insert_block(left_block, target_order);
    insert_block(right_block, target_order);

    // mm_log.messagef(LogLevel::DEBUG, "SPLIT_BLOCK: Finished splitting block, pd=%p", left_block);
    // dump_state();
    return left_block;
  }
	
  /**
   * Takes a block in the given source order, and merges it (and it's buddy) into the next order.
   * This function assumes both the source block and the buddy block are in the free list for the
   * source order.  If they aren't this function will panic the system.
   * @param block_pointer A pointer to a pointer containing a block in the pair to merge.
   * @param source_order The order in which the pair of blocks live.
   * @return Returns the new slot that points to the merged block.
   */
  PageDescriptor **merge_block(PageDescriptor **block_pointer, int source_order)
  {
    assert(*block_pointer);
		
    // Make sure the area_pointer is correctly aligned.
    assert(is_correct_alignment_for_order(*block_pointer, source_order));
		
    // mm_log.messagef(LogLevel::DEBUG, "MERGE_BLOCK: Merging block, pd=%p, source order=%d", block_pointer, source_order);
    // dump_state();
		
    int target_order = source_order + 1;
    PageDescriptor *left_block = *block_pointer;
    PageDescriptor *right_block = buddy_of(*block_pointer, source_order);

    // Make sure that left_block < right_block by writing a swap funtion
    if (left_block > right_block) {
      PageDescriptor *tmp = right_block;
      right_block = left_block;
      left_block = tmp;
    }
    
    assert(left_block < right_block);
		
    // remove block and add new ones
    remove_block(left_block, source_order);
    remove_block(right_block, source_order);
    PageDescriptor **res = insert_block(left_block, target_order);

    // mm_log.messagef(LogLevel::DEBUG, "MERGE_BLOCK: Finished merging block, pd=%p", left_block);
    // dump_state();
    return res;
  }
	
public:
  /**
   * Constructs a new instance of the Buddy Page Allocator.
   */
  BuddyPageAllocator() {
    // Iterate over each free area, and clear it.
    for (unsigned int i = 0; i < ARRAY_SIZE(_free_areas); i++) {
      _free_areas[i] = NULL;
    }
  }
	
  /**
   * Allocates 2^order number of contiguous pages
   * @param order The power of two, of the number of contiguous pages to allocate.
   * @return Returns a pointer to the first page descriptor for the newly allocated page range, or NULL if
   * allocation failed.
   */
  PageDescriptor *alloc_pages(int target_order) override
  {
    // mm_log.messagef(LogLevel::DEBUG, "ALLOC_PAGES: Allocating pages at target order=%d", target_order);
    // dump_state();

    // Making sure that the order is within acceptable range
    assert(target_order >= 0);
    assert(target_order <= MAX_ORDER);

    // start with creating the variables to store the current order and target order
    int current_order = target_order;

    // store the free block
    PageDescriptor *free_block = _free_areas[current_order];

    // while loop to split larger blocks if free_block is NULL
    while (!free_block || current_order > target_order) {
      // if there are no larger blocks to split
      if (current_order > MAX_ORDER || current_order < 0) {
	mm_log.messagef(LogLevel::DEBUG, "ALLOC_PAGES: [OUT-OF-MEMORY] No more larger block to split");
	return nullptr;
      }
	    	    
      if (_free_areas[current_order]) {
	// split larger blocks
	// mm_log.messagef(LogLevel::DEBUG, "ALLOC_PAGES: Splitting larger block at order %d", current_order);
	free_block = split_block(&_free_areas[current_order], current_order);
	current_order--;
	// mm_log.messagef(LogLevel::DEBUG, "ALLOC_PAGES: Block at order %d is split", current_order);
      } else {
	// get to larger blocks
	current_order++;
      }	    
    }
    // remove the block from the free areas
    remove_block(free_block, target_order);
    // mm_log.messagef(LogLevel::DEBUG, "ALLOC_PAGES: Page allocated at %p order %d", free_block, target_order);
    // dump_state();
    return free_block;
  }

  /**
   * Check if page is free.
   * @param pgd A pointer to an array of page descriptors
   * @param order The order of the page descriptor     
   */
  bool is_page_free(PageDescriptor *pgd, int order)
  {
    // Make sure that order is within range
    assert(order >= 0);
    assert(order <= MAX_ORDER);

    PageDescriptor **slot = &_free_areas[order];
    while (*slot) {
      if (*slot == pgd) { return true; }
      slot = &(*slot)->next_free;
    }
    return false;
  }
      
	
  /**
   * Frees 2^order contiguous pages.
   * @param pgd A pointer to an array of page descriptors to be freed.
   * @param order The power of two number of contiguous pages to free.
   */
  void free_pages(PageDescriptor *pgd, int order) override
  {
    // Make sure that the incoming page descriptor is correctly aligned
    // for the order on which it is being freed, for example, it is
    //  mm_log.messagef(LogLevel::DEBUG, "FREE_PAGES: freeing page at pgd=%p, order=%d", pgd, order);
    // dump_state();
    
    // illegal to free page 1 in order-1.
    assert(is_correct_alignment_for_order(pgd, order));

    // Make sure that order is within range
    assert(order >= 0);
    assert(order <= MAX_ORDER);

    int current_order = order;
    PageDescriptor **block_pointer = insert_block(pgd, order);
    PageDescriptor *buddy = buddy_of(pgd, order);

    while (buddy && is_page_free(buddy, current_order) && (current_order < MAX_ORDER - 1)) {
      block_pointer = merge_block(block_pointer, current_order);
      current_order++;
      buddy = buddy_of(*block_pointer, current_order);
    }
    // mm_log.messagef(LogLevel::DEBUG, "FREE_PAGES: Pages freed and merged at pgd: %p order: %d", pgd, order);
    // dump_state();
  }
  
  /**
   * Get the block pointer, assuming that the block is free.
   * @param pgd A pointer to an array of page descriptors.
   * @param order The order of the page descriptor.
   */
  PageDescriptor **get_block_pointer(PageDescriptor *pgd, int order)
  {
    // Make sure that order is within range
    assert(order >= 0);
    assert(order <= MAX_ORDER);

    PageDescriptor **slot = &_free_areas[order];
    while (*slot) {
      if (*slot == pgd) { return slot; }
      slot = &(*slot)->next_free;
    }
    return NULL;
  }
  
  /**
   * Reserves a specific page, so that it cannot be allocated.
   * @param pgd The page descriptor of the page to reserve.
   * @return Returns TRUE if the reservation was successful, FALSE otherwise.
   */
  bool reserve_page(PageDescriptor *pgd)
  {
    int current_order = MAX_ORDER - 1;
    while (current_order > 0) {
      uint64_t ppb = pages_per_block(current_order);
      uint64_t pfn = sys.mm().pgalloc().pgd_to_pfn(pgd);
      uint64_t num_blocks = pfn / ppb;
      uint64_t block = num_blocks * ppb;
      PageDescriptor *pgd_block = sys.mm().pgalloc().pfn_to_pgd(block);
       
      if (is_page_free(pgd_block, current_order)) {
	PageDescriptor **pointer_block = get_block_pointer(pgd_block, current_order);
	split_block(pointer_block, current_order);
	}
      current_order--;
    }
    if (is_page_free(pgd, 0)) {
      remove_block(pgd, 0);
      return true;
    } else {
      return false;
    }
  }
  
  /**
   * Initialises the allocation algorithm.
   * @return Returns TRUE if the algorithm was successfully initialised, FALSE otherwise.
   */
  bool init(PageDescriptor *page_descriptors, uint64_t nr_page_descriptors) override
  {
    mm_log.messagef(LogLevel::DEBUG, "Buddy Allocator Initialising pd=%p, nr=0x%lx", page_descriptors, nr_page_descriptors);
    dump_state();
    uint64_t ppb = pages_per_block(MAX_ORDER - 1);
    uint64_t num_blocks = nr_page_descriptors / ppb;
    // inserting blocks
    for (unsigned int i = 0; i < num_blocks; i++) {
      insert_block(page_descriptors + (ppb * i), (MAX_ORDER - 1));
    }
    mm_log.messagef(LogLevel::DEBUG, "INIT: done initialising buddy algorithm");
    dump_state();
    return true;
  }
  
  /**
   * Returns the friendly name of the allocation algorithm, for debugging and selection purposes.
   */
  const char* name() const override { return "buddy"; }
	
  /**
   * Dumps out the current state of the buddy system
   */
  void dump_state() const override
  {
    // Print out a header, so we can find the output in the logs.
    mm_log.messagef(LogLevel::DEBUG, "BUDDY STATE:");
		
    // Iterate over each free area.
    for (unsigned int i = 0; i < ARRAY_SIZE(_free_areas); i++) {
      char buffer[256];
      snprintf(buffer, sizeof(buffer), "[%d] ", i);
						
      // Iterate over each block in the free area.
      PageDescriptor *pg = _free_areas[i];
      while (pg) {
	// Append the PFN of the free block to the output buffer.
	snprintf(buffer, sizeof(buffer), "%s%lx ", buffer, sys.mm().pgalloc().pgd_to_pfn(pg));
	pg = pg->next_free;
      }
			
      mm_log.messagef(LogLevel::DEBUG, "%s", buffer);
    }
  }

	
private:
  PageDescriptor *_free_areas[MAX_ORDER];
};

/* --- DO NOT CHANGE ANYTHING BELOW THIS LINE --- */

/*
 * Allocation algorithm registration framework
 */
RegisterPageAllocator(BuddyPageAllocator);

  
