/*
 * JFFS3 -- Journalling Flash File System, Version 3.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 * Copyright (C) 2004 Thomas Gleixner <tglx@linutronix.de>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 * Modified debugged and enhanced by Thomas Gleixner <tglx@linutronix.de>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * JFFS2 Id: wbuf.c,v 1.82 2004/11/20 22:08:31 dwmw2  Exp
 * $Id: wbuf.c,v 3.16 2005/02/09 14:52:05 pavlov Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/crc32.h>
#include <linux/mtd/nand.h>
#include "nodelist.h"
#include "summary.h"

/* max. erase failures before we mark a block bad */
#define MAX_ERASE_FAILURES 	2

/* two seconds timeout for timed wbuf-flushing */
#define WBUF_FLUSH_TIMEOUT	2 * HZ

struct jffs3_inodirty {
	uint32_t ino;
	struct jffs3_inodirty *next;
};

static struct jffs3_inodirty inodirty_nomem;

static int jffs3_wbuf_pending_for_ino(struct jffs3_sb_info *c, uint32_t ino)
{
	struct jffs3_inodirty *this = c->wbuf_inodes;

	/* If a malloc failed, consider _everything_ dirty */
	if (this == &inodirty_nomem)
		return 1;

	/* If ino == 0, _any_ non-GC writes mean 'yes' */
	if (this && !ino)
		return 1;

	/* Look to see if the inode in question is pending in the wbuf */
	while (this) {
		if (this->ino == ino)
			return 1;
		this = this->next;
	}
	return 0;
}

static void jffs3_clear_wbuf_ino_list(struct jffs3_sb_info *c)
{
	struct jffs3_inodirty *this;

	this = c->wbuf_inodes;

	if (this != &inodirty_nomem) {
		while (this) {
			struct jffs3_inodirty *next = this->next;
			kfree(this);
			this = next;
		}
	}
	c->wbuf_inodes = NULL;
}

static void jffs3_wbuf_dirties_inode(struct jffs3_sb_info *c, uint32_t ino)
{
	struct jffs3_inodirty *new;

	/* Mark the superblock dirty so that kupdated will flush... */
	OFNI_BS_2SFFJ(c)->s_dirt = 1;

	if (jffs3_wbuf_pending_for_ino(c, ino))
		return;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new) {
		DBG_WBUF(1, "No memory to allocate inodirty. Fallback to all considered dirty\n");
		jffs3_clear_wbuf_ino_list(c);
		c->wbuf_inodes = &inodirty_nomem;
		return;
	}
	new->ino = ino;
	new->next = c->wbuf_inodes;
	c->wbuf_inodes = new;
	return;
}

static inline void jffs3_refile_wbuf_blocks(struct jffs3_sb_info *c)
{
	struct list_head *this, *next;
	static int n;

	if (list_empty(&c->erasable_pending_wbuf_list))
		return;

	list_for_each_safe(this, next, &c->erasable_pending_wbuf_list) {
		struct jffs3_eraseblock *jeb = list_entry(this, struct jffs3_eraseblock, list);

		DBG_WBUF(1, "Removing eraseblock at 0x%08x from erasable_pending_wbuf_list...\n", jeb->offset);
		list_del(this);
		if ((jiffies + (n++)) & 127) {
			/* Most of the time, we just erase it immediately. Otherwise we
			   spend ages scanning it on mount, etc. */
			DBG_WBUF(1, "...and adding to erase_pending_list\n");
			list_add_tail(&jeb->list, &c->erase_pending_list);
			c->nr_erasing_blocks++;
			jffs3_erase_pending_trigger(c);
		} else {
			/* Sometimes, however, we leave it elsewhere so it doesn't get
			   immediately reused, and we spread the load a bit. */
			DBG_WBUF(1, "...and adding to erasable_list\n");
			list_add_tail(&jeb->list, &c->erasable_list);
		}
	}
}

#define REFILE_NOTEMPTY 0
#define REFILE_ANYWAY   1

static void jffs3_block_refile(struct jffs3_sb_info *c, struct jffs3_eraseblock *jeb, int allow_empty)
{
	DBG_WBUF(1, "About to refile bad block at %08x\n", jeb->offset);

	if (DEBUG2) {
		DBG_WBUF(2, "Blocks info:\n");
		jffs3_dbg_dump_block_lists(c);
	}
	/* File the existing block on the bad_used_list.... */
	if (c->nextblock == jeb)
		c->nextblock = NULL;
	else /* Not sure this should ever happen... need more coffee */
		list_del(&jeb->list);
	if (jeb->first_node) {
		DBG_WBUF(1, "Refiling block at %08x to bad_used_list\n", jeb->offset);
		list_add(&jeb->list, &c->bad_used_list);
	} else {
		BUG_ON(allow_empty == REFILE_NOTEMPTY);
		/* It has to have had some nodes or we couldn't be here */
		DBG_WBUF(1, "Refiling block at %08x to erase_pending_list\n", jeb->offset);
		list_add(&jeb->list, &c->erase_pending_list);
		c->nr_erasing_blocks++;
		jffs3_erase_pending_trigger(c);
	}
	if (DEBUG2) {
		DBG_WBUF(2, "Blocks info:\n");
		jffs3_dbg_dump_block_lists(c);
	}

	/* Adjust its size counts accordingly */
	c->wasted_size += jeb->free_size;
	c->free_size -= jeb->free_size;
	jeb->wasted_size += jeb->free_size;
	jeb->free_size = 0;

	jffs3_dbg_acct_sanity_check(c, jeb);
	if (PARANOIA)
		jffs3_dbg_acct_paranoia_check(c, jeb);
}

/* Recover from failure to write wbuf. Recover the nodes up to the
 * wbuf, not the one which we were starting to try to write. */

static void jffs3_wbuf_recover(struct jffs3_sb_info *c)
{
	struct jffs3_eraseblock *jeb, *new_jeb;
	struct jffs3_raw_node_ref **first_raw, **raw;
	size_t retlen;
	int ret;
	unsigned char *buf;
	uint32_t start, end, ofs, len;

	spin_lock(&c->erase_completion_lock);

	jeb = &c->blocks[c->wbuf_ofs / c->sector_size];

	jffs3_block_refile(c, jeb, REFILE_NOTEMPTY);

	/* Find the first node to be recovered, by skipping over every
	   node which ends before the wbuf starts, or which is obsolete. */
	first_raw = &jeb->first_node;
	while (*first_raw &&
	       (ref_obsolete(*first_raw) ||
		(ref_offset(*first_raw)+ref_totlen(c, jeb, *first_raw)) < c->wbuf_ofs)) {
		DBG_WBUF(1, "Skipping node at 0x%08x(%d)-0x%08x which is either before 0x%08x or obsolete\n",
			  ref_offset(*first_raw), ref_flags(*first_raw),
			  ref_offset(*first_raw) + ref_totlen(c, jeb, *first_raw), c->wbuf_ofs);
		first_raw = &(*first_raw)->next_phys;
	}

	if (!*first_raw) {
		/* All nodes were obsolete. Nothing to recover. */
		DBG_WBUF(1, "No non-obsolete nodes to be recovered. Just filing block bad\n");
		spin_unlock(&c->erase_completion_lock);
		return;
	}

	start = ref_offset(*first_raw);
	end = ref_offset(*first_raw) + ref_totlen(c, jeb, *first_raw);

	/* Find the last node to be recovered */
	raw = first_raw;
	while ((*raw)) {
		if (!ref_obsolete(*raw))
			end = ref_offset(*raw) + ref_totlen(c, jeb, *raw);

		raw = &(*raw)->next_phys;
	}
	spin_unlock(&c->erase_completion_lock);

	DBG_WBUF(1, "wbuf recover %08x-%08x\n", start, end);

	buf = NULL;
	if (start < c->wbuf_ofs) {
		/* First affected node was already partially written.
		 * Attempt to reread the old data into our buffer. */

		buf = kmalloc(end - start, GFP_KERNEL);
		if (!buf) {
			WARNING_MSG("Malloc failure in wbuf recovery. Data loss ensues.\n");
			goto read_failed;
		}

		/* Do the read... */
		if (jffs3_cleanmarker_oob(c))
			ret = c->mtd->read_ecc(c->mtd, start, c->wbuf_ofs - start, &retlen, buf, NULL, c->oobinfo);
		else
			ret = c->mtd->read(c->mtd, start, c->wbuf_ofs - start, &retlen, buf);

		if (ret == -EBADMSG && retlen == c->wbuf_ofs - start) {
			/* ECC recovered */
			ret = 0;
		}
		if (ret || retlen != c->wbuf_ofs - start) {
			WARNING_MSG("Old data are already lost in wbuf recovery. Data loss ensues.\n");

			kfree(buf);
			buf = NULL;
		read_failed:
			first_raw = &(*first_raw)->next_phys;
			/* If this was the only node to be recovered, give up */
			if (!(*first_raw))
				return;

			/* It wasn't. Go on and try to recover nodes complete in the wbuf */
			start = ref_offset(*first_raw);
		} else {
			/* Read succeeded. Copy the remaining data from the wbuf */
			memcpy(buf + (c->wbuf_ofs - start), c->wbuf, end - c->wbuf_ofs);
		}
	}
	/* OK... we're to rewrite (end-start) bytes of data from first_raw onwards.
	   Either 'buf' contains the data, or we find it in the wbuf */


	/* ... and get an allocation of space from a shiny new block instead */
	ret = jffs3_reserve_space_gc(c, end-start, &ofs, &len, JFFS3_SUMMARY_NOSUM_SIZE);
	if (ret) {
		WARNING_MSG("Failed to allocate space for wbuf recovery. Data loss ensues.\n");
		kfree(buf);
		return;
	}
	if (end-start >= c->wbuf_pagesize) {
		/* Need to do another write immediately, but it's possible
		   that this is just because the wbuf itself is completely
		   full, and there's nothing earlier read back from the 
		   flash. Hence 'buf' isn't necessarily what we're writing 
		   from. */
		unsigned char *rewrite_buf = buf?:c->wbuf;
		uint32_t towrite = (end-start) - ((end-start)%c->wbuf_pagesize);

		DBG_WBUF(1, "Write 0x%x bytes at 0x%08x in wbuf recover\n",
			  towrite, ofs);

		if (jffs3_cleanmarker_oob(c))
			ret = c->mtd->write_ecc(c->mtd, ofs, towrite, &retlen,
						rewrite_buf, NULL, c->oobinfo);
		else
			ret = c->mtd->write(c->mtd, ofs, towrite, &retlen, rewrite_buf);

		if (ret || retlen != towrite) {
			/* Argh. We tried. Really we did. */
			WARNING_MSG("Recovery of wbuf failed due to a second write error\n");
			kfree(buf);

			if (retlen) {
				struct jffs3_raw_node_ref *raw2;
				uint32_t rawlen = ref_totlen(c, jeb, *first_raw);

				raw2 = jffs3_alloc_raw_node_ref();
				if (!raw2)
					return;

				raw2->flash_offset = ofs | REF_OBSOLETE;
#ifdef TMP_TOTLEN
				raw2->__totlen = rawlen;
#endif
				raw2->next_phys = NULL;
				raw2->next_in_ino = NULL;

				jffs3_add_physical_node_ref(c, raw2, rawlen);
			}
			return;
		}
		NOTICE_MSG("Recovery of wbuf succeeded to %08x\n", ofs);

		c->wbuf_len = (end - start) - towrite;
		c->wbuf_ofs = ofs + towrite;
		memmove(c->wbuf, rewrite_buf + towrite, c->wbuf_len);
		/* Don't muck about with c->wbuf_inodes. False positives are harmless. */
		kfree(buf);
	} else {
		/* OK, now we're left with the dregs in whichever buffer we're using */
		if (buf) {
			memcpy(c->wbuf, buf, end-start);
			kfree(buf);
		} else {
			memmove(c->wbuf, c->wbuf + (start - c->wbuf_ofs), end - start);
		}
		c->wbuf_ofs = ofs;
		c->wbuf_len = end - start;
	}

	/* Now sort out the jffs3_raw_node_refs, moving them from the old to the next block */
	new_jeb = &c->blocks[ofs / c->sector_size];

	spin_lock(&c->erase_completion_lock);
	if (new_jeb->first_node) {
		/* Odd, but possible with ST flash later maybe */
		new_jeb->last_node->next_phys = *first_raw;
	} else {
		new_jeb->first_node = *first_raw;
	}

	raw = first_raw;
	while (*raw) {
		uint32_t rawlen = ref_totlen(c, jeb, *raw);

		DBG_WBUF(1, "Refiling block of %08x at %08x(%d) to %08x\n",
			  rawlen, ref_offset(*raw), ref_flags(*raw), ofs);

		if (ref_obsolete(*raw)) {
			/* Shouldn't really happen much */
			new_jeb->dirty_size += rawlen;
			new_jeb->free_size -= rawlen;
			c->dirty_size += rawlen;
		} else {
			new_jeb->used_size += rawlen;
			new_jeb->free_size -= rawlen;
			jeb->dirty_size += rawlen;
			jeb->used_size  -= rawlen;
			c->dirty_size += rawlen;
		}
		c->free_size -= rawlen;
		(*raw)->flash_offset = ofs | ref_flags(*raw);
		ofs += rawlen;
		new_jeb->last_node = *raw;

		raw = &(*raw)->next_phys;
	}

	/* Fix up the original jeb now it's on the bad_list */
	*first_raw = NULL;
	if (first_raw == &jeb->first_node) {
		jeb->last_node = NULL;
		DBG_WBUF(1, "Failing block at %08x is now empty. Moving to erase_pending_list\n", jeb->offset);
		list_del(&jeb->list);
		list_add(&jeb->list, &c->erase_pending_list);
		c->nr_erasing_blocks++;
		jffs3_erase_pending_trigger(c);
	}
	else
		jeb->last_node = container_of(first_raw, struct jffs3_raw_node_ref, next_phys);

	jffs3_dbg_acct_sanity_check(c, jeb);
	jffs3_dbg_acct_sanity_check(c, new_jeb);
	if (PARANOIA) {
		jffs3_dbg_acct_paranoia_check(c, jeb);
		jffs3_dbg_acct_paranoia_check(c, new_jeb);
	}

	spin_unlock(&c->erase_completion_lock);

	DBG_WBUF(1, "wbuf recovery completed OK\n");
}

/* Meaning of pad argument:
   0: Do not pad. Probably pointless - we only ever use this when we can't pad anyway.
   1: Pad, do not adjust nextblock free_size
   2: Pad, adjust nextblock free_size
*/
#define NOPAD		0
#define PAD_NOACCOUNT	1
#define PAD_ACCOUNTING	2

static int __jffs3_flush_wbuf(struct jffs3_sb_info *c, int pad)
{
	int ret;
	size_t retlen;

	/* Nothing to do if not write-buffering the flash. In particular, we shouldn't
	   del_timer() the timer we never initialised. */
	if (!jffs3_is_writebuffered(c))
		return 0;

	DBG_WBUF(2, "pad %d, wbuf_ofs %#08x, wbuf_len %#x, to pad %#x\n", pad,
			c->wbuf_ofs, c->wbuf_len, c->wbuf_pagesize - c->wbuf_len);

	if (PARANOIA) {
		if (!down_trylock(&c->alloc_sem)) {
			up(&c->alloc_sem);
			ERROR_MSG("Called with alloc_sem not locked!\n");
			BUG();
		}
	} else {
		down(&c->alloc_sem);
	}

	if (!c->wbuf_len)	/* already checked c->wbuf above */
		return 0;

	/* claim remaining space on the page
	   this happens, if we have a change to a new block,
	   or if fsync forces us to flush the writebuffer.
	   if we have a switch to next page, we will not have
	   enough remaining space for this.
	*/
	if (pad) {
		c->wbuf_len = PAD(c->wbuf_len);

		/* Pad with JFFS3_DIRTY_BITMASK initially.  this helps out ECC'd NOR
		   with 8 byte page size */
		memset(c->wbuf + c->wbuf_len, 0, c->wbuf_pagesize - c->wbuf_len);

		if ( c->wbuf_len + sizeof(struct jffs3_unknown_node) < c->wbuf_pagesize) {
			struct jffs3_unknown_node *padnode = (void *)(c->wbuf + c->wbuf_len);
			padnode->magic = cpu_to_je16(JFFS3_MAGIC_BITMASK);
			padnode->nodetype = cpu_to_je16(JFFS3_NODETYPE_PADDING);
			padnode->totlen = cpu_to_je32(c->wbuf_pagesize - c->wbuf_len);
			padnode->hdr_crc = cpu_to_je32(crc32(0, padnode, sizeof(*padnode)-4));
		}
	}
	/* else jffs3_flash_writev has actually filled in the rest of the
	   buffer for us, and will deal with the node refs etc. later. */

	if (jffs3_cleanmarker_oob(c))
		ret = c->mtd->write_ecc(c->mtd, c->wbuf_ofs, c->wbuf_pagesize, &retlen, c->wbuf, NULL, c->oobinfo);
	else
		ret = c->mtd->write(c->mtd, c->wbuf_ofs, c->wbuf_pagesize, &retlen, c->wbuf);

	if (ret || retlen != c->wbuf_pagesize) {
		if (ret)
			WARNING_MSG("Write failed with %d\n",ret);
		else {
			WARNING_MSG("Write was short: %zd instead of %d\n", retlen, c->wbuf_pagesize);
			ret = -EIO;
		}

		jffs3_wbuf_recover(c);

		return ret;
	}

	/* Adjust free size of the block if we padded. */
	if (pad && c->wbuf_len < c->wbuf_pagesize) {
		struct jffs3_raw_node_ref *ref;

		jffs3_dbg_wbuf_acct_sanity_check(c, &c->blocks[c->wbuf_ofs / c->sector_size]);

		ref = jffs3_alloc_raw_node_ref();
		if (!ref)
			return -ENOMEM;

		ref->flash_offset = PAD(c->wbuf_ofs + c->wbuf_len) | REF_OBSOLETE;
#ifdef TMP_TOTLEN
		ref->__totlen = c->wbuf_pagesize - c->wbuf_len;
#endif
		ref->next_in_ino = NULL;
		ref->next_phys = NULL;
		DBG_WBUF(1, "Add padding node of length %#x to the node_refs list\n",
				c->wbuf_pagesize - c->wbuf_len);
		jffs3_add_physical_node_ref(c, ref, c->wbuf_pagesize - c->wbuf_len);
	}

	/* Stick any now-obsoleted blocks on the erase_pending_list */
	spin_lock(&c->erase_completion_lock);
	jffs3_refile_wbuf_blocks(c);
	jffs3_clear_wbuf_ino_list(c);
	spin_unlock(&c->erase_completion_lock);

	memset(c->wbuf, 0xff, c->wbuf_pagesize);
	/* adjust write buffer offset, else we get a non contiguous write bug */
	c->wbuf_ofs += c->wbuf_pagesize;
	c->wbuf_len = 0;
	return 0;
}

/* Trigger garbage collection to flush the write-buffer.
   If ino arg is zero, do it if _any_ real (i.e. not GC) writes are
   outstanding. If ino arg non-zero, do it only if a write for the
   given inode is outstanding. */
int jffs3_flush_wbuf_gc(struct jffs3_sb_info *c, uint32_t ino)
{
	uint32_t old_wbuf_ofs;
	uint32_t old_wbuf_len;
	int ret = 0;

	DBG_WBUF(1, "Ino #%u...\n", ino);

	if (!c->wbuf)
		return 0;

	down(&c->alloc_sem);
	if (!jffs3_wbuf_pending_for_ino(c, ino)) {
		DBG_WBUF(1, "Ino #%d not pending in wbuf. Returning\n", ino);
		up(&c->alloc_sem);
		return 0;
	}

	old_wbuf_ofs = c->wbuf_ofs;
	old_wbuf_len = c->wbuf_len;

	if (c->unchecked_size) {
		/* GC won't make any progress for a while */
		DBG_WBUF(1, "Padding. Not finished checking\n");
		down_write(&c->wbuf_sem);
		ret = __jffs3_flush_wbuf(c, PAD_ACCOUNTING);
		/* retry flushing wbuf in case jffs3_wbuf_recover
		   left some data in the wbuf */
		if (ret)
			ret = __jffs3_flush_wbuf(c, PAD_ACCOUNTING);
		up_write(&c->wbuf_sem);
	} else while (old_wbuf_len &&
		      old_wbuf_ofs == c->wbuf_ofs) {

		up(&c->alloc_sem);

		DBG_WBUF(1, "Call gc pass\n");

		ret = jffs3_garbage_collect_pass(c);
		if (ret) {
			/* GC failed. Flush it with padding instead */
			down(&c->alloc_sem);
			down_write(&c->wbuf_sem);
			ret = __jffs3_flush_wbuf(c, PAD_ACCOUNTING);
			/* retry flushing wbuf in case jffs3_wbuf_recover
			   left some data in the wbuf */
			if (ret)
				ret = __jffs3_flush_wbuf(c, PAD_ACCOUNTING);
			up_write(&c->wbuf_sem);
			break;
		}
		down(&c->alloc_sem);
	}

	DBG_WBUF(1, "End...\n");

	up(&c->alloc_sem);
	return ret;
}

/* Pad write-buffer to end and write it, wasting space. */
int jffs3_flush_wbuf_pad(struct jffs3_sb_info *c)
{
	int ret;

	if (!c->wbuf)
		return 0;

	down_write(&c->wbuf_sem);
	ret = __jffs3_flush_wbuf(c, PAD_NOACCOUNT);
	/* retry - maybe wbuf recover left some data in wbuf. */
	if (ret)
		ret = __jffs3_flush_wbuf(c, PAD_NOACCOUNT);
	up_write(&c->wbuf_sem);

	return ret;
}

#define PAGE_DIV(x) ( (x) & (~(c->wbuf_pagesize - 1)) )
#define PAGE_MOD(x) ( (x) & (c->wbuf_pagesize - 1) )
int jffs3_flash_writev(struct jffs3_sb_info *c, const struct kvec *invecs, unsigned long count, loff_t to, size_t *retlen, uint32_t ino)
{
	struct kvec outvecs[3];
	uint32_t totlen = 0;
	uint32_t split_ofs = 0;
	uint32_t old_totlen;
	int ret, splitvec = -1;
	int invec, outvec;
	size_t wbuf_retlen;
	unsigned char *wbuf_ptr;
	size_t donelen = 0;
	uint32_t outvec_to = to;

	/* If not NAND flash, don't bother */
	if (!jffs3_is_writebuffered(c)) {
		return jffs3_flash_direct_writev(c, invecs, count, to, retlen);
	}

	down_write(&c->wbuf_sem);

	/* If wbuf_ofs is not initialized, set it to target address */
	if (c->wbuf_ofs == 0xFFFFFFFF) {
		c->wbuf_ofs = PAGE_DIV(to);
		c->wbuf_len = PAGE_MOD(to);
		memset(c->wbuf,0xff,c->wbuf_pagesize);
	}

	/* Fixup the wbuf if we are moving to a new eraseblock.  The checks below
	   fail for ECC'd NOR because cleanmarker == 16, so a block starts at
	   xxx0010.  */
	if (jffs3_nor_ecc(c)) {
		if (((c->wbuf_ofs % c->sector_size) == 0) && !c->wbuf_len) {
			c->wbuf_ofs = PAGE_DIV(to);
			c->wbuf_len = PAGE_MOD(to);
			memset(c->wbuf,0xff,c->wbuf_pagesize);
		}
	}

	/* Sanity checks on target address.
	   It's permitted to write at PAD(c->wbuf_len+c->wbuf_ofs),
	   and it's permitted to write at the beginning of a new
	   erase block. Anything else, and you die.
	   New block starts at xxx000c (0-b = block header)
	*/
	if (SECTOR_ADDR(to) != SECTOR_ADDR(c->wbuf_ofs)) {
		/* It's a write to a new block */
		if (c->wbuf_len) {
			DBG_WBUF(1, "writev to 0x%lx causes flush of wbuf at 0x%08x\n", (unsigned long)to, c->wbuf_ofs);
			ret = __jffs3_flush_wbuf(c, PAD_NOACCOUNT);
			if (ret) {
				/* the underlying layer has to check wbuf_len to do the cleanup */
				WARNING_MSG("called from jffs3_flash_writev() failed %d\n", ret);
				*retlen = 0;
				goto exit;
			}
		}
		/* set pointer to new block */
		c->wbuf_ofs = PAGE_DIV(to);
		c->wbuf_len = PAGE_MOD(to);
	}

	if (SANITY && to != PAD(c->wbuf_ofs + c->wbuf_len)) {
		/* We're not writing immediately after the writebuffer. Bad. */
		ERROR_MSG("Non-contiguous write to %08lx\n", (unsigned long)to);
		if (c->wbuf_len)
			 ERROR_MSG("wbuf was previously %08x-%08x\n",
					  c->wbuf_ofs, c->wbuf_ofs+c->wbuf_len);
		BUG();
	}

	/* Note outvecs[3] above. We know count is never greater than 2 */
	if (count > 2) {
		ERROR_MSG("jffs3_flash_writev(): count is %ld\n", count);
		BUG();
	}

	invec = 0;
	outvec = 0;

	/* Fill writebuffer first, if already in use */
	if (c->wbuf_len) {
		uint32_t invec_ofs = 0;

		/* adjust alignment offset */
		if (c->wbuf_len != PAGE_MOD(to)) {
			c->wbuf_len = PAGE_MOD(to);
			/* take care of alignment to next page */
			if (!c->wbuf_len)
				c->wbuf_len = c->wbuf_pagesize;
		}

		while(c->wbuf_len < c->wbuf_pagesize) {
			uint32_t thislen;

			if (invec == count)
				goto alldone;

			thislen = c->wbuf_pagesize - c->wbuf_len;

			if (thislen >= invecs[invec].iov_len)
				thislen = invecs[invec].iov_len;

			invec_ofs = thislen;

			memcpy(c->wbuf + c->wbuf_len, invecs[invec].iov_base, thislen);
			c->wbuf_len += thislen;
			donelen += thislen;
			/* Get next invec, if actual did not fill the buffer */
			if (c->wbuf_len < c->wbuf_pagesize)
				invec++;
		}

		/* write buffer is full, flush buffer */
		ret = __jffs3_flush_wbuf(c, NOPAD);
		if (ret) {
			/* the underlying layer has to check wbuf_len to do the cleanup */
			WARNING_MSG("called from jffs3_flash_writev() failed %d\n", ret);
			/* Retlen zero to make sure our caller doesn't mark the space dirty.
			   We've already done everything that's necessary */
			*retlen = 0;
			goto exit;
		}
		outvec_to += donelen;
		c->wbuf_ofs = outvec_to;

		/* All invecs done ? */
		if (invec == count)
			goto alldone;

		/* Set up the first outvec, containing the remainder of the
		   invec we partially used */
		if (invecs[invec].iov_len > invec_ofs) {
			outvecs[0].iov_base = invecs[invec].iov_base+invec_ofs;
			totlen = outvecs[0].iov_len = invecs[invec].iov_len-invec_ofs;
			if (totlen > c->wbuf_pagesize) {
				splitvec = outvec;
				split_ofs = outvecs[0].iov_len - PAGE_MOD(totlen);
			}
			outvec++;
		}
		invec++;
	}

	/* OK, now we've flushed the wbuf and the start of the bits
	   we have been asked to write, now to write the rest.... */

	/* totlen holds the amount of data still to be written */
	old_totlen = totlen;
	for ( ; invec < count; invec++,outvec++ ) {
		outvecs[outvec].iov_base = invecs[invec].iov_base;
		totlen += outvecs[outvec].iov_len = invecs[invec].iov_len;
		if (PAGE_DIV(totlen) != PAGE_DIV(old_totlen)) {
			splitvec = outvec;
			split_ofs = outvecs[outvec].iov_len - PAGE_MOD(totlen);
			old_totlen = totlen;
		}
	}

	/* Now the outvecs array holds all the remaining data to write */
	/* Up to splitvec,split_ofs is to be written immediately. The rest
	   goes into the (now-empty) wbuf */

	if (splitvec != -1) {
		uint32_t remainder;

		remainder = outvecs[splitvec].iov_len - split_ofs;
		outvecs[splitvec].iov_len = split_ofs;

		/* We did cross a page boundary, so we write some now */
		if (jffs3_cleanmarker_oob(c))
			ret = c->mtd->writev_ecc(c->mtd, outvecs, splitvec+1, outvec_to, &wbuf_retlen, NULL, c->oobinfo);
		else
			ret = jffs3_flash_direct_writev(c, outvecs, splitvec+1, outvec_to, &wbuf_retlen);

		if (ret < 0 || wbuf_retlen != PAGE_DIV(totlen)) {
			/* At this point we have no problem,
			   c->wbuf is empty. However refile nextblock to avoid
			   writing again to same address.
			*/
			struct jffs3_eraseblock *jeb;

			spin_lock(&c->erase_completion_lock);

			jeb = &c->blocks[outvec_to / c->sector_size];
			jffs3_block_refile(c, jeb, REFILE_ANYWAY);

			*retlen = 0;
			spin_unlock(&c->erase_completion_lock);

			goto exit;
		}

		donelen += wbuf_retlen;
		c->wbuf_ofs = PAGE_DIV(outvec_to) + PAGE_DIV(totlen);

		if (remainder) {
			outvecs[splitvec].iov_base += split_ofs;
			outvecs[splitvec].iov_len = remainder;
		} else {
			splitvec++;
		}

	} else {
		splitvec = 0;
	}

	/* Now splitvec points to the start of the bits we have to copy
	   into the wbuf */
	wbuf_ptr = c->wbuf;

	for ( ; splitvec < outvec; splitvec++) {
		/* Don't copy the wbuf into itself */
		if (outvecs[splitvec].iov_base == c->wbuf)
			continue;
		memcpy(wbuf_ptr, outvecs[splitvec].iov_base, outvecs[splitvec].iov_len);
		wbuf_ptr += outvecs[splitvec].iov_len;
		donelen += outvecs[splitvec].iov_len;
	}
	c->wbuf_len = wbuf_ptr - c->wbuf;

	/* If there's a remainder in the wbuf and it's a non-GC write,
	   remember that the wbuf affects this ino */
alldone:
	*retlen = donelen;

#ifdef CONFIG_JFFS3_SUMMARY
	if(jffs3_sum_add_kvec(c,invecs,count,(uint32_t) to)){
		ERROR_MSG("jffs3_sum_add_kvec(): MEMORY ALLOCATION ERROR!");
	}
#endif

	if (c->wbuf_len && ino)
		jffs3_wbuf_dirties_inode(c, ino);

	ret = 0;

exit:
	up_write(&c->wbuf_sem);
	return ret;
}

/*
 *	This is the entry for flash write.
 *	Check, if we work on NAND FLASH, if so build an kvec and write it via vritev
*/
int jffs3_flash_write(struct jffs3_sb_info *c, loff_t ofs, size_t len, size_t *retlen, const u_char *buf)
{
	struct kvec vecs[1];

	if (!jffs3_is_writebuffered(c))
		return c->mtd->write(c->mtd, ofs, len, retlen, buf);

	vecs[0].iov_base = (unsigned char *) buf;
	vecs[0].iov_len = len;
	return jffs3_flash_writev(c, vecs, 1, ofs, retlen, 0);
}

/*
	Handle readback from writebuffer and ECC failure return
*/
int jffs3_flash_read(struct jffs3_sb_info *c, loff_t ofs, size_t len, size_t *retlen, u_char *buf)
{
	loff_t	orbf = 0, owbf = 0, lwbf = 0;
	int	ret;

	if (!jffs3_is_writebuffered(c))
		return c->mtd->read(c->mtd, ofs, len, retlen, buf);

	/* Read flash */
	if (jffs3_cleanmarker_oob(c))
		ret = c->mtd->read_ecc(c->mtd, ofs, len, retlen, buf, NULL, c->oobinfo);
	else
		ret = c->mtd->read(c->mtd, ofs, len, retlen, buf);

	if ( (ret == -EBADMSG) && (*retlen == len) ) {
		WARNING_MSG("mtd->read(0x%zx bytes from 0x%llx) returned ECC error\n", len, ofs);
		/*
		 * We have the raw data without ECC correction in the buffer, maybe
		 * we are lucky and all data or parts are correct. We check the node.
		 * If data are corrupted node check will sort it out.
		 * We keep this block, it will fail on write or erase and the we
		 * mark it bad. Or should we do that now? But we should give him a chance.
		 * Maybe we had a system crash or power loss before the ecc write or
		 * a erase was completed.
		 * So we return success. :)
		 */
	 	ret = 0;
	 }

	/* if no writebuffer available or write buffer empty, return */
	if (!c->wbuf_pagesize || !c->wbuf_len)
		return ret;

	/* if we read in a different block, return */
	if (SECTOR_ADDR(ofs) != SECTOR_ADDR(c->wbuf_ofs))
		return ret;

	/* Lock only if we have reason to believe wbuf contains relevant data,
	   so that checking an erased block during wbuf recovery space allocation
	   does not deadlock. */
	down_read(&c->wbuf_sem);

	if (ofs >= c->wbuf_ofs) {
		owbf = (ofs - c->wbuf_ofs);	/* offset in write buffer */
		if (owbf > c->wbuf_len)		/* is read beyond write buffer ? */
			goto exit;
		lwbf = c->wbuf_len - owbf;	/* number of bytes to copy */
		if (lwbf > len)
			lwbf = len;
	} else {
		orbf = (c->wbuf_ofs - ofs);	/* offset in read buffer */
		if (orbf > len)			/* is write beyond write buffer ? */
			goto exit;
		lwbf = len - orbf; 		/* number of bytes to copy */
		if (lwbf > c->wbuf_len)
			lwbf = c->wbuf_len;
	}
	if (lwbf > 0)
		memcpy(buf+orbf,c->wbuf+owbf,lwbf);

exit:
	up_read(&c->wbuf_sem);
	return ret;
}

/*
 *	Check, if the out of band area is empty
 */
int jffs3_check_oob_empty( struct jffs3_sb_info *c, struct jffs3_eraseblock *jeb, int mode)
{
	unsigned char *buf;
	int 	ret = 0;
	int	i,len,page;
	size_t  retlen;
	int	oob_size;

	/* allocate a buffer for all oob data in this sector */
	oob_size = c->mtd->oobsize;
	len = 4 * oob_size;
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		WARNING_MSG("Allocation of temporary data buffer for oob check failed\n");
		return -ENOMEM;
	}
	/*
	 * if mode = 0, we scan for a total empty oob area, else we have
	 * to take care of the cleanmarker in the first page of the block
	*/
	ret = jffs3_flash_read_oob(c, jeb->offset, len , &retlen, buf);
	if (ret) {
		WARNING_MSG("Read OOB failed %d for block at %08x\n", ret, jeb->offset);
		goto out;
	}

	if (retlen < len) {
		 WARNING_MSG("jffs3_check_oob_empty(): Read OOB return short read "
			  "(%zd bytes not %d) for block at %08x\n", retlen, len, jeb->offset);
		ret = -EIO;
		goto out;
	}

	/* Special check for first page */
	for(i = 0; i < oob_size ; i++) {
		/* Yeah, we know about the cleanmarker. */
		if (mode && i >= c->fsdata_pos &&
		    i < c->fsdata_pos + c->fsdata_len)
			continue;

		if (buf[i] != 0xFF) {
			DBG_WBUF(2, "Found %02x at %x in OOB for %08x\n", buf[i], i, jeb->offset);
			ret = 1;
			goto out;
		}
	}

	/* we know, we are aligned :) */
	for (page = oob_size; page < len; page += sizeof(long)) {
		unsigned long dat = *(unsigned long *)(&buf[page]);
		if(dat != -1) {
			ret = 1;
			goto out;
		}
	}

out:
	kfree(buf);

	return ret;
}

/*
*	Scan for a valid cleanmarker and for bad blocks
*	For virtual blocks (concatenated physical blocks) check the cleanmarker
*	only in the first page of the first physical block, but scan for bad blocks in all
*	physical blocks
*/
int jffs3_check_nand_cleanmarker (struct jffs3_sb_info *c, struct jffs3_eraseblock *jeb)
{
	struct jffs3_unknown_node n;
	unsigned char buf[2 * NAND_MAX_OOBSIZE];
	unsigned char *p;
	int ret, i, cnt, retval = 0;
	size_t retlen, offset;
	int oob_size;

	offset = jeb->offset;
	oob_size = c->mtd->oobsize;

	/* Loop through the physical blocks */
	for (cnt = 0; cnt < (c->sector_size / c->mtd->erasesize); cnt++) {
		/* Check first if the block is bad. */
		if (c->mtd->block_isbad (c->mtd, offset)) {
			DBG_WBUF(1, "Bad block at %08x\n", jeb->offset);
			return 2;
		}
		/*
		   *    We read oob data from page 0 and 1 of the block.
		   *    page 0 contains cleanmarker and badblock info
		   *    page 1 contains failure count of this block
		 */
		ret = c->mtd->read_oob (c->mtd, offset, oob_size << 1, &retlen, buf);

		if (ret) {
			DBG_WBUF(1, "Read OOB failed %d for block at %08x\n", ret, jeb->offset);
			return ret;
		}
		if (retlen < (oob_size << 1)) {
			DBG_WBUF(1, "Read OOB return short read (%zd bytes not %d) for block at %08x\n",
				retlen, oob_size << 1, jeb->offset);
			return -EIO;
		}

		/* Check cleanmarker only on the first physical block */
		if (!cnt) {
			n.magic = cpu_to_je16 (JFFS3_MAGIC_BITMASK);
			n.nodetype = cpu_to_je16 (JFFS3_NODETYPE_CLEANMARKER);
			n.totlen = cpu_to_je32 (8);
			p = (unsigned char *) &n;

			for (i = 0; i < c->fsdata_len; i++) {
				if (buf[c->fsdata_pos + i] != p[i]) {
					retval = 1;
				}
			}

			if (retval == 1) {
				DBG_WBUF(1, "Cleanmarker node not detected in block at %08x\n", jeb->offset);
				DBG_WBUF(1, "OOB at %08x was ", offset);
				if (PARANOIA)
					jffs3_dbg_dump_buffer(buf, oob_size, 0);
			}
		}
		offset += c->mtd->erasesize;
	}
	return retval;
}

int jffs3_write_nand_cleanmarker(struct jffs3_sb_info *c, struct jffs3_eraseblock *jeb)
{
	struct 	jffs3_unknown_node n;
	int 	ret;
	size_t 	retlen;

	n.magic = cpu_to_je16(JFFS3_MAGIC_BITMASK);
	n.nodetype = cpu_to_je16(JFFS3_NODETYPE_CLEANMARKER);
	n.totlen = cpu_to_je32(8);

	ret = jffs3_flash_write_oob(c, jeb->offset + c->fsdata_pos, c->fsdata_len, &retlen, (unsigned char *)&n);

	if (ret) {
		WARNING_MSG("Write failed for block at %08x: error %d\n", jeb->offset, ret);
		return ret;
	}
	if (retlen != c->fsdata_len) {
		WARNING_MSG("Short write for block at %08x: %zd not %d\n", jeb->offset, retlen, c->fsdata_len);
		return ret;
	}
	return 0;
}

/*
 * On NAND we try to mark this block bad. If the block was erased more
 * than MAX_ERASE_FAILURES we mark it finaly bad.
 * Don't care about failures. This block remains on the erase-pending
 * or badblock list as long as nobody manipulates the flash with
 * a bootloader or something like that.
 */

int jffs3_write_nand_badblock(struct jffs3_sb_info *c, struct jffs3_eraseblock *jeb, uint32_t bad_offset)
{
	int 	ret;

	/* if the count is < max, we try to write the counter to the 2nd page oob area */
	if( ++jeb->bad_count < MAX_ERASE_FAILURES)
		return 0;

	if (!c->mtd->block_markbad)
		return 1; // What else can we do?

	NOTICE_MSG("jffs3_write_nand_badblock(): Marking bad block at %08x\n", bad_offset);
	ret = c->mtd->block_markbad(c->mtd, bad_offset);

	if (ret) {
		WARNING_MSG("Write failed for block at %08x: error %d\n", jeb->offset, ret);
		return ret;
	}
	return 1;
}

#define NAND_JFFS3_OOB16_FSDALEN	8

static struct nand_oobinfo jffs3_oobinfo_docecc = {
	.useecc = MTD_NANDECC_PLACE,
	.eccbytes = 6,
	.eccpos = {0,1,2,3,4,5}
};


int jffs3_nand_set_oobinfo(struct jffs3_sb_info *c)
{
	struct nand_oobinfo *oinfo = &c->mtd->oobinfo;

	/* Do this only, if we have an oob buffer */
	if (!c->mtd->oobsize)
		return 0;

	/* Cleanmarker is out-of-band, so inline size zero */
	c->cleanmarker_size = 0;

	/* Should we use autoplacement ? */
	if (oinfo && oinfo->useecc == MTD_NANDECC_AUTOPLACE) {
		DBG_WBUF(1, "JFFS3 using autoplace on NAND\n");
		/* Get the position of the free bytes */
		if (!oinfo->oobfree[0][1]) {
			WARNING_MSG("Eeep. Autoplacement selected and no empty space in oob\n");
			return -ENOSPC;
		}
		c->fsdata_pos = oinfo->oobfree[0][0];
		c->fsdata_len = oinfo->oobfree[0][1];
		if (c->fsdata_len > 8)
			c->fsdata_len = 8;
	} else {
		/* This is just a legacy fallback and should go away soon */
		switch(c->mtd->ecctype) {
		case MTD_ECC_RS_DiskOnChip:
			WARNING_MSG("JFFS3 using DiskOnChip hardware ECC without autoplacement. Fix it!\n");
			c->oobinfo = &jffs3_oobinfo_docecc;
			c->fsdata_pos = 6;
			c->fsdata_len = NAND_JFFS3_OOB16_FSDALEN;
			c->badblock_pos = 15;
			break;

		default:
			DBG_WBUF(1, "JFFS3 on NAND. No autoplacment info found\n");
			return -EINVAL;
		}
	}
	return 0;
}

int jffs3_nand_flash_setup(struct jffs3_sb_info *c)
{
	int res;

	/* Initialise write buffer */
	init_rwsem(&c->wbuf_sem);
	c->wbuf_pagesize = c->mtd->oobblock;
	c->wbuf_ofs = 0xFFFFFFFF;

	c->wbuf = kmalloc(c->wbuf_pagesize, GFP_KERNEL);
	if (!c->wbuf)
		return -ENOMEM;

	res = jffs3_nand_set_oobinfo(c);

	return res;
}

void jffs3_nand_flash_cleanup(struct jffs3_sb_info *c)
{
	kfree(c->wbuf);
}

int jffs3_nor_ecc_flash_setup(struct jffs3_sb_info *c) {
	/* Cleanmarker is actually larger on the flashes */
	c->cleanmarker_size = 16;

	/* Initialize write buffer */
	init_rwsem(&c->wbuf_sem);
	c->wbuf_pagesize = c->mtd->eccsize;
	c->wbuf_ofs = 0xFFFFFFFF;

	c->wbuf = kmalloc(c->wbuf_pagesize, GFP_KERNEL);
	if (!c->wbuf)
		return -ENOMEM;

	return 0;
}

void jffs3_nor_ecc_flash_cleanup(struct jffs3_sb_info *c) {
	kfree(c->wbuf);
}
