/* Final hand-over to the kernel. Uses registers only: the copy may overwrite the
 * loader's own data, heap and stack, so nothing here may touch memory except dst/src.
 *   r3 = destination (virtual, cached)   r4 = source   r5 = bytes   r6 = entry point
 */
	.text
	.globl	cl_copy_and_jump
cl_copy_and_jump:
	li	9, 0
1:	cmplw	9, 5
	bge	2f
	lwzx	10, 4, 9
	stwx	10, 3, 9
	addi	9, 9, 4
	b	1b
2:	li	9, 0		/* write the copied lines back to RAM */
3:	cmplw	9, 5
	bge	4f
	dcbst	3, 9
	addi	9, 9, 32
	b	3b
4:	sync
	li	9, 0		/* and drop stale instructions */
5:	cmplw	9, 5
	bge	6f
	icbi	3, 9
	addi	9, 9, 32
	b	5b
6:	sync
	isync
	mtctr	6
	bctr
